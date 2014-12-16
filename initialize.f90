! This model contains routines to:
! Generate a 1D isothermal white dwarf in hydrostatic equilibrium
! Interpolate a 1D WD model onto a 3D Cartesian grid

module initial_model_module

use bl_types
use bl_constants_module
use bl_error_module
use eos_module, only: eos_input_rt, eos, eos_init
use eos_type_module
use network, only: nspec
use model_parser_module, only: itemp_model, idens_model, ipres_model, ispec_model
use fundamental_constants_module, only: Gconst, M_solar

contains

  subroutine init_1d(model_r, model_hse, nx, dx, mass, temp_core, xn_core, dens_ambient, temp_ambient)

    implicit none

    ! Arguments

    integer,          intent(in   ) :: nx

    double precision, intent(in   ) :: dens_ambient, temp_ambient
    double precision, intent(in   ) :: dx, mass

    double precision, intent(in   ) :: temp_core, xn_core(nspec)
    double precision, intent(inout) :: model_hse(nx,3+nspec)
    double precision, intent(inout) :: model_r(nx)

    ! Local variables

    double precision :: temp_base, delta

    double precision :: xzn_hse(nx), xznl(nx), xznr(nx), M_enclosed(nx), cs_hse(nx)

    integer :: i, n

    double precision :: rho_c, rho_c_old, mass_wd, mass_wd_old, drho_c

    double precision :: slope_T, slope_xn(nspec)

    double precision :: A, B, dAdT, dAdrho, dBdT, dBdrho

    logical :: isentropic

    double precision :: test

    integer, parameter :: nvar = 3 + nspec

    double precision :: xmin, xmax, dCoord

    double precision :: dens_zone, temp_zone, pres_zone, entropy
    double precision :: dpd, dpt, dsd, dst

    double precision :: p_want, drho, dtemp, delx
    double precision :: entropy_base

    double precision :: g_zone

    ! TOL_HSE is the tolerance used when iterating over a zone to force
    ! it into HSE by adjusting the current density (and possibly
    ! temperature).  TOL_HSE should be very small (~ 1.e-10).
    double precision, parameter :: TOL_HSE = 1.d-10

    ! TOL_WD_MASS is tolerance used for getting the total WD mass equal
    ! to M_tot (defined below).  It can be reasonably small, since there
    ! will always be a central density value that can give the desired
    ! WD mass on the grid we use
    double precision, parameter :: TOL_WD_MASS = 1.d-4

    integer, parameter :: MAX_ITER = 250

    integer :: iter, iter_mass

    integer :: icutoff

    logical :: converged_hse, fluff, mass_converged

    double precision, dimension(nspec) :: xn

    double precision :: smallx, smallt

    double precision :: M_tot

    double precision :: max_hse_error, dpdr, rhog

    integer :: narg

    type (eos_t) :: eos_state

    ! convert the envelope and WD mass into solar masses
    M_tot = mass * M_solar

    !----------------------------------------------------------------------------
    ! Create a 1-d uniform grid that is identical to the mesh that we are
    ! mapping onto, and then we want to force it into HSE on that mesh.
    !----------------------------------------------------------------------------

    xmin = 0.000e9
    xmax = 1.024e9

    ! compute the coordinates of the new gridded function
    dCoord = (xmax - xmin) / dble(nx)

    do i = 1, nx
       xznl(i) = xmin + (dble(i) - ONE)*dCoord
       xznr(i) = xmin + (dble(i))*dCoord
       model_r(i) = HALF*(xznl(i) + xznr(i))
    enddo

    print *, idens_model, itemp_model, ipres_model, ispec_model

    ! We don't know what WD central density will give the desired total
    ! mass, so we need to iterate over central density

    ! we will do a secant iteration.  rho_c_old is the 'old' guess for
    ! the central density and rho_c is the current guess.  After 2
    ! loops, we can start estimating the density required to yield our
    ! desired mass
    rho_c_old = -ONE
    rho_c     = 1.d7     ! A reasonable starting guess for moderate-mass WDs

    mass_converged = .false.


    do iter_mass = 1, MAX_ITER

       print *, 'mass iter = ', iter_mass, rho_c, temp_core

       fluff = .false.

       ! we start at the center of the WD and integrate outward.  Initialize
       ! the central conditions.
       eos_state%T     = temp_core
       eos_state%rho   = rho_c
       eos_state%xn(:) = xn_core(:)

       ! (t, rho) -> (p, s)    
       call eos(eos_input_rt, eos_state, .false.)

       ! make the initial guess be completely uniform
       model_hse(:,idens_model) = eos_state%rho
       model_hse(:,itemp_model) = eos_state%T
       model_hse(:,ipres_model) = eos_state%p

       if (iter_mass .eq. 1) then
          print *, model_r
       endif

       do i = 1, nspec
          model_hse(:,ispec_model-1+i) = eos_state%xn(i)
          print *, i, ispec_model-1+i, model_hse(:,ispec_model-1+i)
       enddo

       if (iter_mass .eq. 1) then
          print *, model_r
       endif

       ! keep track of the mass enclosed below the current zone
       M_enclosed(1) = FOUR3RD*M_PI*(xznr(1)**3 - xznl(1)**3)*model_hse(1,idens_model)


       !-------------------------------------------------------------------------
       ! HSE + entropy solve
       !-------------------------------------------------------------------------
       do i = 2, nx

          delx = model_r(i) - model_r(i-1)

          ! as the initial guess for the density, use the previous zone
          dens_zone = model_hse(i-1,idens_model)

          temp_zone = temp_core
          xn(:) = xn_core(:)

          isentropic = .false.


          g_zone = -Gconst*M_enclosed(i-1)/(xznl(i)*xznl(i))


          !----------------------------------------------------------------------
          ! iteration loop
          !----------------------------------------------------------------------

          ! start off the Newton loop by saying that the zone has not converged
          converged_hse = .FALSE.

          if (.not. fluff) then

             do iter = 1, MAX_ITER


                if (isentropic) then

                   p_want = model_hse(i-1,ipres_model) + &
                        delx*0.5_dp_t*(dens_zone + model_hse(i-1,idens_model))*g_zone


                   ! now we have two functions to zero:
                   !   A = p_want - p(rho,T)
                   !   B = entropy_base - s(rho,T)
                   ! We use a two dimensional Taylor expansion and find the deltas
                   ! for both density and temperature   

                   eos_state%T     = temp_zone
                   eos_state%rho   = dens_zone
                   eos_state%xn(:) = xn(:)

                   ! (t, rho) -> (p, s) 
                   call eos(eos_input_rt, eos_state, .false.)

                   entropy = eos_state%s
                   pres_zone = eos_state%p

                   dpt = eos_state%dpdt
                   dpd = eos_state%dpdr
                   dst = eos_state%dsdt
                   dsd = eos_state%dsdr

                   A = p_want - pres_zone
                   B = entropy_base - entropy

                   dAdT = -dpt
                   dAdrho = 0.5d0*delx*g_zone - dpd
                   dBdT = -dst
                   dBdrho = -dsd

                   dtemp = (B - (dBdrho/dAdrho)*A)/ &
                        ((dBdrho/dAdrho)*dAdT - dBdT)

                   drho = -(A + dAdT*dtemp)/dAdrho

                   dens_zone = max(0.9_dp_t*dens_zone, &
                                   min(dens_zone + drho, 1.1_dp_t*dens_zone))

                   temp_zone = max(0.9_dp_t*temp_zone, &
                                   min(temp_zone + dtemp, 1.1_dp_t*temp_zone))

                   ! check if the density falls below our minimum
                   ! cut-off -- if so, floor it
                   if (dens_zone < dens_ambient) then

                      dens_zone = dens_ambient
                      temp_zone = temp_ambient
                      converged_hse = .TRUE.
                      fluff = .TRUE.
                      exit

                   endif

                   if ( abs(drho) < TOL_HSE*dens_zone .and. &
                        abs(dtemp) < TOL_HSE*temp_zone) then
                      converged_hse = .TRUE.
                      exit
                   endif

                else
                   ! the core is isothermal, so we just need to constrain
                   ! the density and pressure to agree with the EOS and HSE

                   ! We difference HSE about the interface between the current
                   ! zone and the one just inside.
                   p_want = model_hse(i-1,ipres_model) + &
                        delx*0.5*(dens_zone + model_hse(i-1,idens_model))*g_zone

                   eos_state%T     = temp_zone
                   eos_state%rho   = dens_zone
                   eos_state%xn(:) = xn(:)

                   ! (t, rho) -> (p, s)
                   call eos(eos_input_rt, eos_state, .false.)

                   entropy = eos_state%s
                   pres_zone = eos_state%p

                   dpd = eos_state%dpdr

                   drho = (p_want - pres_zone)/(dpd - 0.5*delx*g_zone)

                   dens_zone = max(0.9*dens_zone, &
                        min(dens_zone + drho, 1.1*dens_zone))

                   ! (t, rho) -> (p, s)

                   if (abs(drho) < TOL_HSE*dens_zone) then
                      converged_hse = .TRUE.
                      exit
                   endif

                   if (dens_zone < dens_ambient) then

                      icutoff = i
                      dens_zone = dens_ambient
                      temp_zone = temp_ambient
                      converged_hse = .TRUE.
                      fluff = .TRUE.
                      exit

                   endif
                endif

                if (temp_zone < temp_ambient .and. isentropic) then
                   temp_zone = temp_ambient
                   isentropic = .false.
                endif


             enddo

             if (.NOT. converged_hse) then

                print *, 'Error zone', i, ' did not converge in init_1d'
                print *, dens_zone, temp_zone
                print *, p_want
                print *, drho
                call bl_error('Error: HSE non-convergence')

             endif

          else
             dens_zone = dens_ambient
             temp_zone = temp_ambient
          endif


          ! call the EOS one more time for this zone and then go on
          ! to the next
          eos_state%T     = temp_zone
          eos_state%rho   = dens_zone
          eos_state%xn(:) = xn(:)

          ! (t, rho) -> (p, s)    
          call eos(eos_input_rt, eos_state, .false.)

          pres_zone = eos_state%p


          ! update the thermodynamics in this zone
          model_hse(i,idens_model) = dens_zone
          model_hse(i,itemp_model) = temp_zone
          model_hse(i,ipres_model) = pres_zone

          model_hse(i,ispec_model:ispec_model-1+nspec) = xn(:)

          M_enclosed(i) = M_enclosed(i-1) + &
               FOUR3RD*M_PI*(xznr(i) - xznl(i))* &
               (xznr(i)**2 +xznl(i)*xznr(i) + xznl(i)**2)*model_hse(i,idens_model)

          cs_hse(i) = eos_state%cs

       enddo  ! end loop over zones


       mass_wd = FOUR3RD*M_PI*(xznr(1)**3 - xznl(1)**3)*model_hse(1,idens_model)

       do i = 2, icutoff
          mass_wd = mass_wd + &
               FOUR3RD*M_PI*(xznr(i) - xznl(i))* &
               (xznr(i)**2 +xznl(i)*xznr(i) + xznl(i)**2)*model_hse(i,idens_model)
       enddo


       if (rho_c_old < ZERO) then
          ! not enough iterations yet -- store the old central density and
          ! mass and pick a new value
          rho_c_old = rho_c
          mass_wd_old = mass_wd

          rho_c = HALF*rho_c_old

       else
          ! have we converged
          if ( abs(mass_wd - M_tot)/M_tot < TOL_WD_MASS ) then
             mass_converged = .true.
             exit
          endif

          ! do a secant iteration:
          ! M_tot = M(rho_c) + dM/drho |_rho_c x drho + ...        
          drho_c = (M_tot - mass_wd)/ &
               ( (mass_wd  - mass_wd_old)/(rho_c - rho_c_old) )

          rho_c_old = rho_c
          mass_wd_old = mass_wd

          rho_c = min(1.1d0*rho_c_old, &
                      max((rho_c + drho_c), 0.9d0*rho_c_old))

          print *, 'current mass = ', mass_wd/M_solar

       endif     

    enddo  ! end mass constraint loop

    print *, model_r

    if (.not. mass_converged) then
       print *, 'ERROR: WD mass did not converge'
       call bl_error("ERROR: mass did not converge")
    endif

    print *, 'final masses: '
    print *, ' mass WD: ', mass_wd/M_solar

    ! compute the maximum HSE error
    max_hse_error = -1.d30

    do i = 2, nx-1
       g_zone = -Gconst*M_enclosed(i-1)/xznr(i-1)**2
       dpdr = (model_hse(i,ipres_model) - model_hse(i-1,ipres_model))/delx
       rhog = HALF*(model_hse(i,idens_model) + model_hse(i-1,idens_model))*g_zone

       print *, model_r(i), g_zone*model_r(i)

       if (dpdr /= ZERO .and. model_hse(i+1,idens_model) > dens_ambient) then
          max_hse_error = max(max_hse_error, abs(dpdr - rhog)/abs(dpdr))
       endif

    enddo

    print *, 'maximum HSE error = ', max_hse_error
    print *, ' '

  end subroutine init_1d

end module initial_model_module
