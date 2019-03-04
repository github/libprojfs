# Library of functions shared by all test scripts, included by
# test-lib.sh.
#
# Copyright (C) 2005 Junio C Hamano
#
# See the NOTICE file distributed with this library for additional
# information regarding copyright ownership.  Additional functions are:
#
# Copyright (C) 2018-2019 GitHub, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see http://www.gnu.org/licenses/ .

# In some bourne shell implementations, the "unset" builtin returns
# nonzero status when a variable to be unset was not set in the first
# place.
#
# Use sane_unset when that should not be considered an error.

sane_unset () {
	unset "$@"
	return 0
}

# Stop execution and start a shell. This is useful for debugging tests.
#
# Be sure to remove all invocations of this command before submitting.

test_pause () {
	"$SHELL_PATH" <&6 >&5 2>&7
}

# Wrap cmd with a debugger. Adding this to a command can make it easier
# to understand what is going on in a failing test.
#
# Examples:
#     debug cmd ...
#     debug --debugger=nemiver cmd $ARGS
#     debug -d "valgrind --tool=memcheck --track-origins=yes" cmd $ARGS
debug () {
	case "$1" in
	-d)
		PROJFS_DEBUGGER="$2" &&
		shift 2
		;;
	--debugger=*)
		PROJFS_DEBUGGER="${1#*=}" &&
		shift 1
		;;
	*)
		PROJFS_DEBUGGER=1
		;;
	esac &&
	PROJFS_DEBUGGER="${PROJFS_DEBUGGER}" "$@" <&6 >&5 2>&7
}

# Get the modebits from a file.
test_modebits () {
	ls -l "$1" | $SED -e 's|^\(..........\).*|\1|'
}

write_script () {
	{
		echo "#!${2-"$SHELL_PATH"}" &&
		cat
	} >"$1" &&
	chmod +x "$1"
}

# Use test_set_prereq to tell that a particular prerequisite is available.
# The prerequisite can later be checked for in two ways:
#
# - Explicitly using test_have_prereq.
#
# - Implicitly by specifying the prerequisite tag in the calls to
#   test_expect_{success,failure,code}.
#
# The single parameter is the prerequisite tag (a simple word, in all
# capital letters by convention).

test_unset_prereq () {
	! test_have_prereq "$1" ||
	satisfied_prereq="${satisfied_prereq% $1 *} ${satisfied_prereq#* $1 }"
}

test_set_prereq () {
	case "$1" in
	!*)
		test_unset_prereq "${1#!}"
		;;
	*)
		satisfied_prereq="$satisfied_prereq$1 "
		;;
	esac
}
satisfied_prereq=" "
lazily_testable_prereq= lazily_tested_prereq=

# Usage: test_lazy_prereq PREREQ 'script'
test_lazy_prereq () {
	lazily_testable_prereq="$lazily_testable_prereq$1 "
	eval test_prereq_lazily_$1=\$2
}

test_run_lazy_prereq_ () {
	script='
$MKDIR_P "$TRASH_DIRECTORY/prereq-test-dir" &&
(
	cd "$TRASH_DIRECTORY/prereq-test-dir" &&'"$2"'
)'
	say >&3 "checking prerequisite: $1"
	say >&3 "$script"
	test_eval_ "$script"
	eval_ret=$?
	rm -rf "$TRASH_DIRECTORY/prereq-test-dir"
	if test "$eval_ret" = 0; then
		say >&3 "prerequisite $1 ok"
	else
		say >&3 "prerequisite $1 not satisfied"
	fi
	return $eval_ret
}

test_have_prereq () {
	# prerequisites can be concatenated with ','
	save_IFS=$IFS
	IFS=,
	set -- $*
	IFS=$save_IFS

	total_prereq=0
	ok_prereq=0
	missing_prereq=

	for prerequisite
	do
		case "$prerequisite" in
		!*)
			negative_prereq=t
			prerequisite=${prerequisite#!}
			;;
		*)
			negative_prereq=
		esac

		case " $lazily_tested_prereq " in
		*" $prerequisite "*)
			;;
		*)
			case " $lazily_testable_prereq " in
			*" $prerequisite "*)
				eval "script=\$test_prereq_lazily_$prerequisite" &&
				if test_run_lazy_prereq_ "$prerequisite" "$script"
				then
					test_set_prereq $prerequisite
				fi
				lazily_tested_prereq="$lazily_tested_prereq$prerequisite "
			esac
			;;
		esac

		total_prereq=$(($total_prereq + 1))
		case "$satisfied_prereq" in
		*" $prerequisite "*)
			satisfied_this_prereq=t
			;;
		*)
			satisfied_this_prereq=
		esac

		case "$satisfied_this_prereq,$negative_prereq" in
		t,|,t)
			ok_prereq=$(($ok_prereq + 1))
			;;
		*)
			# Keep a list of missing prerequisites; restore
			# the negative marker if necessary.
			prerequisite=${negative_prereq:+!}$prerequisite
			if test -z "$missing_prereq"
			then
				missing_prereq=$prerequisite
			else
				missing_prereq="$prerequisite,$missing_prereq"
			fi
		esac
	done

	test $total_prereq = $ok_prereq
}

