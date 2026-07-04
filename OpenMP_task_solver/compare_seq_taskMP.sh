#!/bin/bash

g++ -O3 solver.cpp -o solver
g++ -O3 -fopenmp solverMP.cpp -o solverMP

MAPS="mapa/mapa15_5.txt mapa/mapa9_9.txt mapa/mapa5_15.txt mapa/mapa7_11.txt mapa/mapa7_10.txt"
export OMP_NUM_THREADS=48
TIMEFORMAT="%3R"

echo "Mapa | Sekvencni | Paralelni (48 vlaken)"

for map in $MAPS; do
    [ ! -f "$map" ] && continue
    
    seq_time=$({ time ./solver "$map" > /dev/null; } 2>&1)
    par_time=$({ time ./solverMP "$map" 8 > /dev/null; } 2>&1)
    
    echo "$map | ${seq_time} s | ${par_time} s"
done