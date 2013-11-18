#!/bin/sh

 # scanner.sh
 #
 # Performs simple tests to verify that the scanner performs mostly as
 # expected.  This script works with bash on GNU and might not be very
 # portable.
 #
 # Copyright 2005  Sami Tolvanen <sami@ngim.org>


## User-tunable variables

START_MONITORS=100			# When increasing the number of monitors, also
RESTART_MONITORS=50			# increase the sleep time
SLEEP_FOR_MONITORS=10

MAX_SERVICES=500
PROG_SCANNER=scanner
PROG_MONITOR=monitor
PROG_MONITOR_TEST=monitor.sh
DIR_ACTIVE=active
DIR_ALL=all


## Usage

if [ "x$1" == "x" ]; then
	echo "Usage: $0 path_to_srvctl_programs path_to_test_scripts"
	exit 1
fi

if [ ! -s "$1/$PROG_SCANNER" ]; then
	echo "$0: invalid program directory: $PROG_SCANNER not found"
	exit 1
fi

if [ ! -s "$2/$PROG_MONITOR_TEST" ]; then
	echo "$0: invalid test script directory: $PROG_MONITOR_TEST not found"
	exit 1
fi

if [ -z "$TMPDIR" ]; then
	if [ -d /tmp ]; then
		TMPDIR="/tmp"
	else
		echo "$0: no temporary directory found, set TMPDIR"
		exit 1
	fi
fi

## Test for required programs

REQUIRED="which mktemp pkill chmod touch cat sleep mkdir head tail xargs sort diff cmp rm grep awk wc"

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


## Create temporary directories

echo "$0: creating temporary directories"

TEST_DIR=`mktemp -q -d -p "$TMPDIR" scanner_tests.XXXXXX`
SCAN_DIR=`mktemp -q -d -p "$TMPDIR" scanner_services.XXXXXX`

TEST_PROG_DIR="$TEST_DIR/progs"
mkdir "$TEST_PROG_DIR" || exit 1

TEST_RSLT_DIR="$TEST_DIR/results"
mkdir "$TEST_RSLT_DIR" || exit 1

SCAN_ACTIVE_DIR="$SCAN_DIR/$DIR_ACTIVE"
mkdir "$SCAN_ACTIVE_DIR" || exit 1

SCAN_ALL_DIR="$SCAN_DIR/$DIR_ALL"
mkdir "$SCAN_ALL_DIR" || exit 1


## Create test scripts

echo "$0: creating test scripts"

cat > "$TEST_PROG_DIR/$PROG_SCANNER" <<-END
	#!/bin/sh
	echo \$\$ > "$TEST_RSLT_DIR/$PROG_SCANNER.pid"
	exec "$PROG_DIR/$PROG_SCANNER" "$SCAN_DIR" > "$TEST_RSLT_DIR/$PROG_SCANNER.stdout" 2> "$TEST_RSLT_DIR/$PROG_SCANNER.stderr"
END

chmod a+x "$TEST_PROG_DIR/$PROG_SCANNER"
touch "$TEST_RSLT_DIR/$PROG_SCANNER.pid"

cat > "$TEST_PROG_DIR/$PROG_MONITOR" <<-END
	#!/bin/sh
	echo \$\$ >> "$TEST_RSLT_DIR/$PROG_MONITOR.pids"
	echo \$1 >> "$TEST_RSLT_DIR/$PROG_MONITOR.params"
	exec sleep 300
END

chmod a+x "$TEST_PROG_DIR/$PROG_MONITOR"
touch "$TEST_RSLT_DIR/$PROG_MONITOR.pids"
touch "$TEST_RSLT_DIR/$PROG_MONITOR.params"


## Start the games

ERRORS=0

if [[ -d "$TEST_DIR" && -d "$PROG_DIR" && -d "$TEST_PROG_DIR" && -d "$TEST_RSLT_DIR" ]]; then
	echo "$0: starting tests"
else
	echo "$0: problem: failed to create temporary directories"
	((ERRORS++));
fi


## Start scanner

echo -n "$0: starting $PROG_SCANNER"

PATH="$TEST_PROG_DIR:$PROG_DIR:$PATH" "$TEST_PROG_DIR/$PROG_SCANNER" &
sleep 5

if [ -s "$TEST_RSLT_DIR/$PROG_SCANNER.pid" ]; then
	SCANNER_PID=`cat "$TEST_RSLT_DIR/$PROG_SCANNER.pid"`
	echo " [pid $SCANNER_PID]"
else
	echo " failed"
	((ERRORS++));
fi

grep "fatal" "$TEST_RSLT_DIR/$PROG_SCANNER.stderr" >/dev/null 2>&1

if [ $? -eq 0 ]; then
	echo "$0: problem: detected a fatal failure while starting $PROG_SCANNER"
	((ERRORS++));
fi


## Make sure monitors are started properly

if [ $ERRORS -ne 0 ]; then
	echo "$0: skipping monitor start tests due to detected problems"
