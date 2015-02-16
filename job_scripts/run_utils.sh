# run_utils.sh: helper functions for job submission scripts.

# This uses the functionality built into the CASTRO makefile setup,
# where make print-$VAR finds the variable VAR in the makefile
# variable list and prints it out to stdout. It is the last word
# on the last line of the make output.

function get_wdmerger_make_var {

    make print-$1 -C $compile_dir | tail -2 | head -1 | awk '{ print $NF }'

}

function get_castro_make_var {

    make print-$1 -f $local_makefile -C $compile_dir  | tail -2 | head -1 | awk '{ print $NF }'

}



# Return the name of the machine we're currently working on.

function get_machine {

  UNAMEN=$(uname -n)

  # Blue Waters doesn't retain the h2o in the name when running on
  # a compute node, so as a hack we'll go with the nid.* signifier.
  # This may cause conflicts with other systems, so we should 
  # replace this at some point with a better system.

  if   [[ $UNAMEN == *"h2o"*   ]] || [[ $UNAMEN == *"nid"* ]]; then
    MACHINE=BLUE_WATERS
  elif [[ $UNAMEN == *"titan"* ]]; then
    MACHINE=TITAN
  fi

  echo $MACHINE

}



# Given a directory as the first argument, return the numerically last checkpoint file.

function get_last_checkpoint {

    # Doing a search this way will treat first any checkpoint files 
    # with six digits, and then will fall back to ones with five digits.
    # On recent versions of GNU sort, one can simplify this with sort -V.

    # Note: if we do not hand this an argument, assume we want to look in the current directory.

    if [ ! -z $1 ]; then
	dir='.'
    else
	dir=$1
    fi

    checkpoint=$(find $dir -type d -name "*chk??????" -print | sort | tail -1)

    # The Header is the last thing written -- check if it's there, otherwise,
    # fall back to the second-to-last check file written, because it means 
    # the latest checkpoint file is still being written.

    if [ ! -f ${checkpoint}/Header ]; then

	# How many *chk?????? files are there? if only one, then skip,
	# as there are no valid and complete checkpoint files.

	nl=$(find $dir -type d -name "*chk??????" -print | sort | wc -l)
	if [ $nl -gt 1 ]; then
	    checkpoint=$(find $dir -type d -name "*chk??????" -print | sort | tail -2 | head -1)    
	else
	    checkpoint=""
	fi

    fi

    echo $checkpoint

}



# Return a string that is used for restarting from the latest checkpoint.
# Optionally you can hand this a directory, otherwise it will default 
# to whatever get_last_checkpoint determines.

function get_restart_string {

    if [ ! -z $1 ]; then
	checkpoint=$(get_last_checkpoint)
    else
	checkpoint=$(get_last_checkpoint $1)
    fi

    # restartString will be empty if no chk files are found -- i.e. this is a new run.

    if [ ! -n "$checkpoint" ]; then
	restartString=""
    else
	restartString="amr.restart="$checkpoint
    fi

    echo $restartString

}



# Achive the file given in the first argument, to the same path
# on the archive machine relative to the machine's $workdir.

