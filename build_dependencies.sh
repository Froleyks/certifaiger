#!/usr/bin/env bash
set -e
dir=$(cd "$(dirname "$0")/.." && pwd)
cd "$dir"
git clone https://github.com/arminbiere/kissat.git
git clone https://github.com/arminbiere/aiger.git
cd "$dir"/kissat
./configure && make -j8
cd "$dir"/aiger
sed -i 's/assert (reset <= 1 || reset == lit);//' aiger.c
./configure.sh && make -j8
echo Suceyssfully build kissat and aiger library
if [ -n "$1" ]; then
    echo "Downloading QAIGER dependencies to deal with non-stratified resets"
    cd "$dir"
    git clone --recurse-submodules https://github.com/ltentrup/quabs.git
    cd "$dir"/quabs
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make
fi
cd "$dir"/certifaiger
./configure && make -j8
./check_certificate.sh examples/model.aag examples/witness.aag
