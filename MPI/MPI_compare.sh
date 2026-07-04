#!/bin/bash
#SBATCH --job-name=PDP_Mereni
#SBATCH --output="%x-%J.out"
#SBATCH --error="%x-%J.err"
#SBATCH --partition=arm_long
#SBATCH --nodes=4
#SBATCH --exclusive

source /etc/profile.d/zz-cray-pe.sh
export MV2_HOMOGENEOUS_CLUSTER=1
export MV2_SUPPRESS_JOB_STARTUP_PERFORMANCE_WARNING=1
export MV2_ENABLE_AFFINITY=0
export MV2_USE_ALIGNED_ALLOC=1
export MV2_USE_THREAD_WARNING=0
module load cray-mvapich2_pmix_nogpu

CC solver.cpp -o solver -O3
CC solverMP.cpp -o solverMP -fopenmp -O3
CC solverData.cpp -o solverData -fopenmp -O3
CC solverMPI.cpp -o solverMPI -fopenmp -O3

MAPS="mapa/mapa9_9.txt"
MPI_NODES="1 2 3 4"

for map in $MAPS; do
    echo "MAPA: $map"
    
    echo "Sekvencni verze:"
    seq_time=$(srun --quiet -N 1 -n 1 -c 1 ./solver "$map")
    echo "Cas: $seq_time s"
    echo ""

    echo "OpenMP (48 jader)"
    export OMP_NUM_THREADS=48
    task_time=$(srun --quiet -N 1 -n 1 -c 48 ./solverMP "$map" 8)
    data_time=$(srun --quiet -N 1 -n 1 -c 48 ./solverData "$map" 5000)
    echo "Task: $task_time s"
    echo "Data: $data_time s"
    echo ""

    echo "MPI + OpenMP Datovy paralelismus"
    echo "Uzly (x 48 jader) | Cas [s]"
    echo "----------------------------"
    for n in $MPI_NODES; do
        export OMP_NUM_THREADS=48
        mpi_time=$(srun --quiet -N $n -n $n -c 48 ./solverMPI "$map")
        echo "$n | $mpi_time"
    done
    echo ""
done

exit 0