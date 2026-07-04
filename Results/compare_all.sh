#!/bin/bash
#SBATCH --job-name=PDP_Finalni
#SBATCH --output="final_out_%J.out"
#SBATCH --error="final_err_%J.err"
#SBATCH --partition=arm_long
#SBATCH --nodes=4
#SBATCH --exclusive

source /etc/profile.d/zz-cray-pe.sh
module load cray-mvapich2_pmix_nogpu

rm -f solverMP solverData solverMPI

CC -O3 -fopenmp solverMP.cpp -o solverMP
CC -O3 -fopenmp solverData.cpp -o solverData
CC -O3 -fopenmp solverMPI.cpp -o solverMPI

CSV="vysledky_final.csv"
echo "mapa,verze,uzly,vlakna,cas" > $CSV

THREADS="2 4 8 12 16 32 48"
NODES="1 2 3 4"

echo "Start mereni"

for map in mapa/*.txt; do
    if [ ! -f "$map" ]; then 
        echo "Chybi mapy"
        break
    fi
    
    m=$(basename "$map")
    echo "Mapa: $m"

    for t in $THREADS; do
        export OMP_NUM_THREADS=$t
        cas=$(srun --quiet -N 1 -n 1 -c 48 ./solverMP "$map")
        echo "TASK $t vlaken: $cas"
        echo "$m,TASK,1,$t,$cas" >> $CSV
    done

    for t in $THREADS; do
        export OMP_NUM_THREADS=$t
        cas=$(srun --quiet -N 1 -n 1 -c 48 ./solverData "$map" 5000)
        echo "DATA $t vlaken: $cas"
        echo "$m,DATA,1,$t,$cas" >> $CSV
    done

    export OMP_NUM_THREADS=48
    for n in $NODES; do
        cas=$(srun --quiet -N $n -n $n -c 48 ./solverMPI "$map")
        echo "MPI $n uzlu: $cas"
        echo "$m,MPI,$n,48,$cas" >> $CSV
    done
done

echo "Konec mereni"