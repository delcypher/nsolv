#!/bin/bash

if [ $# -ne 2 ]; then
	echo "$0 : <input file> <output directory>"
	echo "<input file> - A logging file produced by NSolv"
	echo "<output directory> - The directory to place the files"
	echo ""
	echo "This utility will report the run times for each solver which placed in a seperate file for each solver"
	exit
fi

INPUT="$1"
OUTPUT_DIR="$2"

if [ ! -r "${INPUT}" ]; then
	echo "Can't open input file ${INPUT}"
	exit
fi

if [ ! -d "${OUTPUT_DIR}" ]; then
	echo "The directory ${OUTPUT_DIR} is not accessible"
	exit
fi

#Strip trailing slash for OUTPUT_DIR
OUTPUT_DIR=$( echo "$OUTPUT_DIR" | sed 's#/\+$##')

#Get solver names from file
SOLVERS=$(grep -E --max-count=1 '^# [0-9]+ solvers:' "${INPUT}" | sed 's/# [0-9]\+ solvers: //; s/,/ /g')


if [ -z "${SOLVERS}" ]; then
	echo "No solvers found!"
	exit
fi

#Loop over solvers extracting info
for solver in ${SOLVERS}
do

OUTPUT_FILE="${OUTPUT_DIR}/${solver}-runtimes.txt"
echo "Creating ${OUTPUT_FILE}"

echo -e "#[Run time]\t[ satisfiability]" > "${OUTPUT_FILE}"
cat "${INPUT}" | awk 'BEGIN { OFS="\t";}
		     /^'${solver}'/ { print $2,$3;} ' >> "${OUTPUT_FILE}"

done