test_declared_prereq () {
	case ",$test_prereq," in
	*,$1,*)
		return 0
		;;
	esac
	return 1
}

test_verify_prereq () {
	test -z "$test_prereq" ||
	expr >/dev/null "$test_prereq" : '[A-Z0-9_,!]*$' ||
	BUG "'$test_prereq' does not look like a prereq"
}

test_expect_failure () {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	BUG "not 2 or 3 parameters to test-expect-failure"
	test_verify_prereq
	export test_prereq
	if ! test_skip "$@"
	then
		say >&3 "checking known breakage: $2"
		if test_run_ "$2" expecting_failure
		then
			test_known_broken_ok_ "$1"
		else
			test_known_broken_failure_ "$1"
		fi
	fi
	test_finish_
}

test_expect_success () {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	BUG "not 2 or 3 parameters to test-expect-success"
	test_verify_prereq
	export test_prereq
	if ! test_skip "$@"
	then
		say >&3 "expecting success: $2"
		if test_run_ "$2"
		then
			test_ok_ "$1"
		else
			test_failure_ "$@"
		fi
	fi
	test_finish_
}

# test_external runs external test scripts that provide continuous
# test output about their progress, and succeeds/fails on
# zero/non-zero exit code.  It outputs the test output on stdout even
# in non-verbose mode, and announces the external script with "# run
# <n>: ..." before running it.  When providing relative paths, keep in
# mind that all scripts run in "trash directory".
# Usage: test_external description command arguments...
# Example: test_external 'Perl API' perl ../path/to/test.pl
test_external () {
	test "$#" = 4 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 3 ||
	BUG "not 3 or 4 parameters to test_external"
	descr="$1"
	shift
	test_verify_prereq
	export test_prereq
	if ! test_skip "$descr" "$@"
	then
		# Announce the script to reduce confusion about the
		# test output that follows.
		say_color "" "# run $test_count: $descr ($*)"
		# Export TEST_DIRECTORY and TRASH_DIRECTORY
		# to be able to use them in script
		export TEST_DIRECTORY TRASH_DIRECTORY
		# Run command; redirect its stderr to &4 as in
		# test_run_, but keep its stdout on our stdout even in
		# non-verbose mode.
		"$@" 2>&4
		if test "$?" = 0
		then
			if test $test_external_has_tap -eq 0; then
				test_ok_ "$descr"
			else
				say_color "" "# test_external test $descr was ok"
				test_success=$(($test_success + 1))
			fi
		else
			if test $test_external_has_tap -eq 0; then
				test_failure_ "$descr" "$@"
			else
				say_color error "# test_external test $descr failed: $@"
				test_failure=$(($test_failure + 1))
			fi
		fi
	fi
}

# Like test_external, but in addition tests that the command generated
# no output on stderr.
test_external_without_stderr () {
	# The temporary file has no (and must have no) security
	# implications.
	tmp=${TMPDIR:-/tmp}
	stderr="$tmp/projfs-external-stderr.$$.tmp"
	test_external "$@" 4> "$stderr"
	test -f "$stderr" || error "Internal error: $stderr disappeared."
	descr="no stderr: $1"
	shift
	say >&3 "# expecting no stderr from previous command"
	if test ! -s "$stderr"
	then
		rm "$stderr"

		if test $test_external_has_tap -eq 0; then
			test_ok_ "$descr"
		else
			say_color "" "# test_external_without_stderr test $descr was ok"
			test_success=$(($test_success + 1))
		fi
	else
		if test "$verbose" = t
		then
			output=$(echo; echo "# Stderr is:"; cat "$stderr")
		else
			output=
		fi
		# rm first in case test_failure exits.
		rm "$stderr"
		if test $test_external_has_tap -eq 0; then
			test_failure_ "$descr" "$@" "$output"
		else
			say_color error "# test_external_without_stderr test $descr failed: $@: $output"
			test_failure=$(($test_failure + 1))
		fi
	fi
}

