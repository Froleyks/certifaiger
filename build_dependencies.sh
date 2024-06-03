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
cd $dir
./configure && make -j4
./check_certificate.sh examples/model.aag examples/witness.aag
