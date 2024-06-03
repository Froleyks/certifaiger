#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ x"$CERTIFAIGER" = x ] && CERTIFAIGER="$SCRIPT_DIR/certifaiger"
[ x"$KISSAT" = x ] && KISSAT="$SCRIPT_DIR/../kissat/build/kissat"
[ x"$AIGTOCNF" = x ] && AIGTOCNF="$SCRIPT_DIR/../aiger/aigtocnf"
TEMP_DIR="/tmp/certifaiger"

die() {
    printf "$1\n"
    exit 1
}
find_binary() {
    path=${!1}
    name=${1,,}
    [ ! -f "$path" ] && die "$name not found at $path
Please compile $name by running '$3' in the $2 directory
or point $1 to the binary"
    echo "Using $path"
}

[ "$#" -lt 2 ] && die "Usage: $0 <model> <witness>"
find_binary CERTIFAIGER certifaiger './configure && make'
find_binary KISSAT kissat './configure && make'
find_binary AIGTOCNF aiger './configure.sh && make aigtocnf'

CHECKS=(base step property reset transition)

mkdir -p "$TEMP_DIR"
TMP="$TEMP_DIR/$$_"
echo "Writing temporary files to $TMP"
cleanup() {
    for check in "${CHECKS[@]}"; do
        rm -f "${TMP}$check.aig" "${TMP}$check.cnf"
    done
}
trap cleanup EXIT HUP INT QUIT TERM

AIGS=()
for check in "${CHECKS[@]}"; do
    AIGS+=("${TMP}$check.aig")
done
$CERTIFAIGER "$1" "$2" "${AIGS[@]}" || die "Error: certifaiger execution failed."

ALL_SUCCESS=true
echo
for check in "${CHECKS[@]}"; do
    echo Checking "$check"
    $AIGTOCNF "${TMP}$check.aig" "${TMP}$check.cnf"
    $KISSAT --quiet "${TMP}$check.cnf"
    if [ $? -ne 20 ]; then
        ALL_SUCCESS=false
        echo "Error: ${check} check failed"
    fi
    echo
done
if $ALL_SUCCESS; then
    echo "Success: Certificate check passed."
    exit 0
else
    echo "Error: Certificate check failed."
    exit 1
fi