# debugging-friendly alternatives to "test [-f|-d|-e]"
# The commands test the existence or non-existence of $1. $2 can be
# given to provide a more precise diagnosis.
test_path_is_file () {
	if ! test -f "$1"
	then
		echo "File $1 doesn't exist. $2"
		false
	fi
}

test_path_is_dir () {
	if ! test -d "$1"
	then
		echo "Directory $1 doesn't exist. $2"
		false
	fi
}

test_path_exists () {
	if ! test -e "$1"
	then
		echo "Path $1 doesn't exist. $2"
		false
	fi
}

# Check if the directory exists and is empty as expected, barf otherwise.
test_dir_is_empty () {
	test_path_is_dir "$1" &&
	if test -n "$(ls -a1 "$1" | egrep -v '^\.\.?$')"
	then
		echo "Directory '$1' is not empty, it contains:"
		ls -la "$1"
		return 1
	fi
}

test_path_is_missing () {
	if test -e "$1"
	then
		echo "Path exists:"
		ls -ld "$1"
		if test $# -ge 1
		then
			echo "$*"
		fi
		false
	fi
}

# test_line_count checks that a file has the number of lines it
# ought to. For example:
#
#	test_expect_success 'produce exactly one line of output' '
#		do something >output &&
#		test_line_count = 1 output
#	'
#
# is like "test $(wc -l <output) = 1" except that it passes the
# output through when the number of lines is wrong.

test_line_count () {
	if test $# != 3
	then
		BUG "not 3 parameters to test_line_count"
	elif ! test $(wc -l <"$3") "$1" "$2"
	then
		echo "test_line_count: line count for $3 !$1 $2"
		cat "$3"
		return 1
	fi
}

# Returns success if a comma separated string of keywords ($1) contains a
# given keyword ($2).
# Examples:
# `list_contains "foo,bar" bar` returns 0
# `list_contains "foo" bar` returns 1

list_contains () {
	case ",$1," in
	*,$2,*)
		return 0
		;;
	esac
	return 1
}

# This is not among top-level (test_expect_success | test_expect_failure)
# but is a prefix that can be used in the test script, like:
#
#	test_expect_success 'complain and die' '
#           do something &&
#           do something else &&
#	    test_must_fail cmd ...
#	'
#
# Writing this as "! cmd ..." is wrong, because
# the failure could be due to a segv.  We want a controlled failure.
#
# Accepts the following options:
#
#   ok=<signal-name>[,<...>]:
#     Don't treat an exit caused by the given signal as error.
#     Multiple signals can be specified as a comma separated list.
#     Currently recognized signal names are: sigpipe, success.
#     (Don't use 'success', use 'test_might_fail' instead.)

test_must_fail () {
	case "$1" in
	ok=*)
		_test_ok=${1#ok=}
		shift
		;;
	*)
		_test_ok=
		;;
	esac
	"$@" 2>&7
	exit_code=$?
	if test $exit_code -eq 0 && ! list_contains "$_test_ok" success
	then
		echo >&4 "test_must_fail: command succeeded: $*"
		return 1
	elif test_match_signal 13 $exit_code && list_contains "$_test_ok" sigpipe
	then
		return 0
	elif test $exit_code -gt 129 && test $exit_code -le 192
	then
		echo >&4 "test_must_fail: died by signal $(($exit_code - 128)): $*"
		return 1
	elif test $exit_code -eq 127
	then
		echo >&4 "test_must_fail: command not found: $*"
		return 1
	elif test $exit_code -eq 126
	then
		echo >&4 "test_must_fail: valgrind error: $*"
		return 1
	fi
	return 0
} 7>&2 2>&4

# Similar to test_must_fail, but tolerates success, too.  This is
# meant to be used in contexts like:
#
#	test_expect_success 'some command works without configuration' '
#		test_might_fail cmd ... &&
#		do something
#	'
#
# Writing "cmd ... || :" would be wrong,
# because we want to notice if it fails due to segv.
#
# Accepts the same options as test_must_fail.

test_might_fail () {
	test_must_fail ok=success "$@" 2>&7
} 7>&2 2>&4

# Similar to test_must_fail and test_might_fail, but check that a
# given command exited with a given exit code. Meant to be used as:
#
#	test_expect_success 'Merge with d/f conflicts' '
#		test_expect_code 1 cmd ...
#	'

