#!/bin/sh
if [ $# -ne 2 ]; then
	echo "2 args not specified."
	exit 1
fi
filesdir=$1
searchstr=$2

if [ ! -d ${filesdir} ]; then
	echo "Directory \"${filesdir}\" does not exist."
	exit 1
fi

x=$(find $filesdir -type f | wc -l)
y=$(grep -r $searchstr $filesdir | wc -l)
echo "The number of files are $x and the number of matching lines are $y"