else
	echo "$0: starting $START_MONITORS monitors"

	for ((i=0; i < $START_MONITORS; i++)); do
		mkdir "$SCAN_ALL_DIR/service$i"
		ln -s "../$DIR_ALL/service$i" "$SCAN_ACTIVE_DIR/service$i"
	done

	sleep $SLEEP_FOR_MONITORS

	STARTED1=`wc "$TEST_RSLT_DIR/$PROG_MONITOR.params" | awk '{ print $1 }'`

	echo "$0: started a total of $STARTED1 monitors for services:"
	cat -n "$TEST_RSLT_DIR/$PROG_MONITOR.params"

	if [ $STARTED1 -ne $START_MONITORS ]; then
		if [[ $START_MONITORS -gt $MAX_SERVICES && $STARTED1 -eq $MAX_SERVICES ]]; then
			echo "$0: successfully started maximum number of services ($MAX_SERVICES)"
		else
			echo "$0: problem: $STARTED1 / $START_MONITORS monitors were started"
			((ERRORS++));
		fi
	fi


	## Make sure monitors are also restarted properly if they happen to die

	if [ $RESTART_MONITORS -gt $STARTED1 ]; then
		RESTART_MONITORS=$STARTED1
	fi

	if [ $RESTART_MONITORS -gt 0 ]; then
		echo "$0: restarting the first $RESTART_MONITORS monitors"

		head -n $RESTART_MONITORS "$TEST_RSLT_DIR/$PROG_MONITOR.pids" | xargs -n1 kill
		head -n $RESTART_MONITORS "$TEST_RSLT_DIR/$PROG_MONITOR.params" | sort > "$TEST_RSLT_DIR/$PROG_MONITOR.params_torestart"
		sleep $SLEEP_FOR_MONITORS

		STARTED2=`wc "$TEST_RSLT_DIR/$PROG_MONITOR.params" | awk '{ print $1 }'`
		((STARTED2 = STARTED2 - STARTED1));

		echo "$0: restarted a total of $STARTED2 monitors for services:"
		tail -n $STARTED2 "$TEST_RSLT_DIR/$PROG_MONITOR.params" | cat -n

		if [ $STARTED2 -ne $RESTART_MONITORS ]; then
			echo "$0: problem: restarted $STARTED2 / $RESTART_MONITORS monitors"
			((ERRORS++));
		else
			tail -n $STARTED2 "$TEST_RSLT_DIR/$PROG_MONITOR.params" | sort > "$TEST_RSLT_DIR/$PROG_MONITOR.params_restarted"
			cmp -s "$TEST_RSLT_DIR/$PROG_MONITOR.params_torestart" "$TEST_RSLT_DIR/$PROG_MONITOR.params_restarted" >/dev/null 2>&1

			if [ $? -ne 0 ]; then
				echo "$0: problem: requested services weren't restarted:"
				diff -u "$TEST_RSLT_DIR/$PROG_MONITOR.params_torestart" "$TEST_RSLT_DIR/$PROG_MONITOR.params_restarted" | cat -n
				((ERRORS++));
			fi
		fi
	else
		echo "$0: problem: skipping restart test due to detected problems"
		((ERRORS++));
	fi


	## Kill our fake monitors

	echo "$0: removing monitor directories"

	for ((i=0; i < $START_MONITORS; i++)); do
		rm -f "$SCAN_ACTIVE_DIR/service$i"
		rm -rf "$SCAN_ALL_DIR/service$i"
	done

	echo "$0: shutting down monitors"
	pkill -P `cat "$TEST_RSLT_DIR/$PROG_SCANNER.pid"`>/dev/null 2>&1
	sleep $SLEEP_FOR_MONITORS


	## See if there were any fatal problems this far

	grep "fatal" "$TEST_RSLT_DIR/$PROG_SCANNER.stderr" >/dev/null 2>&1

	if [ $? -eq 0 ]; then
		echo "$0: problem: detected a fatal failure during tests"
		((ERRORS++));
	fi
fi

## Remove the fake monitor script

rm -f "$TEST_PROG_DIR/$PROG_MONITOR"


## Start monitor.sh to test the actual monitor behaviour

if [ $ERRORS -ne 0 ]; then
	echo "$0: skipping monitor.sh due to detected problems"
else
	echo "$0: starting $PROG_MONITOR_TEST"
	"./$PROG_MONITOR_TEST" "$PROG_DIR" "$SCAN_DIR" "$TEST_RSLT_DIR/$PROG_SCANNER.stdout"

	if [ $? -ne 0 ]; then
		echo "$0: problem: test script $PROG_MONITOR_TEST detected errors"
		((ERRORS++));
	fi
fi

## Shut down the scanner

if [ -s "$TEST_RSLT_DIR/$PROG_SCANNER.pid" ]; then
	echo "$0: shutting down $PROG_SCANNER... sending TERM"
	kill `cat "$TEST_RSLT_DIR/$PROG_SCANNER.pid"` >/dev/null 2>&1
	sleep 3
	echo "$0: shutting down $PROG_SCANNER... sending KILL"
	kill -KILL `cat "$TEST_RSLT_DIR/$PROG_SCANNER.pid"` >/dev/null 2>&1
	sleep 3
fi


## Display scanner's output

echo "$0: $PROG_SCANNER.stderr:"
cat -n "$TEST_RSLT_DIR/$PROG_SCANNER.stderr"

echo "$0: $PROG_SCANNER.stdout:"
cat -n "$TEST_RSLT_DIR/$PROG_SCANNER.stdout"


## Clean up directories and files

echo "$0: removing temporary directories and files"
rm -rf "$SCAN_DIR" >/dev/null 2>&1
rm -rf "$TEST_DIR" >/dev/null 2>&1

if [ $ERRORS -gt 0 ]; then
	echo "$0: detected $ERRORS problems"
else
	echo "$0: no problems detected"
fi

exit $ERRORS
