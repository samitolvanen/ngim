#!/bin/sh

 # monitor.sh
 #
 # Performs simple tests to verify that the monitor performs mostly as
 # expected.  This script works with bash on GNU and might not be very
 # portable.
 #
 # Copyright 2005  Sami Tolvanen <sami@ngim.org>

# TODO:
# 	- verify that command x actually terminates the monitor
#	- verify that command w does nothing if nothing has changed, but
#	  brings up the service if monitor/up has appeared
#	- verify that information in monitor/status is valid

## Variables

PROG_MONITOR=monitor
PROG_RUN=run
PROG_LOG=log

DIR_ACTIVE=active
DIR_ALL=all
DIR_MONITOR=monitor
FILE_UP=up
PIPE_CONTROL=control
PIPE_STDIN=stdin

SLEEP_FOR_MONITOR=6		# Decreasing this may trigger the suspension timer
SLEEP_FOR_SCANNER=10
KILL_TESTS=5


## Usage

if [ "x$1" == "x" ]; then
	echo "Usage: $0 path_to_srvctl_programs path_to_service_dir file_scanner_stdout"
	exit 1
fi

if [ ! -s "$1/$PROG_MONITOR" ]; then
	echo "$0: invalid program directory: $PROG_MONITOR not found"
	exit 1
fi

if [ ! -d "$2" ]; then
	echo "$0: invalid service directory"
	exit 1
fi

if [ ! -f "$3" ]; then
	echo "$0: scanner log ($3) does not exist"
	exit 1
fi


## Test for required programs

REQUIRED="which mktemp chmod touch cat sleep mkdir tail cmp cp rm grep ln"

for p in `echo $REQUIRED`; do
	which $p >/dev/null 2>&1

	if [ $? -ne 0 ]; then
		echo "$0: program $p missing"
		exit 1
	fi
done

## Get full directory paths

cd "$2"
CURR_DIR="`pwd -P`"

cd "$1"
PROG_DIR="`pwd -P`"

cd "$CURR_DIR"

# Directory structure:
#  $2						<-- working directory
#    active
#      ../$TEST_DIR			<-- symbolic link
#    all
#      $TEST_DIR
#        $TEST_PROG_DIR
#        $TEST_RSLT_DIR

## Create temporary service directory

echo "$0: creating temporary directories"

TEST_DIR=`mktemp -q -d -p "$DIR_ALL" service.XXXXXX`

TEST_PROG_DIR="progs"
mkdir "$TEST_DIR/$TEST_PROG_DIR" || exit 1

TEST_RSLT_DIR="results"
mkdir "$TEST_DIR/$TEST_RSLT_DIR" || exit 1

## Create test scripts

echo "$0: creating test scripts"

cat > "$TEST_DIR/$TEST_PROG_DIR/$PROG_RUN" <<-END
	#!/bin/sh
	echo \$\$ >> "$TEST_RSLT_DIR/$PROG_RUN.pids"
	exec cat 2>&1
END

chmod a+x "$TEST_DIR/$TEST_PROG_DIR/$PROG_RUN"
touch "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"
ln -s "$TEST_PROG_DIR/$PROG_RUN" "$TEST_DIR/$PROG_RUN"

cat > "$TEST_DIR/$TEST_PROG_DIR/$PROG_LOG" <<-END
	#!/bin/sh
	echo \$\$ >> "$TEST_RSLT_DIR/$PROG_LOG.pids"
	exec cat >> "$TEST_RSLT_DIR/$PROG_LOG.stdout" 2>&1
END

chmod a+x "$TEST_DIR/$TEST_PROG_DIR/$PROG_LOG"
touch "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"
touch "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.stdout"
ln -s "$TEST_PROG_DIR/$PROG_LOG" "$TEST_DIR/$PROG_LOG"


## Start the games

ERRORS=0

if [[ -d "$TEST_DIR" && -d "$TEST_DIR/$TEST_PROG_DIR" && -d "$TEST_DIR/$TEST_RSLT_DIR" ]]; then
	echo "$0: starting tests"
else
	echo "$0: failed to create temporary directories"
	((ERRORS++));
fi

## Create link to scanner directory to start monitor

echo "$0: starting monitor"
ln -s "../$TEST_DIR" "$DIR_ACTIVE"
sleep $SLEEP_FOR_SCANNER
sleep $SLEEP_FOR_MONITOR

if [ -s "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids" ]; then
	CURPID=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
	echo "$0: problem: started $PROG_RUN [pid $CURPID] too soon"
	((ERRORS++));
fi

if [ -s "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids" ]; then
	CURPID=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
	echo "$0: problem: started $PROG_LOG [pid $CURPID] too soon"
	((ERRORS++));
fi

