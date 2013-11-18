#!/bin/sh

 # tainlog.sh
 #
 # Performs simple tests to verify that the tainlog performs mostly as
 # expected. This script works with bash on GNU and might not be very
 # portable.
 #
 # Copyright 2005  Sami Tolvanen <sami@ngim.org>

# TODO:
#  - verify that log files without the first column matches a random
#    text file fed to the tainlog (dd if=/dev/urandom | mimencode is
#    cat log1 log2 ... | awk '{ print $2 }')
#  - verify that lines longer than the buffer are correctly wrapped
#    (and indicated with a tab)
#  - verify that rotating works correctly with different parameters
#    (correct number of files is kept, they are not too big, oldest
#    is always deleted first etc.)
#  - verify that timestamps are always ascending
