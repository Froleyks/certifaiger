#!/usr/bin/env bash
set -e
dir=$(cd "$(dirname "$0")" && pwd)
cd ..
git clone https://github.com/arminbiere/kissat.git
git clone https://github.com/arminbiere/aiger.git
(cd kissat && ./configure && make -j4)
cd aiger
sed -i 's/assert (reset <= 1 || reset == lit);//' aiger.c
./configure.sh && make
echo Suceyssfully build kissat and aiger library
if [ -n "$1" ]; then
    echo "Downloading qbf dependecies for non-stratified resets"
    cd ..
    git clone https://github.com/ltentrup/quabs.git
    cd quabs
    git submodule init
    git submodule update
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j4
fi
cd $dir
./configure && make -j4
./check_certificate.sh examples/model.aag examples/witness.aag
