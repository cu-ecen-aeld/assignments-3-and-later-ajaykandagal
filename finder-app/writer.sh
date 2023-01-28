#!/bin/sh

print_usage()
{
    echo "Total number of arguements should be 2"
    echo "The order of arguements should be:"
    echo "\t1)File directory path"
    echo "\t2)String to be written in to the specified file directory path"
}

if [ $# -ne 2 ]
then
    echo "Error: Invalid Number of Arguements"
    print_usage
    exit 1
fi

writefile=$1
writestr=$2

if [ ! -f $writefile ]
then
    mkdir -p $(dirname $writefile)
    touch $writefile
fi

if [ -f $writefile ]
then
    echo $writestr > $writefile
else
    echo "Error: Failed to create \"$writefile\" file"
fi