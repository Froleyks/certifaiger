set -e
cd ..
git clone https://github.com/arminbiere/kissat.git
git clone https://github.com/arminbiere/aiger.git
(cd kissat && ./configure && make -j4)
cd aiger
git checkout development
sed -i 's/assert (reset <= 1 || reset == lit);//' aiger.c
./configure.sh && make
echo Suceyssfully build kissat and aiger library
