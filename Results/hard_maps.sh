#!/bin/bash
#SBATCH --job-name=PDP_MPI
#SBATCH --output="mpi_out_%J.out"
#SBATCH --error="mpi_err_%J.err"
#SBATCH --partition=arm_long
#SBATCH --nodes=4
#SBATCH --exclusive

source /etc/profile.d/zz-cray-pe.sh
module load cray-mvapich2_pmix_nogpu

rm -f solverMPI
CC -O3 -fopenmp solverMPI.cpp -o solverMPI

CSV="vysledky_MPI.csv"

if [ ! -f "$CSV" ]; then
    echo "mapa,verze,uzly,vlakna,cas" > "$CSV"
fi

NODES="1 2 3 4"

echo "Start mereni MPI"

map="mapa_hard/mapa7_12_hard.txt"

if [ ! -f "$map" ]; then 
    echo "Chybi mapa $map"
    exit 1
fi
    
m=$(basename "$map")
echo "Mapa: $m"

export OMP_NUM_THREADS=48
export MV2_ENABLE_AFFINITY=0
export OMP_WAIT_POLICY=PASSIVE
export OMP_PROC_BIND=false
export MV2_HOMOGENEOUS_CLUSTER=1
export MV2_SUPPRESS_JOB_STARTUP_PERFORMANCE_WARNING

for n in $NODES; do
    cas=$(srun --quiet -N $n -n $n -c 48 ./solverMPI "$map")
    echo "MPI $n uzlu: $cas"
    echo "$m,MPI,$n,48,$cas" >> "$CSV"
done

echo "Konec mereni"