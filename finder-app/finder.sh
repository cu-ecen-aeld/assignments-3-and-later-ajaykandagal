#!/bin/sh
filesdir=$1
searchstr=$2

if [ $# -ne 2 ]
then
    echo "Invalid number of arguements"
    exit 1
fi

if [ ! -d $filesdir ]
then
    echo "Directory not present"
    exit 1
fi

files_count=0
lines_count=0

#Get only files including sub-directories
files=$(find $filesdir -type f)

for file in $files
do
    #Get only searchstring occurance in each file
    count=$(grep -c $searchstr $file)
    if [ $count -ne 0 ]
    then
        lines_count=$((lines_count+count))
        files_count=$((files_count+1))
    fi
done

echo "The number of files are $files_count and the number of matching lines are $lines_count"