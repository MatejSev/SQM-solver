#!/bin/bash
#SBATCH --job-name=PDP_seq
#SBATCH --output="seq_out_%J.out"
#SBATCH --error="seq_err_%J.err"
#SBATCH --partition=arm_serial
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1

source /etc/profile.d/zz-cray-pe.sh
module load cray-mvapich2_pmix_nogpu

echo "Kompiluju..."
CC solver.cpp -o solver -O3

# Priprava CSV pro Python
CSV_SOUBOR="vysledky.csv"
echo "mapa,cas" > $CSV_SOUBOR

echo "Vysledky (uklada se to i do $CSV_SOUBOR):"
echo "Mapa | Cas [s]"
echo "-------------------"

for map in mapa/*.txt; do
    if [ ! -f "$map" ]; then
        echo "Zadne mapy nenalezeny!"
        break
    fi

    # Spusti to a ulozi cas
    cas=$(srun --quiet -N 1 -n 1 -c 1 ./solver "$map")
    
    # Obycejny vypis jmena mapy a casu
    echo "$(basename $map) | $cas"
    
    # Pridani radku do CSVcka
    echo "$(basename $map),$cas" >> $CSV_SOUBOR
done

echo "Hotovo, data jsou v $CSV_SOUBOR"