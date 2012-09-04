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

MERGE_FILE="${OUTPUT_DIR}/all-runtimes.txt"
MERGE_TEMP_FILE="${OUTPUT_DIR}/merge-runtimes-temp.txt"
MERGE_CMD_LINE=""
#Loop over solvers extracting info
for solver in ${SOLVERS}
do

OUTPUT_FILE="${OUTPUT_DIR}/${solver}-runtimes.txt"
echo "Creating ${OUTPUT_FILE}"

echo -e "#[Run time]\t[ satisfiability]" > "${OUTPUT_FILE}"
cat "${INPUT}" | awk 'BEGIN { OFS="\t";}
		     /^'${solver}'/ { print $2,$3;} ' >> "${OUTPUT_FILE}"

#Output part of header for merge file
echo -en "[Solver ${solver}]\t\t" >> "${MERGE_TEMP_FILE}"

#Add solver to the merge command line
MERGE_CMD_LINE="${MERGE_CMD_LINE} ${OUTPUT_FILE}"

done

#Output new line for merge file
echo "" >> "${MERGE_TEMP_FILE}"

echo "Creating ${MERGE_FILE}"
#Create merged file
cat "${MERGE_TEMP_FILE}" <(paste ${MERGE_CMD_LINE}) > "${MERGE_FILE}"

#Remove the temp file
rm "${MERGE_TEMP_FILE}"
