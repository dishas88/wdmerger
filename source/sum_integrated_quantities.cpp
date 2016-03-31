#include <cmath>

#include <iomanip>

#include <vector>

#include <Castro.H>
#include <Castro_F.H>
#include <Geometry.H>

#include <Problem.H>
#include <Problem_F.H>

#include <Gravity.H>
#include <Gravity_F.H>

#include "buildInfo.H"

void
Castro::sum_integrated_quantities ()
{
    if (level > 0) return;

    bool local_flag = true;

    int finest_level  = parent->finestLevel();
    Real time         = state[State_Type].curTime();
    Real dt           = parent->dtLevel(0);

    if (time == 0.0) dt = 0.0; // dtLevel returns the next timestep for t = 0, so overwrite

    int timestep = parent->levelSteps(0);

    Real mass                 = 0.0;
    Real momentum[3]          = { 0.0 };
    Real angular_momentum[3]  = { 0.0 };
    Real hybrid_momentum[3]   = { 0.0 };
    Real rho_E                = 0.0;
    Real rho_e                = 0.0;
    Real rho_K                = 0.0;
    Real rho_phi              = 0.0;
    Real rho_phirot           = 0.0;

    // Total energy on the grid, including decomposition
    // into the various components.

    Real gravitational_energy = 0.0;
    Real kinetic_energy       = 0.0;
    Real gas_energy           = 0.0;
    Real rotational_energy    = 0.0;
    Real internal_energy      = 0.0;
    Real total_energy         = 0.0;
    Real total_E_grid         = 0.0;

    // Rotation frequency.

    Real omega[3] = { 0.0 };

    get_omega_vec(omega, time);

    // Mass transfer rate

    Real mdot = 0.5 * (std::abs(mdot_p) + std::abs(mdot_s));

    // Center of mass of the system.

    Real com[3]       = { 0.0 };
    Real com_vel[3]   = { 0.0 };

    // Stellar masses.

    Real mass_p       = 0.0;
    Real mass_s       = 0.0;

    // Distance between the WDs.

    Real wd_dist[3] = { 0.0 };
    Real wd_dist_init[3] = { 0.0 };

    Real separation = 0.0;
    Real angle = 0.0;

    // Stellar centers of mass and velocities.

    Real com_p_mag = 0.0;
    Real com_s_mag = 0.0;

    Real com_p[3]     = { 0.0 };
    Real com_s[3]     = { 0.0 };

    Real vel_p_mag = 0.0;
    Real vel_s_mag = 0.0;

    Real vel_p[3] = { 0.0 };
    Real vel_s[3] = { 0.0 };

    Real vel_p_rad = 0.0;
    Real vel_s_rad = 0.0;

    Real vel_p_phi = 0.0;
    Real vel_s_phi = 0.0;

    // Gravitational free-fall timescale of the stars.
    
    Real t_ff_p = 0.0;
    Real t_ff_s = 0.0;

    // Gravitational wave amplitudes.
    
    Real h_plus_1  = 0.0;
    Real h_cross_1 = 0.0;

    Real h_plus_2  = 0.0;
    Real h_cross_2 = 0.0;

    Real h_plus_3  = 0.0;
    Real h_cross_3 = 0.0;

    // Number of species.
    
    int NumSpec;
    get_num_spec(&NumSpec);    

    // Species names and total masses on the domain.

    Real M_solar = 1.9884e33;
    
    Real species_mass[NumSpec];
    std::vector<std::string> species_names(NumSpec);
    
    std::string name1; 
    std::string name2;

    int index1;
    int index2;

    int dataprecision = 16; // Number of digits after the decimal point, for float data

    int datwidth      = 25; // Floating point data in scientific notation
    int fixwidth      = 25; // Floating point data not in scientific notation
    int intwidth      = 12; // Integer data

    int axis_1;
    int axis_2;
    int axis_3;

    // Determine various coordinate axes
    get_axes(axis_1, axis_2, axis_3);

    wd_dist_init[axis_1 - 1] = 1.0;

    // Determine the names of the species in the simulation.    

    for (int i = 0; i < NumSpec; i++) {
      species_names[i] = desc_lst[State_Type].name(FirstSpec+i);
      species_names[i] = species_names[i].substr(4,std::string::npos);
      species_mass[i]  = 0.0;	
    }

    for (int lev = 0; lev <= finest_level; lev++)
    {

      // Update the local level we're on.
      
      set_amr_info(lev, -1, -1, -1.0, -1.0);
      
      // Get the current level from Castro

      Castro& ca_lev = getLevel(lev);

      for ( int i = 0; i < 3; i++ ) {
        com[i] += ca_lev.locWgtSum("density", time, i, local_flag);
      }

      // Calculate total mass, momentum, angular momentum, and energy of system.

      mass += ca_lev.volWgtSum("density", time, local_flag);

      momentum[0] += ca_lev.volWgtSum("inertial_momentum_x", time, local_flag);
      momentum[1] += ca_lev.volWgtSum("inertial_momentum_y", time, local_flag);
      momentum[2] += ca_lev.volWgtSum("inertial_momentum_z", time, local_flag);

      angular_momentum[0] += ca_lev.volWgtSum("inertial_angular_momentum_x", time, local_flag);
      angular_momentum[1] += ca_lev.volWgtSum("inertial_angular_momentum_y", time, local_flag);
      angular_momentum[2] += ca_lev.volWgtSum("inertial_angular_momentum_z", time, local_flag);

#ifdef HYBRID_MOMENTUM
      hybrid_momentum[0] += ca_lev.volWgtSum("rmom", time, local_flag);
      hybrid_momentum[1] += ca_lev.volWgtSum("lmom", time, local_flag);
      hybrid_momentum[2] += ca_lev.volWgtSum("pmom", time, local_flag);
#endif

      rho_E += ca_lev.volWgtSum("rho_E", time, local_flag);
      rho_K += ca_lev.volWgtSum("kineng",time, local_flag);
      rho_e += ca_lev.volWgtSum("rho_e", time, local_flag);

#ifdef GRAVITY
      if (do_grav)
        rho_phi += ca_lev.volProductSum("density", "phiGrav", time, local_flag);
#endif

#ifdef ROTATION
      if (do_rotation)
	rho_phirot += ca_lev.volProductSum("density", "phiRot", time, local_flag);
#endif            
      
      // Gravitational wave signal. This is designed to add to these quantities so we can send them directly.
      ca_lev.gwstrain(time, h_plus_1, h_cross_1, h_plus_2, h_cross_2, h_plus_3, h_cross_3, local_flag);

      // Integrated mass of all species on the domain.      
      for (int i = 0; i < NumSpec; i++)
	species_mass[i] += ca_lev.volWgtSum("rho_" + species_names[i], time, local_flag) / M_solar;

      MultiFab& S_new = ca_lev.get_new_data(State_Type);

    }

    // Return to the original level.
    
    set_amr_info(level, -1, -1, -1.0, -1.0);    

    // Do the reductions.

    int nfoo_sum = 24 + NumSpec;

    Array<Real> foo_sum(nfoo_sum);

    foo_sum[0] = mass;

    for (int i = 0; i < 3; i++) {
      foo_sum[i+1]  = com[i];
      foo_sum[i+4]  = momentum[i];
      foo_sum[i+7]  = angular_momentum[i];
      foo_sum[i+10] = hybrid_momentum[i];
    }
    
    foo_sum[13] = rho_E;
    foo_sum[14] = rho_K;
    foo_sum[15] = rho_e;
    foo_sum[16] = rho_phi;
    foo_sum[17] = rho_phirot;
    foo_sum[18] = h_plus_1;
    foo_sum[19] = h_cross_1;
    foo_sum[20] = h_plus_2;
    foo_sum[21] = h_cross_2;
    foo_sum[22] = h_plus_3;
    foo_sum[23] = h_cross_3;

    for (int i = 0; i < NumSpec; i++) {
      foo_sum[i + 24] = species_mass[i];
    }

    ParallelDescriptor::ReduceRealSum(foo_sum.dataPtr(), nfoo_sum);

    mass = foo_sum[0];

    for (int i = 0; i < 3; i++) {
      com[i]              = foo_sum[i+1];
      momentum[i]         = foo_sum[i+4];
      angular_momentum[i] = foo_sum[i+7];
      hybrid_momentum[i]  = foo_sum[i+10];
    }

    rho_E      = foo_sum[13];
    rho_K      = foo_sum[14];
    rho_e      = foo_sum[15];
    rho_phi    = foo_sum[16];
    rho_phirot = foo_sum[17];
    h_plus_1   = foo_sum[18];
    h_cross_1  = foo_sum[19];
    h_plus_2   = foo_sum[20];
    h_cross_2  = foo_sum[21];
    h_plus_3   = foo_sum[22];
    h_cross_3  = foo_sum[23];

    for (int i = 0; i < NumSpec; i++) {
      species_mass[i] = foo_sum[i + 24];
    }

    // Complete calculations for energy and momenta

    gravitational_energy = -rho_phi; // CASTRO uses positive phi
    if (gravity->get_gravity_type() == "PoissonGrav")
      gravitational_energy *= 0.5; // avoids double counting
    internal_energy = rho_e;
    kinetic_energy = rho_K;
    gas_energy = rho_E;
    rotational_energy = rho_phirot;
    total_E_grid = gravitational_energy + rho_E;
    total_energy = total_E_grid + rotational_energy;
    
    // Complete calculations for center of mass quantities

    for ( int i = 0; i < 3; i++ ) {

      com[i]       = com[i] / mass;
      com_vel[i]   = momentum[i] / mass;

    }

    get_star_data(com_p, com_s, vel_p, vel_s, &mass_p, &mass_s, &t_ff_p, &t_ff_s);

    com_p_mag += std::pow( std::pow(com_p[0],2) + std::pow(com_p[1],2) + std::pow(com_p[2],2), 0.5 );
    com_s_mag += std::pow( std::pow(com_s[0],2) + std::pow(com_s[1],2) + std::pow(com_s[2],2), 0.5 );
    vel_p_mag += std::pow( std::pow(vel_p[0],2) + std::pow(vel_p[1],2) + std::pow(vel_p[2],2), 0.5 );
    vel_s_mag += std::pow( std::pow(vel_s[0],2) + std::pow(vel_s[1],2) + std::pow(vel_s[2],2), 0.5 );

#if (BL_SPACEDIM == 3)
    if (mass_p > 0.0) {
      vel_p_rad = (com_p[axis_1 - 1] / com_p_mag) * vel_p[axis_1 - 1] + (com_p[axis_2 - 1] / com_p_mag) * vel_p[axis_2 - 1];
      vel_p_phi = (com_p[axis_1 - 1] / com_p_mag) * vel_p[axis_2 - 1] - (com_p[axis_2 - 1] / com_p_mag) * vel_p[axis_1 - 1];
    }

    if (mass_s > 0.0) {
      vel_s_rad = (com_s[axis_1 - 1] / com_s_mag) * vel_s[axis_1 - 1] + (com_s[axis_2 - 1] / com_s_mag) * vel_s[axis_2 - 1];
      vel_s_phi = (com_s[axis_1 - 1] / com_s_mag) * vel_s[axis_2 - 1] - (com_s[axis_2 - 1] / com_s_mag) * vel_s[axis_1 - 1];
    }
#else
    if (mass_p > 0.0) {
      vel_p_rad = vel_p[axis_1 - 1];
      vel_p_phi = vel_p[axis_3 - 1];
    }

    if (mass_s > 0.0) {
      vel_s_rad = vel_s[axis_1 - 1];
      vel_s_phi = vel_s[axis_3 - 1];
    }
#endif

    if (mass_p > 0.0 && mass_s > 0.0) {
      
      // Calculate the distance between the primary and secondary.

      for ( int i = 0; i < 3; i++ ) 
	wd_dist[i] = com_s[i] - com_p[i];
    
      separation = norm(wd_dist);

      // Calculate the angle between the initial stellar axis and
      // the line currently joining the two stars. Note that this
      // neglects any motion in the plane perpendicular to the initial orbit.

      angle = atan2( wd_dist[axis_2 - 1] - wd_dist_init[axis_2 - 1],
                     wd_dist[axis_1 - 1] - wd_dist_init[axis_1 - 1] ) * 180.0 / M_PI;

      // Now let's transform from [-180, 180] to [0, 360].
      
      if (angle < 0.0) angle += 360.0;
      
    }

    // Write data out to the log.

    if ( ParallelDescriptor::IOProcessor() )
    {

      // The data logs are only defined on the IO processor
      // for parallel runs, so the stream should only be opened inside.

      if (parent->NumDataLogs() > 0) {

	 std::ostream& log = parent->DataLog(0);

	 if ( log.good() ) {

	   // Write header row

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(datwidth) << "             TOTAL ENERGY";
	     log << std::setw(datwidth) << "             TOTAL E GRID";
	     log << std::setw(datwidth) << "               GAS ENERGY";
	     log << std::setw(datwidth) << "              KIN. ENERGY";
	     log << std::setw(datwidth) << "              ROT. ENERGY";
	     log << std::setw(datwidth) << "             GRAV. ENERGY";
	     log << std::setw(datwidth) << "              INT. ENERGY";
#if (BL_SPACEDIM == 3)
	     log << std::setw(datwidth) << "                     XMOM";
	     log << std::setw(datwidth) << "                     YMOM";
	     log << std::setw(datwidth) << "                     ZMOM";
#ifdef HYBRID_MOMENTUM
	     log << std::setw(datwidth) << "              HYB. MOM. R";
	     log << std::setw(datwidth) << "              HYB. MOM. L";
	     log << std::setw(datwidth) << "              HYB. MOM. P";
#endif
	     log << std::setw(datwidth) << "              ANG. MOM. X";
	     log << std::setw(datwidth) << "              ANG. MOM. Y";
	     log << std::setw(datwidth) << "              ANG. MOM. Z";
#else
	     log << std::setw(datwidth) << "                     RMOM";
	     log << std::setw(datwidth) << "                     ZMOM";
	     log << std::setw(datwidth) << "              ANG. MOM. R";
	     log << std::setw(datwidth) << "              ANG. MOM. Z";
#endif
	     log << std::setw(datwidth) << "                     MASS";
#if (BL_SPACEDIM == 3 )
	     log << std::setw(datwidth) << "                    X COM";
	     log << std::setw(datwidth) << "                    Y COM";
	     log << std::setw(datwidth) << "                    Z COM";
	     log << std::setw(datwidth) << "                X COM VEL";
	     log << std::setw(datwidth) << "                Y COM VEL";
	     log << std::setw(datwidth) << "                Z COM VEL";
#else
	     log << std::setw(datwidth) << "                R COM    ";
	     log << std::setw(datwidth) << "                Z COM    ";
	     log << std::setw(datwidth) << "                R COM VEL";
	     log << std::setw(datwidth) << "                Z COM VEL";
#endif
	     log << std::setw(datwidth) << "                    T MAX";
	     log << std::setw(datwidth) << "                  RHO MAX";
	     log << std::setw(datwidth) << "            T_S / T_E MAX";
	     log << std::setw(datwidth) << "             h_+ (axis 1)";
	     log << std::setw(datwidth) << "             h_x (axis 1)";
	     log << std::setw(datwidth) << "             h_+ (axis 2)";
	     log << std::setw(datwidth) << "             h_x (axis 2)";
	     log << std::setw(datwidth) << "             h_+ (axis 3)";
	     log << std::setw(datwidth) << "             h_x (axis 3)";

	     log << std::endl;
	   }

	   // Write data for the present time

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   log << std::setw(datwidth) << std::setprecision(dataprecision) << total_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << total_E_grid;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << gas_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << kinetic_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << rotational_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << gravitational_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << internal_energy;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << momentum[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << momentum[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << momentum[2];
#endif
#ifdef HYBRID_MOMENTUM
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << hybrid_momentum[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << hybrid_momentum[1];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << hybrid_momentum[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << angular_momentum[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << angular_momentum[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << angular_momentum[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mass;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_vel[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_vel[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_vel[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_plus_1;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_cross_1;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_plus_2;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_cross_2;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_plus_3;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << h_cross_3;

	   log << std::endl;
	 }
      }

      if (parent->NumDataLogs() > 1) {

	 std::ostream& log = parent->DataLog(1);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";

	     log << std::setw(datwidth) << "              WD DISTANCE";
	     log << std::setw(fixwidth) << "                 WD ANGLE";
	     log << std::setw(datwidth) << "                     MDOT";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << separation;

	   log << std::fixed;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << angle;

	   log << std::scientific;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mdot;

	   log << std::endl;

	 }
      }

      // Species

      if (parent->NumDataLogs() > 2) {

	 std::ostream& log = parent->DataLog(2);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                  TIME";

	     // We need to be careful here since the species names have differing numbers of characters

	     for (int i = 0; i < NumSpec; i++) {
	       std::string outString  = "";
	       std::string massString = "Mass ";
	       std::string specString = species_names[i];
               while (outString.length() + specString.length() + massString.length() < datwidth) outString += " ";
	       outString += massString;
	       outString += specString;
	       log << std::setw(datwidth) << outString;
	     }

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   for (int i = 0; i < NumSpec; i++)
	     log << std::setw(datwidth) << std::setprecision(dataprecision) << species_mass[i];

	   log << std::endl;

	 }
      }

      if (parent->NumDataLogs() > 3) {

	 std::ostream& log = parent->DataLog(3);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(fixwidth) << "                       DT";
	     log << std::setw(intwidth) << "  FINEST LEV";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << dt;
	   log << std::setw(intwidth)                                     << parent->finestLevel();

	   log << std::endl;

	 }

      }

      // Material lost through domain boundaries

      if (parent->NumDataLogs() > 4) {

	 std::ostream& log = parent->DataLog(4);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(datwidth) << "                MASS LOST";
	     log << std::setw(datwidth) << "                XMOM LOST";
	     log << std::setw(datwidth) << "                YMOM LOST";
	     log << std::setw(datwidth) << "                ZMOM LOST";
	     log << std::setw(datwidth) << "                EDEN LOST";
	     log << std::setw(datwidth) << "         ANG. MOM. X LOST";
	     log << std::setw(datwidth) << "         ANG. MOM. Y LOST";
	     log << std::setw(datwidth) << "         ANG. MOM. Z LOST";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   for (int i = 0; i < n_lost; i++)
	     log << std::setw(datwidth) << std::setprecision(dataprecision) << material_lost_through_boundary_cumulative[i];

	   log << std::endl;

	 }

      }

      // Primary star

      if (parent->NumDataLogs() > 5) {

	std::ostream& log = parent->DataLog(5);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(datwidth) << "             PRIMARY MASS";
	     log << std::setw(datwidth) << "             PRIMARY MDOT";
	     log << std::setw(datwidth) << "          PRIMARY MAG COM";
#if (BL_SPACEDIM == 3)
	     log << std::setw(datwidth) << "            PRIMARY X COM";
	     log << std::setw(datwidth) << "            PRIMARY Y COM";
	     log << std::setw(datwidth) << "            PRIMARY Z COM";
#else
	     log << std::setw(datwidth) << "            PRIMARY R COM";
	     log << std::setw(datwidth) << "            PRIMARY Z COM";
#endif
	     log << std::setw(datwidth) << "          PRIMARY MAG VEL";
	     log << std::setw(datwidth) << "          PRIMARY RAD VEL";
	     log << std::setw(datwidth) << "          PRIMARY ANG VEL";
#if (BL_SPACEDIM == 3)
	     log << std::setw(datwidth) << "            PRIMARY X VEL";
	     log << std::setw(datwidth) << "            PRIMARY Y VEL";
	     log << std::setw(datwidth) << "            PRIMARY Z VEL";
#else
	     log << std::setw(datwidth) << "            PRIMARY R VEL";
	     log << std::setw(datwidth) << "            PRIMARY Z VEL";
#endif
	     log << std::setw(datwidth) << "       PRIMARY T_FREEFALL";
	     for (int i = 0; i <= 6; ++i)
	       log << "       PRIMARY 1E" << i << " RADIUS";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mass_p;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mdot_p;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_p_mag;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_p[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_p[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_p[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p_mag;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p_rad;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p_phi;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_p[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << t_ff_p;
	   for (int i = 0; i <= 6; ++i)
	       log << std::setw(datwidth) << std::setprecision(dataprecision) << rad_p[i];

	   log << std::endl;

	 }

      }

      // Secondary star

      if (parent->NumDataLogs() > 6) {

	std::ostream& log = parent->DataLog(6);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(datwidth) << "           SECONDARY MASS";
	     log << std::setw(datwidth) << "           SECONDARY MDOT";
	     log << std::setw(datwidth) << "        SECONDARY MAG COM";
#if (BL_SPACEDIM == 3)
	     log << std::setw(datwidth) << "          SECONDARY X COM";
	     log << std::setw(datwidth) << "          SECONDARY Y COM";
	     log << std::setw(datwidth) << "          SECONDARY Z COM";
#else
	     log << std::setw(datwidth) << "          SECONDARY R COM";
	     log << std::setw(datwidth) << "          SECONDARY Z COM";
#endif
	     log << std::setw(datwidth) << "        SECONDARY MAG VEL";
	     log << std::setw(datwidth) << "        SECONDARY RAD VEL";
	     log << std::setw(datwidth) << "        SECONDARY ANG VEL";
#if (BL_SPACEDIM == 3)
	     log << std::setw(datwidth) << "          SECONDARY X VEL";
	     log << std::setw(datwidth) << "          SECONDARY Y VEL";
	     log << std::setw(datwidth) << "          SECONDARY Z VEL";
#else
	     log << std::setw(datwidth) << "          SECONDARY R VEL";
	     log << std::setw(datwidth) << "          SECONDARY Z VEL";
#endif
	     log << std::setw(datwidth) << "     SECONDARY T_FREEFALL";
	     for (int i = 0; i <= 6; ++i)
	       log << "     SECONDARY 1E" << i << " RADIUS";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mass_s;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << mdot_s;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_s_mag;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_s[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_s[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << com_s[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s_mag;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s_rad;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s_phi;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s[0];
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s[1];
#if (BL_SPACEDIM == 3)
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << vel_s[2];
#endif
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << t_ff_s;
	   for (int i = 0; i <= 6; ++i)
	       log << std::setw(datwidth) << std::setprecision(dataprecision) << rad_s[i];

	   log << std::endl;

	 }

      }

      // Extrema over time of various quantities

      if (parent->NumDataLogs() > 7) {

	 std::ostream& log = parent->DataLog(7);

	 if ( log.good() ) {

	   if (time == 0.0) {

	     // Output the git commit hashes used to build the executable.

	     const char* castro_hash   = buildInfoGetGitHash(1);
	     const char* boxlib_hash   = buildInfoGetGitHash(2);
	     const char* wdmerger_hash = buildInfoGetBuildGitHash();

	     log << "# Castro   git hash: " << castro_hash   << std::endl;
	     log << "# BoxLib   git hash: " << boxlib_hash   << std::endl;
	     log << "# wdmerger git hash: " << wdmerger_hash << std::endl;

	     log << std::setw(intwidth) << "#   TIMESTEP";
	     log << std::setw(fixwidth) << "                     TIME";
	     log << std::setw(datwidth) << "               MAX T CURR";
	     log << std::setw(datwidth) << "             MAX RHO CURR";
	     log << std::setw(datwidth) << "           MAX TS_TE CURR";
	     log << std::setw(datwidth) << "           MAX T ALL TIME";
	     log << std::setw(datwidth) << "         MAX RHO ALL TIME";
	     log << std::setw(datwidth) << "       MAX TS_TE ALL TIME";

	     log << std::endl;

	   }

	   log << std::fixed;

	   log << std::setw(intwidth)                                     << timestep;
	   log << std::setw(fixwidth) << std::setprecision(dataprecision) << time;

	   log << std::scientific;

	   log << std::setw(datwidth) << std::setprecision(dataprecision) << T_curr_max;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << rho_curr_max;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << ts_te_curr_max;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << T_global_max;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << rho_global_max;
	   log << std::setw(datwidth) << std::setprecision(dataprecision) << ts_te_global_max;

	   log << std::endl;

	 }

      }

    }
}

