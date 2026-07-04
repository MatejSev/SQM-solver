#!/bin/bash

if [ ! -f "./solver" ]; then
    echo "Kompiluji solver.cpp..."
    g++ -O3 solver.cpp -o solver
fi

echo "       TEST VSECH MAP V ADRESARI          "

for map_file in mapa/*.txt; do
    echo ">>> Mapa: $map_file <<<"
    time ./solver "$map_file"
    echo "------------------------------------------"
done

echo "Vsechny mapy byly zpracovany."