test_expect_code () {
	want_code=$1
	shift
	"$@" 2>&7
	exit_code=$?
	if test $exit_code = $want_code
	then
		return 0
	fi

	echo >&4 "test_expect_code: command exited with $exit_code, we wanted $want_code $*"
	return 1
} 7>&2 2>&4

# test_cmp is a helper function to compare actual and expected output.
# You can use it like:
#
#	test_expect_success 'foo works' '
#		echo expected >expected &&
#		foo >actual &&
#		test_cmp expected actual
#	'
#
# This could be written as either "cmp" or "diff -u", but:
# - cmp's output is not nearly as easy to read as diff -u
# - not all diff versions understand "-u"

test_cmp() {
	$PROJFS_TEST_CMP "$@"
}

# test_cmp_bin - helper to compare binary files

test_cmp_bin() {
	cmp "$@"
}

# Call any command "$@" but be more verbose about its
# failure. This is handy for commands like "test" which do
# not output anything when they fail.
verbose () {
	"$@" && return 0
	echo -n >&4 "command failed: "
	echo >&4 "$@"
	return 1
}

# Check if the file expected to be empty is indeed empty, and barfs
# otherwise.

test_must_be_empty () {
	test_path_is_file "$1" &&
	if test -s "$1"
	then
		echo "'$1' is not empty, it contains:"
		cat "$1"
		return 1
	fi
}

# Print a sequence of integers in increasing order, either with
# two arguments (start and end):
#
#     test_seq 1 5 -- outputs 1 2 3 4 5 one line at a time
#
# or with one argument (end), in which case it starts counting
# from 1.

test_seq () {
	case $# in
	1)	set 1 "$@" ;;
	2)	;;
	*)	BUG "not 1 or 2 parameters to test_seq" ;;
	esac
	test_seq_counter__=$1
	while test "$test_seq_counter__" -le "$2"
	do
		echo "$test_seq_counter__"
		test_seq_counter__=$(( $test_seq_counter__ + 1 ))
	done
}

# This function can be used to schedule some commands to be run
# unconditionally at the end of the test to restore sanity:
#
#	test_expect_success 'test core.capslock' '
#		cmd ... &&
#		test_when_finished "reset-cmd ..." &&
#		hello world
#	'
#
# That would be roughly equivalent to
#
#	test_expect_success 'test core.capslock' '
#		cmd ... &&
#		hello world
#		reset-cmd ...
#	'
#
# except that the greeting and config --unset must both succeed for
# the test to pass.
#
# Note that under --immediate mode, no clean-up is done to help diagnose
# what went wrong.

test_when_finished () {
	# We cannot detect when we are in a subshell in general, but by
	# doing so on Bash is better than nothing (the test will
	# silently pass on other shells).
	test "${BASH_SUBSHELL-0}" = 0 ||
	BUG "test_when_finished does nothing in a subshell"
	test_cleanup="{ $*
		} && (exit \"\$eval_ret\"); eval_ret=\$?; $test_cleanup"
}

# This function writes out its parameters, one per line
test_write_lines () {
	printf "%s\n" "$@"
}

# Given a variable $1, normalize the value of it to one of "true",
# "false", or "auto" and store the result to it.
#
#     test_tristate PROJFS_TEST_FOO
#
# A variable set to an empty string is set to 'false'.
# A variable set to 'false' or 'auto' keeps its value.
# Anything else is set to 'true'.
# An unset variable defaults to 'auto'.
#
# The last rule is to allow people to set the variable to an empty
# string and export it to decline testing the particular feature
# for versions both before and after this change.  We used to treat
# both unset and empty variable as a signal for "do not test" and
# took any non-empty string as "please test".

test_tristate () {
	if eval "test x\"\${$1+isset}\" = xisset"
	then
		# explicitly set
		eval "
			case \"\$$1\" in
			'')	$1=false ;;
			auto)	;;
			*)	$1=\$(test_normalize_bool \$$1 || echo true) ;;
			esac
		"
	else
		eval "$1=auto"
	fi
}

# Exit the test suite, either by skipping all remaining tests or by
# exiting with an error. If "$1" is "auto", we then we assume we were
# opportunistically trying to set up some tests and we skip. If it is
# "true", then we report a failure.
#
# The error/skip message should be given by $2.
#
test_skip_or_die () {
	case "$1" in
	auto)
		skip_all=$2
		test_done
		;;
	true)
		error "$2"
		;;
	*)
		error "BUG: test tristate is '$1' (real error: $2)"
	esac
}

