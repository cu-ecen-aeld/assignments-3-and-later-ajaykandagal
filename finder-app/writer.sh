#!/bin/sh
writefile=$1
writestr=$2

if [ $# -ne 2 ]
then
    echo "Invalid number of arguements"
    exit 1
fi

if [ ! -f $writefile ]
then
    mkdir -p $(dirname $writefile)
    touch $writefile
fi

if [ -f $writefile ]
then
    echo $writestr > $writefile
else
    echo "Failed to create file"
fi