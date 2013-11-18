#!/bin/sh

 # taiconv.sh
 #
 # Performs simple tests to verify that taiconv performs mostly as
 # expected. This script works with bash on GNU and might not be very
 # portable.
 #
 # Copyright 2005  Sami Tolvanen <sami@ngim.org>

# TODO:
#   - create a tricky test file to convert, and compare the result
#     to a known correct conversion

## Variables

PROG_TAICONV=taiconv


## Usage

if [ "x$1" == "x" ]; then
	echo "Usage: $0 path_to_srvctl_programs"
	exit 1
fi

if [ ! -s "$1/$PROG_TAICONV" ]; then
	echo "$0: invalid program directory: $PROG_TAICONV not found"
	exit 1
fi


## Test for required programs

REQUIRED="cat cmp pwd rm"

for p in `echo $REQUIRED`; do
	which $p >/dev/null 2>&1

	if [ $? -ne 0 ]; then
		echo "$0: program $p missing"
		exit 1
	fi
done


## Get full directory paths

cd "$1"
PROG_DIR="`pwd -P`"

## Create a temporary directory


## Start the games

ERRORS=0


## Run conversion tests for file input


## Run conversion tests for pipe input


## Clean up

if [ $ERRORS -gt 0 ]; then
	echo "$0: detected $ERRORS problems"
else
	echo "$0: no problems detected"
fi

exit $ERRORS