# Like "env PROJFS=BAR some-program", but run inside a subshell, which means
# it also works for shell functions (though those functions cannot impact
# the environment outside of the test_env invocation).
test_env () {
	(
		while test $# -gt 0
		do
			case "$1" in
			*=*)
				eval "${1%%=*}=\${1#*=}"
				eval "export ${1%%=*}"
				shift
				;;
			*)
				"$@" 2>&7
				exit
				;;
			esac
		done
	)
} 7>&2 2>&4

# Returns true if the numeric exit code in "$2" represents the expected signal
# in "$1". Signals should be given numerically.
test_match_signal () {
	if test "$2" = "$((128 + $1))"
	then
		# POSIX
		return 0
	elif test "$2" = "$((256 + $1))"
	then
		# ksh
		return 0
	fi
	return 1
}

# Start a projected filesystem command and wait for its mount to be
# available.  The helper program in the t/ directory should be in "$1",
# with the relative paths of the lower filesystem to be used for storage,
# and the mount point of the projected upper filesystem, in "$2" and "$3",
# with any options for the helper program in the remaining arguments.
# Any output from the helper program will be captured in a file
# with the basename from "$1" and the extension ".log", and any errors
# in a file with the basename from "$1" and the extension ".err".
projfs_start () {
	helper="$TEST_DIRECTORY/$1"; shift
	lower_path="$TRASH_DIRECTORY/$1"; shift
	mount_path="$TRASH_DIRECTORY/$1"; shift

	helper_log=$(basename "$helper").log
	helper_err=$(basename "$helper").err

	$MKDIR_P "$lower_path" "$mount_path" &&
	lower_dev=$(stat -c %D "$mount_path") || exit 1

	"$helper" "$@" "$lower_path" "$mount_path" \
		>"$helper_log" 2>"$helper_err" &
	projfs_pid=$!

	"$TEST_DIRECTORY"/wait_mount --timeout $PROJFS_TIMEOUT \
		"0x$lower_dev" "$mount_path" && \
	mount_dev=$(stat -c %D "$mount_path")

	if test $? -ne 0
	then
		projfs_stop
		return 1
	fi

	trap projfs_stop EXIT
}

# Stop the projected filesystem command that was started by projfs_start()
# and wait for its umount operation to be completed.
projfs_stop () {
	if test -n "$projfs_pid"
	then
		kill "$projfs_pid" &&
		"$TEST_DIRECTORY"/wait_mount --timeout $PROJFS_TIMEOUT \
			"0x$mount_dev" "$mount_path" && \
		wait "$projfs_pid" &&
		projfs_pid=""
	fi
}

EXEC_PID="exec.pid"
EXEC_LOG="exec.log"
EXEC_ERR="exec.err"

# Execute a command, capturing its process ID, and then append a message
# followed by the command's pid into a log file and an optional error log
# file.  The log file should be given in "$1", the log message head in "$2",
# the error log file in "$3", the error log message in "$4", and the command in
# the remaining arguments.
# To skip logging to an error log, "$3" should be an empty string.
# Note that command arguments with internal spaces must have single quotes
# passed in the argument, e.g., "'foo bar'".
# Output and errors from the command will be appended to "$EXEC_OUT" and
# "$EXEC_ERR", respectively.
projfs_log_exec () {
	exec_log="$1"; shift
	log_msg_head="$1"; shift
	exec_err="$1"; shift
	err_msg="$1"; shift

	# Do not chain because we want to proceed in the case of errors
	$SHELL_PATH -c "echo -n \$\$ >$EXEC_PID && exec $*" \
		>>"$EXEC_LOG" 2>>"$EXEC_ERR"; ret=$?

	echo -n "$log_msg_head" >>"$exec_log" &&
	cat "$EXEC_PID" >>"$exec_log" &&
	echo >>"$exec_log" &&
	if test ":$exec_err" != ":"
	then
		echo -n "$err_msg" >>"$exec_err" &&
		cat $EXEC_PID >>"$exec_err" &&
		echo '' >>"$exec_err"
	fi

	return $ret
}

# Run the given command twice in parallel, wait for both to complete, and
# return with 1 if at least one of the executions fails.
projfs_run_twice () {
	"$@" &
	pidA="$!"
	"$@" &
	pidB="$!"

	ret=0
	if ! wait $pidA; then
		ret=1
	fi
	if ! wait $pidB; then
		ret=1
	fi

	return $ret
}
