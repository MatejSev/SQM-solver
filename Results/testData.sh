#!/bin/bash

source /etc/profile.d/zz-cray-pe.sh
module load cray-mvapich2_pmix_nogpu

rm -f ./solverData

if [ ! -f "./solverData" ] || [ solverData.cpp -nt ./solverData ]; then
    echo "Kompiluji solverData.cpp s podporou OpenMP..."
    g++ -O3 -fopenmp solverData.cpp -o solverData    
fi

echo "   TEST MAP, VLÁKEN A ENOUGHSTATES (Data Parallelism)   "

THREADS="1 2 4 8 16 32 48"
STATES="500 1000 5000 10000 50000" 
OUTPUT_CSV="vysledky_states_data.csv"

echo "Mapa,EnoughStates,1,2,4,8,16,32,48" > "$OUTPUT_CSV"
TIMEFORMAT="%3R"

for map_file in mapa/*.txt; do
    if [ ! -f "$map_file" ]; then break; fi
    map_name=$(basename "$map_file")
    
    echo ">>> Mapa: $map_name <<<"
    
    for s in $STATES; do
        echo "--> Testuji EnoughStates (BFS hloubku): $s"
        csv_line="$map_name,$s"
        
        for t in $THREADS; do
            export OMP_NUM_THREADS=$t        
            exec_time=$({ time ./solverData "$map_file" "$s" > /dev/null; } 2>&1)            
            echo "    [Threads: $t] Čas: ${exec_time} s"
            csv_line="$csv_line,$exec_time"
        done
        
        echo "$csv_line" >> "$OUTPUT_CSV"
    done
    echo "------------------------------------------------"
done

echo "Hotovo! Výsledky byly uloženy do tabulky: $OUTPUT_CSV"