#!/bin/bash

if [ ! -f "./solverMP" ] || [ solverMP.cpp -nt ./solverMP ]; then
    echo "Kompiluji solverMP.cpp s podporou OpenMP..."
    g++ -O3 -fopenmp solverMP.cpp -o solverMP    
fi

echo "   TEST MAP, VLÁKEN A CUTOFFU     "

THREADS="1 2 4 8 16 32 48"
CUTOFFS="2 4 6 8 10" 
OUTPUT_CSV="vysledky.csv"

echo "Mapa,Cutoff,1,2,4,8,16,32,48" > "$OUTPUT_CSV"
TIMEFORMAT="%3R"

for map_file in mapa/*.txt; do
    if [ ! -f "$map_file" ]; then break; fi
    map_name=$(basename "$map_file")
    
    echo ">>> Mapa: $map_name <<<"
    
    for c in $CUTOFFS; do
        echo "--> Testuji hloubku Cutoff: $c"
        csv_line="$map_name,$c"
        
        for t in $THREADS; do
            export OMP_NUM_THREADS=$t        
            exec_time=$({ time ./solverMP "$map_file" "$c" > /dev/null; } 2>&1)            
            echo "    [Threads: $t] Čas: ${exec_time} s"
            csv_line="$csv_line,$exec_time"
        done
        
        echo "$csv_line" >> "$OUTPUT_CSV"
    done
    echo "------------------------------------------------"
done

echo "Hotovo! Výsledky byly uloženy do tabulky: $OUTPUT_CSV"