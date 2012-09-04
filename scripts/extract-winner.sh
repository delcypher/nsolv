#!/bin/bash

if [ $# -ne 1 ]; then
	echo "$0 : <input file>"
	echo "<input file> - A logging file produced by NSolv"
	echo ""
	echo "This utility will report the number of times each solver completed first"
	exit
fi

INPUT="$1"

if [ ! -r "${INPUT}" ]; then
	echo "Can't open input file ${INPUT}"
	exit
fi

#Get solver names from file
SOLVERS=$(grep -E --max-count=1 '^# [0-9]+ solvers:' "${INPUT}" | sed 's/# [0-9]\+ solvers: //; s/,/ /g')

if [ -z "${SOLVERS}" ]; then
	echo "No solvers found!"
	exit
fi

echo -e "#[Solver name]\t[ number of times came first]"
#Loop over solvers extracting info
for solver in ${SOLVERS}
do

cat "${INPUT}" | awk 'BEGIN { counter=0;}
		     /#First solver to finish/ { if($5 == "'${solver}'") counter++;}
	              END { print "'${solver}' " counter;}'

done