if [ $ERRORS -eq 0 ]; then
	echo "$0: bringing service up"
	touch "$TEST_DIR/$DIR_MONITOR/$FILE_UP"
	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	if [ -s "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids" ]; then
		CURPID=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
		echo "$0: started $PROG_RUN [pid $CURPID]"
	else
		echo "$0: problem: failed to start $PROG_RUN"
		((ERRORS++));
	fi

	if [ -s "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids" ]; then
		CURPID=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
		echo "$0: started $PROG_LOG [pid $CURPID]"
	else
		echo "$0: problem: failed to start $PROG_LOG"
		((ERRORS++));
	fi
fi


## Test control commands

if [ $ERRORS -gt 0 ]; then
	echo "$0: skipping control command tests due to detected errors"
else
	echo "$0: testing control commands"

	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	for ((i = 1, j = 0; j < $KILL_TESTS; i++, j++)); do
		if [ $ERRORS -gt 0 ]; then
			break
		fi

		echo -n "$0: writing command k: $i/$KILL_TESTS: "
		echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
		sleep $SLEEP_FOR_MONITOR

		PIDS_RUN[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
		PIDS_LOG[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
		echo "$PROG_RUN [pid ${PIDS_RUN[$i]}] $PROG_LOG [pid ${PIDS_LOG[$i]}]"

		if [ ${PIDS_RUN[$i]} == ${PIDS_RUN[$j]} ]; then
			echo "$0: problem: failed to restart $PROG_RUN after command k"
			((ERRORS++));
		fi

		if [ ${PIDS_LOG[$i]} == ${PIDS_LOG[$j]} ]; then
			echo "$0: problem: failed to restart $PROG_LOG after command k"
			((ERRORS++));
		fi
	done

	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	for ((i = 1, j = 0; j < $KILL_TESTS; i++, j++)); do
		if [ $ERRORS -gt 0 ]; then
			break
		fi

		echo -n -e "$0: writing command 15 (signal): $i/$KILL_TESTS: "
		echo -n -e '\017' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
		sleep $SLEEP_FOR_MONITOR

		PIDS_RUN[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
		PIDS_LOG[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
		echo "$PROG_RUN [pid ${PIDS_RUN[$i]}] $PROG_LOG [pid ${PIDS_LOG[$i]}]"

		if [ ${PIDS_RUN[$i]} == ${PIDS_RUN[$j]} ]; then
			echo "$0: problem: failed to restart $PROG_RUN after command signal 15"
			((ERRORS++));
		fi

		if [ ${PIDS_LOG[$i]} != ${PIDS_LOG[$j]} ]; then
			echo "$0: problem: restarted $PROG_LOG after command signal 15"
			((ERRORS++));
		fi
	done
fi


## Make sure services are restarted if they die (or get killed)

if [ $ERRORS -gt 0 ]; then
	echo "$0: skipping restart tests due to detected problems"
else
	echo "$0: testing restarts if children die"

	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	for ((i = 1, j = 0; j < $KILL_TESTS; i++, j++)); do
		if [ $ERRORS -gt 0 ]; then
			break
		fi

		echo -n "$0: killing $PROG_LOG [pid ${PIDS_LOG[$j]}]: $i/$KILL_TESTS: "
		kill -KILL "${PIDS_LOG[$j]}"
		sleep $SLEEP_FOR_MONITOR

		PIDS_RUN[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
		PIDS_LOG[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
		echo "$PROG_RUN [pid ${PIDS_RUN[$i]}] $PROG_LOG [pid ${PIDS_LOG[$i]}]"

		if [ ${PIDS_LOG[$i]} == ${PIDS_LOG[$j]} ]; then
			echo "$0: problem: failed to restart $PROG_LOG after kill"
			((ERRORS++));
		fi

		if [ ${PIDS_RUN[$i]} != ${PIDS_RUN[$j]} ]; then
			echo "$0: problem: restarted $PROG_RUN after $PROG_LOG was killed"
			((ERRORS++));
		fi
	done

	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	for ((i = 1, j = 0; j < $KILL_TESTS; i++, j++)); do
		if [ $ERRORS -gt 0 ]; then
			break
		fi

		echo -n "$0: killing $PROG_RUN [pid ${PIDS_RUN[$j]}]: $i/$KILL_TESTS: "
		kill -KILL "${PIDS_RUN[$j]}"
		sleep $SLEEP_FOR_MONITOR

		PIDS_RUN[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`
		PIDS_LOG[$i]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`
		echo "$PROG_RUN [pid ${PIDS_RUN[$i]}] $PROG_LOG [pid ${PIDS_LOG[$i]}]"

		if [ ${PIDS_RUN[$i]} == ${PIDS_RUN[$j]} ]; then
			echo "$0: problem: failed to restart $PROG_RUN after kill"
			((ERRORS++));
		fi

		if [ ${PIDS_LOG[$i]} != ${PIDS_LOG[$j]} ]; then
			echo "$0: problem: restarted $PROG_LOG after $PROG_RUN was killed"
			((ERRORS++));
		fi
	done
fi


## Make sure run or log get started with the other one missing, and
## properly restarted when the reappear

if [ $ERRORS -gt 0 ]; then
	echo "$0: skipping missing $PROG_RUN test due to detected errors"
else
	echo "$0: testing behaviour with $PROG_RUN missing"

	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	# Remove run and restart
	rm -f "$TEST_DIR/$PROG_RUN"
	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_LOG[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	if [ ${PIDS_LOG[0]} == ${PIDS_LOG[1]} ]; then
		echo "$0: problem: failed to restart $PROG_LOG with $PROG_RUN missing"
		((ERRORS++));
	fi

	# Restore and restart run
	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	ln -s "$TEST_PROG_DIR/$PROG_RUN" "$TEST_DIR/$PROG_RUN"
	echo -n 'w' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_RUN[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	if [ ${PIDS_RUN[0]} == ${PIDS_RUN[1]} ]; then
		echo "$0: failed to restart $PROG_RUN after command w"
		((ERRORS++));
	fi
fi

if [ $ERRORS -gt 0 ]; then
	echo "$0: skipping missing $PROG_LOG test due to detected errors"
else
	echo "$0: testing behaviour with $PROG_LOG missing"

	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	# Remove log and restart
	rm -f "$TEST_DIR/$PROG_LOG"
	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_RUN[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	if [ ${PIDS_RUN[0]} == ${PIDS_RUN[1]} ]; then
		echo "$0: problem: failed to restart $PROG_RUN with $PROG_LOG missing"
		((ERRORS++));
	fi

	# Restore and restart log
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	ln -s "$TEST_PROG_DIR/$PROG_LOG" "$TEST_DIR/$PROG_LOG"
	echo -n 'w' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_LOG[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	if [ ${PIDS_LOG[0]} != ${PIDS_LOG[1]} ]; then
		echo "$0: problem: started missing $PROG_LOG after command w"
		((ERRORS++));
	fi

	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	if [ ${PIDS_LOG[0]} == ${PIDS_LOG[1]} ]; then
		echo "$0: problem: failed to restart $PROG_LOG after command k"
		((ERRORS++));
	fi
fi


## Test file descriptor forwarding

if [ $ERRORS -gt 0 ]; then
	echo "$0: skipping file descriptor tests due to detected problems"
else
	echo "$0: testing forwarding with $PROG_LOG"

	# Anything written to PIPE_STDIN should end up in PROG_LOG.stdout
	echo "$0: this is stdout with $PROG_LOG" > "$TEST_DIR/$TEST_RSLT_DIR/test.stdout"

	cp /dev/null "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.stdout"
	cat "$TEST_DIR/$TEST_RSLT_DIR/test.stdout" > "$TEST_DIR/$DIR_MONITOR/$PIPE_STDIN"
	sleep $SLEEP_FOR_MONITOR

	cmp -s "$TEST_DIR/$TEST_RSLT_DIR/test.stdout" "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.stdout"

	if [ $? -ne 0 ]; then
		echo "$0: problem: forwarding does not work with $PROG_LOG"
		((ERRORS++));
	fi

	# Try the same without log
	PIDS_RUN[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	rm -f "$TEST_DIR/$PROG_LOG"
	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_RUN[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_RUN.pids"`

	if [ "${PIDS_RUN[0]}" == "${PIDS_RUN[1]}" ]; then
		echo "$0: problem: failed to restart $PROG_RUN after command k"
		((ERRORS++));
	fi

	# Anything written to PIPE_STDIN should end up in the file $3
	echo "$0: testing forwarding without $PROG_LOG"

	STR_TEST="$0: this is stdout without $PROG_LOG"
	echo "$STR_TEST" > "$TEST_DIR/$DIR_MONITOR/$PIPE_STDIN"
	sleep $SLEEP_FOR_MONITOR

	grep "$STR_TEST" "$3" >/dev/null 2>&1

	if [ $? -ne 0 ]; then
		echo "$0: problem: forwarding does not work without $PROG_LOG"
		((ERRORS++));
	fi

	# Restart log
	PIDS_LOG[0]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	ln -s "$TEST_PROG_DIR/$PROG_LOG" "$TEST_DIR/$PROG_LOG"
	echo -n 'k' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
	sleep $SLEEP_FOR_MONITOR

	PIDS_LOG[1]=`tail -n 1 "$TEST_DIR/$TEST_RSLT_DIR/$PROG_LOG.pids"`

	if [ "${PIDS_LOG[0]}" == "${PIDS_LOG[1]}" ]; then
		echo "$0: problem: failed to restart $PROG_LOG after command k"
		((ERRORS++));
	fi
fi


## Clean up

echo "$0: shutting down service and monitor"
rm -f "$DIR_ACTIVE/service.*"
echo -n 'x' > "$TEST_DIR/$DIR_MONITOR/$PIPE_CONTROL"
sleep $SLEEP_FOR_MONITOR

echo "$0: removing temporary directories and files"
rm -rf "$TEST_DIR"

if [ $ERRORS -gt 0 ]; then
	echo "$0: detected $ERRORS problems"
else
	echo "$0: no problems detected"
fi

exit $ERRORS