function archive {

  if [ ! -z $1 ]; then
      echo "Archiving file " $1"."
  else
      echo "No file to archive; exiting."
      return
  fi

  # We may get a directory to archive, so call basename to make sure $file
  # doesn't appear with a trailing slash.

  file=$(basename $1)
  dir=$(dirname $1)

  storage_dir=$(echo ${dir#$workdir})

  # Move the file into the output directory to signify that we've archived it.

  mv ${file} $dir/output/

  # Determine archiving tool based on machine.

  if   [ $MACHINE == "TITAN"       ]; then

      htar -H copies=2 -Pcvf ${storage_dir}/${file}.tar $dir/$file

  elif [ $MACHINE == "BLUE_WATERS" ]; then

      archive_dir=/projects/sciteam/$allocation/$USER/$storage_dir

      cwd=$(pwd)

      src=$globus_src_endpoint$dir/output/$file
      dst=$globus_dst_endpoint$archive_dir/output/$file

      if [ -d $dir/output/$file ]; then
          # If we're transferring a directory, Globus needs to explicitly know
          # that it is recursive, and needs to have trailing slashes.
          ssh $globus_username@$globus_hostname transfer -- $src/ $dst/ -r
      else
	  # We're copying a normal file.
	  ssh $globus_username@$globus_hostname transfer -- $src $dst
      fi

  fi

}



# Archive all the output files in the directory given in the first argument.
# The directory must be an absolute path.

function archive_all {

  if [ ! -z $1 ]; then
      dir=$1
  else
      echo "No directory passed to function archive_all; exiting."
      return
  fi

  # Archive the plotfiles and checkpoint files.

  pltlist=$(find $dir -maxdepth 1 -type d -name "*plt*" | sort)
  chklist=$(find $dir -maxdepth 1 -type d -name "*chk*" | sort)

  for file in $pltlist
  do
      archive $file
  done

  for file in $chklist
  do
      archive $file
  done

  diaglist=$(find $dir -maxdepth 1 -name "*diag*.out")

  # For the diagnostic files, we want to make a copy with the current date
  # before archiving it, since the diagnostic files need to remain there
  # for the duration of the simulation.

  datestr=$(date +"%Y%m%d_%H%M_%S_")

  for file in $diaglist
  do
      filebasename=$(basename $file)
      archivefile=$datestr$filebasename
      cp $file $dir/$archivefile
      archive $dir/$archivefile
  done

}



# Copies all relevant files needed for a CASTRO run into the target directory.

function copy_files {

    cp $compile_dir/$CASTRO $1
    if [ -e "$compile_dir/helm_table.dat" ]; then
	cp $compile_dir/helm_table.dat $1
    fi
    cp $compile_dir/$inputs $1
    cp $compile_dir/$probin $1
    cp $compile_dir/$job_script $1

}



# Main submission script. Checks which Linux variant we're on,
# and uses the relevant batch submission script. If you want to
# use a different machine, you'll need to include a run script
# for it in the job_scripts directory.
# The first argument is the name of the directory where we want to 
# submit this job from.
# The second argument is the number of processors you want to run the job on.
# The third argument is the walltime you want the job to run for.
# The last two are optional and default to one processor running for one hour.

function run {

  dir=$1
  if [ ! -z $2 ]; then
      nprocs=$2
  else
      nprocs=1
  fi
  if [ ! -z $3 ]; then
      walltime=$3
  else
      walltime=1:00:00
  fi

  if [ ! -d $dir ]; then

    echo "Submitting job in directory "$dir"."

    mkdir -p $dir

    nodes=$(expr $nprocs / $ppn)

    # If the number of processors is less than the number of processors per node,
    # there are scaling tests where this is necessary; we'll assume the user understands
    # what they are doing and set it up accordingly.

    old_ppn=$ppn

    if [ $nodes -eq 0 ]; then
	nodes="1"
	ppn=$nprocs
    fi

    if [ $MACHINE == "GENERICLINUX" ] ; then 
	echo "echo \"mpiexec -n $nprocs $CASTRO $inputs > info.out\" | batch" > $compile_dir/$job_script
    elif [ $MACHINE == "BLUE_WATERS" ]; then
	sed -i "/#PBS -l nodes/c #PBS -l nodes=$nodes:ppn=$ppn:xe" $compile_dir/$job_script
	sed -i "/#PBS -l walltime/c #PBS -l walltime=$walltime" $compile_dir/$job_script
	sed -i "/aprun/c aprun -n $nprocs -N $ppn $CASTRO $inputs \$\{restartString\}" $compile_dir/$job_script
    elif [ $MACHINE == "TITAN" ]; then
	sed -i "/#PBS -l nodes/c #PBS -l nodes=$nodes" $compile_dir/$job_script
	sed -i "/#PBS -l walltime/c #PBS -l walltime=$walltime" $compile_dir/$job_script
	sed -i "/aprun/c aprun -n $nprocs -N $ppn -j 1 $CASTRO $inputs \$\{restartString\}" $compile_dir/$job_script
    fi

    # Change into the run directory, submit the job, then come back to the main directory.

    copy_files $dir
    cd $dir
    $exec $job_script
    cd - > /dev/null

    # Restore the number of processors per node in case we changed it.

    ppn=$old_ppn

  else

    # If the directory already exists, check to see if we've reached the desired stopping point.

    checkpoint=$(get_last_checkpoint)

    # Extract the checkpoint time. It is stored in row 3 of the Header file.

    time=$(awk 'NR==3' $checkpoint/Header)

    # Extract the current timestep. It is stored in row 12 of the Header file.

    step=$(awk 'NR==12' $checkpoint/Header)

    # Now determine if we are both under max_step and stop_time. If so, re-submit the job.
    # The job script already knows to start from the latest checkpoint file.

    stop_time=$(grep "stop_time" $dir/$inputs | awk '{print $3}')
    max_step=$(grep "max_step" $dir/$inputs | awk '{print $3}')

    time_flag=$(echo "$time < $stop_time" | bc -l)
    step_flag=$(echo "$step < $max_step" | bc -l)

    # First as a sanity check, make sure the desired job isn't already running.

    if [ -e $dir/*$run_ext ]; then

	echo "Job currently in process in directory "$dir"."

    else
 
	if [ $time_flag -eq 1 ] && [ $step_flag -eq 1 ]; then

	echo "Continuing job in directory "$dir"."

	cd $dir
	$exec $job_script
	cd - > /dev/null

	# If we make it here, then we've already reached either stop_time
	# or max_step, so we should conclude that the run is done.

	else
	    echo "Job has already been completed in directory "$dir"."
	fi
    fi

  fi

}



########################################################################

# Define variables

# Get current machine and set preferences accordingly.
# Note: workdir is the name of the directory you submit 
# jobs from (usually scratch directories).

MACHINE=$(get_machine)

if [ $MACHINE == "GENERICLINUX" ]; then

    exec="bash"
    job_script="linux.run"
    ppn="16"

elif [ $MACHINE == "BLUE_WATERS" ]; then

    allocation="jni"
    exec="qsub"
    job_script="bluewaters.run"
    COMP="Cray"
    FCOMP="Cray"
    ppn="16"
    run_ext=".OU"
    workdir="/scratch/sciteam/$USER"
    globus=T
    globus_src_endpoint="ncsa#BlueWaters"
    globus_dst_endpoint="ncsa#Nearline"

elif [ $MACHINE == "TITAN" ]; then

    allocation="ast106"
    exec="qsub"
    job_script="titan.run"
    COMP="Cray"
    FCOMP="Cray"
    ppn="8"
    run_ext=".OU"
    workdir="/lustre/atlas/scratch/$USER/$allocation"

fi

# If we're using Globus Online, set some useful parameters.
if [ $globus == T ]; then
    globus_username="mkatz"
    globus_hostname="cli.globusonline.org"
fi

# Directory to compile the executable in

compile_dir="compile"

# Upon initialization, store some variables and create results directory.
# We only want to initialize these variables if we're currently in a root problem directory.

if [ -d $compile_dir ]; then

    if [ -e $compile_dir/makefile ]; then

	local_makefile=$(get_wdmerger_make_var local_makefile)
	inputs=$(get_wdmerger_make_var inputs)
	probin=$(get_wdmerger_make_var probin)
	CASTRO=$(get_castro_make_var executable)

    fi

    # This returns the Linux variant the current machine uses, defined in
    # $(BOXLIB_HOME)/Tools/C_mk/Make.defs.
    # This is used to select which batch submission script we want to use.

    if [ ! -e $compile_dir/$job_script ]; then
	cp $WDMERGER_HOME/job_scripts/$job_script $compile_dir
    fi

    # Directory for executing and storing results

    results_dir="results"

    if [ ! -d $results_dir ]; then
      mkdir $results_dir
    fi

    # Directory for placing plots from analysis routines

    plots_dir="plots"

    if [ ! -d $plots_dir ]; then
	mkdir $plots_dir
    fi

fi
