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

# I/O redirections used in this library:
#
# fd 3: verbose output, if any
# fd 4: verbose errors, if any
# fd 5: stdout
# fd 6: stdin
# fd 7: stderr
#
# The file descriptors 5 to 7 are used when a test function needs to
# read/write to a "real" fd from inside a test_eval() call stack, where
# fds 0 to 2 have already been redirected.

# Timeout in seconds before abandoning attempt to wait for mount/umount
# of projfs.
PROJFS_TIMEOUT=30

# If no positional arguments were provided and $PROJFS_TEST_OPTS was,
# we are running within the automake TAP harness and should respect any
# flags the user passed via the environment instead of the command line.
test "$#" -eq 0 && test -n "$PROJFS_TEST_OPTS" && \
  eval set -- "$PROJFS_TEST_OPTS"

# Test the binaries we have just built.  The tests are kept in
# t/ subdirectory and are run in 'trash directory' subdirectory.
if test -z "$TEST_DIRECTORY"
then
	# We allow tests to override this, in case they want to run tests
	# outside of t/, e.g. for running tests on the test library
	# itself.
	TEST_DIRECTORY=$(pwd)
else
	# ensure that TEST_DIRECTORY is an absolute path so that it
	# is valid even if the current working directory is changed
	TEST_DIRECTORY=$(cd "$TEST_DIRECTORY" && pwd) || exit 1
fi
if test -z "$TEST_OUTPUT_DIRECTORY"
then
	# Similarly, override this to store the test-output subdir
	# elsewhere
	TEST_OUTPUT_DIRECTORY=$TEST_DIRECTORY
fi
PROJFS_BUILD_DIR="$TEST_DIRECTORY"/..

# If we were built with ASAN, it may complain about leaks
# of program-lifetime variables. Disable it by default to lower
# the noise level. This needs to happen at the start of the script,
# before we even do our "did we build projfs yet" check (since we don't
# want that one to complain to stderr).
: ${ASAN_OPTIONS=detect_leaks=0:abort_on_error=1}
export ASAN_OPTIONS

# If LSAN is in effect we _do_ want leak checking, but we still
# want to abort so that we notice the problems.
: ${LSAN_OPTIONS=abort_on_error=1}
export LSAN_OPTIONS

if test ! -f "$PROJFS_BUILD_DIR"/config.sh
then
	echo >&2 'error: config.sh missing (has libprojfs been built?)'
	exit 1
fi
. "$PROJFS_BUILD_DIR"/config.sh
export SHELL_PATH

################################################################
# It appears that people try to run tests without building...
if ! test -x "$TEST_DIRECTORY"/test_simple
then
	case " $* " in
	*' --clean '*|*' --clean-all '*)
		# proceed as we will exit after removing test directories
		;;
	*)
		echo >&2 'error: test programs missing (has "make" been run?)'
		exit 1
		;;
	esac
fi

# if --tee was passed, write the output not only to the terminal, but
# additionally to the file test-output/$BASENAME.out, too.
case "$PROJFS_TEST_TEE_STARTED, $* " in
done,*|*' --clean '*|*' --clean-all '*)
	# do not redirect again
	;;
*' --tee '*|*' --va'*|*' -V '*|*' --verbose-log '*)
	$MKDIR_P "$TEST_OUTPUT_DIRECTORY/test-output"
	BASE="$TEST_OUTPUT_DIRECTORY/test-output/$(basename "$0" .sh)"

	# Make this filename available to the sub-process in case it is using
	# --verbose-log.
	PROJFS_TEST_TEE_OUTPUT_FILE=$BASE.out
	export PROJFS_TEST_TEE_OUTPUT_FILE

	# Truncate before calling "tee -a" to get rid of the results
	# from any previous runs.
	>"$PROJFS_TEST_TEE_OUTPUT_FILE"

	(PROJFS_TEST_TEE_STARTED=done ${TEST_SHELL_PATH} "$0" "$@" 2>&1;
	 echo $? >"$BASE.exit") | tee -a "$PROJFS_TEST_TEE_OUTPUT_FILE"
	test "$(cat "$BASE.exit")" = 0
	exit
	;;
esac

# For repeatability, reset the environment to known value.
# TERM is sanitized below, after saving color control sequences.
LANG=C
LC_ALL=C
export LANG LC_ALL
unset VISUAL LANGUAGE COLUMNS

# Add libc MALLOC and MALLOC_PERTURB test
# only if we are not executing the test with valgrind
if expr " $PROJFS_TEST_OPTS " : ".* --valgrind " >/dev/null ||
   test -n "$TEST_NO_MALLOC_CHECK"
then
	setup_malloc_check () {
		: nothing
	}
	teardown_malloc_check () {
		: nothing
	}
else
	setup_malloc_check () {
		MALLOC_CHECK_=3	MALLOC_PERTURB_=165
		export MALLOC_CHECK_ MALLOC_PERTURB_
	}
	teardown_malloc_check () {
		unset MALLOC_CHECK_ MALLOC_PERTURB_
	}
fi

# Protect ourselves from common misconfiguration to export
# CDPATH into the environment
unset CDPATH

# Each test should start with something like this, after copyright notices:
#
# test_description='Description of this test...
# This test checks if command xyzzy does the right thing...
# '
# . ./test-lib.sh
test "x$TERM" != "xdumb" && (
		test -t 1 &&
		tput bold >/dev/null 2>&1 &&
		tput setaf 1 >/dev/null 2>&1 &&
		tput sgr0 >/dev/null 2>&1
	) &&
	color=t

while test "$#" -ne 0
do
	case "$1" in
	-d|--d|--de|--deb|--debu|--debug)
		debug=t; shift ;;
	-i|--i|--im|--imm|--imme|--immed|--immedi|--immedia|--immediat|--immediate)
		immediate=t; shift ;;
	-r)
		shift; test "$#" -ne 0 || {
			echo 'error: -r requires an argument' >&2;
			exit 1;
		}
		run_list=$1; shift ;;
	--run=*)
		run_list=${1#--*=}; shift ;;
	-h|--h|--he|--hel|--help)
		help=t; shift ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t; shift ;;
	--verbose-only=*)
		verbose_only=${1#--*=}
		shift ;;
	-q|--q|--qu|--qui|--quie|--quiet)
		# Ignore --quiet under a TAP::Harness. Saying how many tests
		# passed without the ok/not ok details is always an error.
		test -z "$HARNESS_ACTIVE" && quiet=t; shift ;;
	--no-color)
		color=; shift ;;
	--va|--val|--valg|--valgr|--valgri|--valgrin|--valgrind)
		valgrind=memcheck
		shift ;;
	--valgrind=*)
		valgrind=${1#--*=}
		shift ;;
	--valgrind-only=*)
		valgrind_only=${1#--*=}
		shift ;;
	--tee)
		shift ;; # was handled already
	--root=*)
		root=${1#--*=}
		shift ;;
	--chain-lint)
		PROJFS_TEST_CHAIN_LINT=1
		shift ;;
	--no-chain-lint)
		PROJFS_TEST_CHAIN_LINT=0
		shift ;;
	-x)
		# Some test scripts can't be reliably traced  with '-x',
		# unless the test is run with a Bash version supporting
		# BASH_XTRACEFD (introduced in Bash v4.1).  Check whether
		# this test is marked as such, and ignore '-x' if it
		# isn't executed with a suitable Bash version.
		if test -z "$test_untraceable" || {
		     test -n "$BASH_VERSION" && {
		       test ${BASH_VERSINFO[0]} -gt 4 || {
			 test ${BASH_VERSINFO[0]} -eq 4 &&
			 test ${BASH_VERSINFO[1]} -ge 1
		       }
		     }
		   }
		then
			trace=t
		else
			echo >&2 "warning: ignoring -x; '$0' is untraceable without BASH_XTRACEFD"
		fi
		shift ;;
	-V|--verbose-log)
		verbose_log=t
		shift ;;
	--clean)
		clean=t
		shift ;;
	--clean-all)
		clean=t
		cleanall=t
		shift ;;
	*)
		echo "error: unknown test option '$1'" >&2; exit 1 ;;
	esac
done

if test -n "$valgrind_only"
then
	test -z "$valgrind" && valgrind=memcheck
	test -z "$verbose" && verbose_only="$valgrind_only"
elif test -n "$valgrind"
then
	test -z "$verbose_log" && verbose=t
fi

if test -n "$trace" && test -z "$verbose_log"
then
	verbose=t
fi

if test -n "$color"
then
	# Save the color control sequences now rather than run tput
	# each time say_color() is called.  This is done for two
	# reasons:
	#   * TERM will be changed to dumb
	#   * HOME will be changed to a temporary directory and tput
	#     might need to read ~/.terminfo from the original HOME
	#     directory to get the control sequences
	# Note:  This approach assumes the control sequences don't end
	# in a newline for any terminal of interest (command
	# substitutions strip trailing newlines).  Given that most
	# (all?) terminals in common use are related to ECMA-48, this
	# shouldn't be a problem.
	say_color_error=$(tput bold; tput setaf 1) # bold red
	say_color_skip=$(tput setaf 4) # blue
	say_color_warn=$(tput setaf 3) # brown/yellow
	say_color_pass=$(tput setaf 2) # green
	say_color_info=$(tput setaf 6) # cyan
	say_color_reset=$(tput sgr0)
	say_color_="" # no formatting for normal text
	say_color () {
		test -z "$1" && test -n "$quiet" && return
		eval "say_color_color=\$say_color_$1"
		shift
		printf "%s\\n" "$say_color_color$*$say_color_reset"
	}
else
	say_color() {
		test -z "$1" && test -n "$quiet" && return
		shift
		printf "%s\n" "$*"
	}
fi

TERM=dumb
export TERM

error () {
	say_color error "error: $*"
	PROJFS_EXIT_OK=t
	exit 1
}

BUG () {
	error >&7 "bug in the test script: $*"
}

say () {
	say_color info "$*"
}

if test -n "$HARNESS_ACTIVE"
then
	if test "$verbose" = t || test -n "$verbose_only"
	then
		printf 'Bail out! %s\n' \
		 'verbose mode forbidden under TAP harness; try --verbose-log'
		exit 1
	fi
fi

test "${test_description}" != "" ||
error "Test script did not set test_description."

if test "$help" = "t"
then
	printf '%s\n' "$test_description"
	exit 0
fi

exec 5>&1
exec 6<&0
exec 7>&2
if test "$verbose_log" = "t" && test "$clean" != "t"
then
	exec 3>>"$PROJFS_TEST_TEE_OUTPUT_FILE" 4>&3
elif test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

# Send any "-x" output directly to stderr to avoid polluting tests
# which capture stderr. We can do this unconditionally since it
# has no effect if tracing isn't turned on.
#
# Note that this sets up the trace fd as soon as we assign the variable, so it
# must come after the creation of descriptor 4 above. Likewise, we must never
# unset this, as it has the side effect of closing descriptor 4, which we
# use to show verbose tests to the user.
#
# Note also that we don't need or want to export it. The tracing is local to
# this shell, and we would not want to influence any shells we exec.
BASH_XTRACEFD=4

test_failure=0
test_count=0
test_fixed=0
test_broken=0
test_success=0

test_external_has_tap=0

die () {
	code=$?
	if test -n "$PROJFS_EXIT_OK"
	then
		exit $code
	else
		echo >&5 "FATAL: Unexpected exit with code $code"
		exit 1
	fi
}

PROJFS_EXIT_OK=
trap 'die' EXIT
trap 'exit $?' INT

# The user-facing functions are loaded from a separate file so that
# test_perf subshells can have them too
. "$TEST_DIRECTORY/test-lib-functions.sh"

# You are not expected to call test_ok_ and test_failure_ directly, use
# the test_expect_* functions instead.

test_ok_ () {
	test_success=$(($test_success + 1))
	say_color "" "ok $test_count - $@"
}

test_failure_ () {
	test_failure=$(($test_failure + 1))
	say_color error "not ok $test_count - $1"
	shift
	printf '%s\n' "$*" | $SED -e 's/^/#	/'
	test "$immediate" = "" || { PROJFS_EXIT_OK=t; exit 1; }
}

test_known_broken_ok_ () {
	test_fixed=$(($test_fixed+1))
	say_color error "ok $test_count - $@ # TODO known breakage vanished"
}

test_known_broken_failure_ () {
	test_broken=$(($test_broken+1))
	say_color warn "not ok $test_count - $@ # TODO known breakage"
}

test_debug () {
	test "$debug" = "" || eval "$1"
}

match_pattern_list () {
	arg="$1"
	shift
	test -z "$*" && return 1
	for pattern_
	do
		case "$arg" in
		$pattern_)
			return 0
		esac
	done
	return 1
}

match_test_selector_list () {
	title="$1"
	shift
	arg="$1"
	shift
	test -z "$1" && return 0

	# Both commas and whitespace are accepted as separators.
	OLDIFS=$IFS
	IFS=' 	,'
	set -- $1
	IFS=$OLDIFS

	# If the first selector is negative we include by default.
	include=
	case "$1" in
		!*) include=t ;;
	esac

	for selector
	do
		orig_selector=$selector

		positive=t
		case "$selector" in
			!*)
				positive=
				selector=${selector##?}
				;;
		esac

		test -z "$selector" && continue

		case "$selector" in
			*-*)
				if expr "z${selector%%-*}" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in range" \
						"start: '$orig_selector'" >&2
					exit 1
				fi
				if expr "z${selector#*-}" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in range" \
						"end: '$orig_selector'" >&2
					exit 1
				fi
				;;
			*)
				if expr "z$selector" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in test" \
						"selector: '$orig_selector'" >&2
					exit 1
				fi
		esac

		# Short cut for "obvious" cases
		test -z "$include" && test -z "$positive" && continue
		test -n "$include" && test -n "$positive" && continue

		case "$selector" in
			-*)
				if test $arg -le ${selector#-}
				then
					include=$positive
				fi
				;;
			*-)
				if test $arg -ge ${selector%-}
				then
					include=$positive
				fi
				;;
			*-*)
				if test ${selector%%-*} -le $arg \
					&& test $arg -le ${selector#*-}
				then
					include=$positive
				fi
				;;
			*)
				if test $arg -eq $selector
				then
					include=$positive
				fi
				;;
		esac
	done

	test -n "$include"
}

maybe_teardown_verbose () {
	test -z "$verbose_only" && return
	exec 4>/dev/null 3>/dev/null
	verbose=
}

last_verbose=t
maybe_setup_verbose () {
	test -z "$verbose_only" && return
	if match_pattern_list $test_count $verbose_only
	then
		exec 4>&2 3>&1
		# Emit a delimiting blank line when going from
		# non-verbose to verbose.  Within verbose mode the
		# delimiter is printed by test_expect_*.  The choice
		# of the initial $last_verbose is such that before
		# test 1, we do not print it.
		test -z "$last_verbose" && echo >&3 ""
		verbose=t
	else
		exec 4>/dev/null 3>/dev/null
		verbose=
	fi
	last_verbose=$verbose
}

maybe_teardown_valgrind () {
	test -z "$PROJFS_VALGRIND" && return
	PROJFS_VALGRIND_ENABLED=
}

maybe_setup_valgrind () {
	test -z "$PROJFS_VALGRIND" && return
	if test -z "$valgrind_only"
	then
		PROJFS_VALGRIND_ENABLED=t
		return
	fi
	PROJFS_VALGRIND_ENABLED=
	if match_pattern_list $test_count $valgrind_only
	then
		PROJFS_VALGRIND_ENABLED=t
	fi
}

want_trace () {
	test "$trace" = t && {
		test "$verbose" = t || test "$verbose_log" = t
	}
}

# This is a separate function because some tests use
# "return" to end a test_expect_success block early
# (and we want to make sure we run any cleanup like
# "set +x").
test_eval_inner_ () {
	# Do not add anything extra (including LF) after '$*'
	eval "
		want_trace && set -x
		$*"
}

test_eval_ () {
	# If "-x" tracing is in effect, then we want to avoid polluting stderr
	# with non-test commands. But once in "set -x" mode, we cannot prevent
	# the shell from printing the "set +x" to turn it off (nor the saving
	# of $? before that). But we can make sure that the output goes to
	# /dev/null.
	#
	# There are a few subtleties here:
	#
	#   - we have to redirect descriptor 4 in addition to 2, to cover
	#     BASH_XTRACEFD
	#
	#   - the actual eval has to come before the redirection block (since
	#     it needs to see descriptor 4 to set up its stderr)
	#
	#   - likewise, any error message we print must be outside the block to
	#     access descriptor 4
	#
	#   - checking $? has to come immediately after the eval, but it must
	#     be _inside_ the block to avoid polluting the "set -x" output
	#

	test_eval_inner_ "$@" </dev/null >&3 2>&4
	{
		test_eval_ret_=$?
		if want_trace
		then
			set +x
		fi
	} 2>/dev/null 4>&2

	if test "$test_eval_ret_" != 0 && want_trace
	then
		say_color error >&4 "error: last command exited with \$?=$test_eval_ret_"
	fi
	return $test_eval_ret_
}

test_run_ () {
	test_cleanup=:
	expecting_failure=$2

	if test "${PROJFS_TEST_CHAIN_LINT:-1}" != 0; then
		# turn off tracing for this test-eval, as it simply creates
		# confusing noise in the "-x" output
		trace_tmp=$trace
		trace=
		# 117 is magic because it is unlikely to match the exit
		# code of other programs
		if $(printf '%s\n' "$1" | $SED -f "$PROJFS_BUILD_DIR/t/chainlint.sed" | grep -q '?![A-Z][A-Z]*?!') ||
			test "OK-117" != "$(test_eval_ "(exit 117) && $1${LF}${LF}echo OK-\$?" 3>&1)"
		then
			BUG "broken &&-chain or run-away HERE-DOC: $1"
		fi
		trace=$trace_tmp
	fi

	setup_malloc_check
	test_eval_ "$1"
	eval_ret=$?
	teardown_malloc_check

	if test -z "$immediate" || test $eval_ret = 0 ||
	   test -n "$expecting_failure" && test "$test_cleanup" != ":"
	then
		setup_malloc_check
		test_eval_ "$test_cleanup"
		teardown_malloc_check
	fi
	if test "$verbose" = "t" && test -n "$HARNESS_ACTIVE"
	then
		echo ""
	fi
	return "$eval_ret"
}

test_start_ () {
	test_count=$(($test_count+1))
	maybe_setup_verbose
	maybe_setup_valgrind
}

test_finish_ () {
	echo >&3 ""
	maybe_teardown_valgrind
	maybe_teardown_verbose
}

test_skip () {
	to_skip=
	skipped_reason=
	if match_pattern_list $this_test.$test_count $PROJFS_SKIP_TESTS
	then
		to_skip=t
		skipped_reason="PROJFS_SKIP_TESTS"
	fi
	if test -z "$to_skip" && test -n "$test_prereq" &&
	   ! test_have_prereq "$test_prereq"
	then
		to_skip=t

		of_prereq=
		if test "$missing_prereq" != "$test_prereq"
		then
			of_prereq=" of $test_prereq"
		fi
		skipped_reason="missing $missing_prereq${of_prereq}"
	fi
	if test -z "$to_skip" && test -n "$run_list" &&
		! match_test_selector_list '--run' $test_count "$run_list"
	then
		to_skip=t
		skipped_reason="--run"
	fi

	case "$to_skip" in
	t)
		say_color skip >&3 "skipping test: $@"
		say_color skip "ok $test_count # skip $1 ($skipped_reason)"
		: true
		;;
	*)
		false
		;;
	esac
}

# stub; perf-lib overrides it
test_at_end_hook_ () {
	:
}

test_done () {
	PROJFS_EXIT_OK=t

	if test "$test_fixed" != 0
	then
		say_color error "# $test_fixed known breakage(s) vanished; please update test(s)"
	fi
	if test "$test_broken" != 0
	then
		say_color warn "# still have $test_broken known breakage(s)"
	fi
	if test "$test_broken" != 0 || test "$test_fixed" != 0
	then
		test_remaining=$(( $test_count - $test_broken - $test_fixed ))
		msg="remaining $test_remaining test(s)"
	else
		test_remaining=$test_count
		msg="$test_count test(s)"
	fi
	case "$test_failure" in
	0)
		if test $test_external_has_tap -eq 0
		then
			if test $test_remaining -gt 0
			then
				say_color pass "# passed all $msg"
			fi

			# Maybe print SKIP message
			test -z "$skip_all" || skip_all="# SKIP $skip_all"
			case "$test_count" in
			0)
				say "1..$test_count${skip_all:+ $skip_all}"
				;;
			*)
				test -z "$skip_all" ||
				say_color warn "$skip_all"
				say "1..$test_count"
				;;
			esac
		fi

		if test -z "$debug"
		then
			test -d "$TRASH_DIRECTORY" ||
			error "Tests passed but trash directory already removed before test cleanup; aborting"

			cd "$TRASH_DIRECTORY/.." &&
			rm -fr "$TRASH_DIRECTORY" ||
			error "Tests passed but test cleanup failed; aborting"
		fi
		test_at_end_hook_

		exit 0 ;;

	*)
		if test $test_external_has_tap -eq 0
		then
			say_color error "# failed $test_failure among $msg"
			say "1..$test_count"
		fi

		exit 1 ;;

	esac
}

# TODO: revise as needed if using valgrind; see git/git/t/valgrind
if test -n "$valgrind"
then
	make_symlink () {
		test -h "$2" &&
		test "$1" = "$(readlink "$2")" || {
			# be super paranoid
			if mkdir "$2".lock
			then
				rm -f "$2" &&
				ln -s "$1" "$2" &&
				rm -r "$2".lock
			else
				while test -d "$2".lock
				do
					say "Waiting for lock on $2."
					sleep 1
				done
			fi
		}
	}

	make_valgrind_symlink () {
		base=$(basename "$1")
		symlink_target="$PROJFS_BUILD_DIR/t/$base"
		# create the link, or replace it if it is out of date
		make_symlink "$symlink_target" "$PROJFS_VALGRIND/bin/$base" \
			|| exit
	}

	# override all projfs executables in TEST_DIRECTORY/..
	PROJFS_VALGRIND=$TEST_DIRECTORY/valgrind
	$MKDIR_P "$PROJFS_VALGRIND"/bin
	# TODO: which executables need to be overridden?
	for file in $PROJFS_BUILD_DIR/t/test_handlers \
		    $PROJFS_BUILD_DIR/t/test_simple \
		    $PROJFS_BUILD_DIR/t/wait_mount
	do
		make_valgrind_symlink $file
	done

	PATH=$PROJFS_VALGRIND/bin:$PATH
	export PATH

	export PROJFS_VALGRIND
	PROJFS_VALGRIND_MODE="$valgrind"
	export PROJFS_VALGRIND_MODE
	PROJFS_VALGRIND_ENABLED=t
	test -n "$valgrind_only" && PROJFS_VALGRIND_ENABLED=
	export PROJFS_VALGRIND_ENABLED
fi

if test -z "$PROJFS_TEST_CMP"
then
	if test -n "$PROJFS_TEST_CMP_USE_COPIED_CONTEXT"
	then
		PROJFS_TEST_CMP="$DIFF -c"
	else
		PROJFS_TEST_CMP="$DIFF -u"
	fi
fi

# Test mount points
TRASH_DIRECTORY="test-mounts/$(basename "$0" .t)"
test -n "$root" && TRASH_DIRECTORY="$root/$TRASH_DIRECTORY"
case "$TRASH_DIRECTORY" in
/*) ;; # absolute path is good
 *) TRASH_DIRECTORY="$TEST_OUTPUT_DIRECTORY/$TRASH_DIRECTORY" ;;
esac
rm -fr "$TRASH_DIRECTORY" || {
	PROJFS_EXIT_OK=t
	echo >&5 "FATAL: Cannot prepare test area"
	exit 1
}

# Clean and exit if requested
if test -n "$clean"
then
	rm -fr $(dirname "$TRASH_DIRECTORY")
	if test -n "$cleanall"
	then
		rm -fr "$TEST_OUTPUT_DIRECTORY/test-output"
	fi
	PROJFS_EXIT_OK=t
	exit 0
fi

$MKDIR_P "$TRASH_DIRECTORY"

HOME="$TRASH_DIRECTORY"
export HOME

# Use -P to resolve symlinks in our working directory so that the cwd
# in subprocesses like projfs equals our $PWD (for pathname comparisons).
cd -P "$TRASH_DIRECTORY" || exit 1

this_test=${0##*/}
this_test=${this_test%%-*}
if match_pattern_list "$this_test" $PROJFS_SKIP_TESTS
then
	say_color info >&3 "skipping test $this_test altogether"
	skip_all="skip all tests in $this_test"
	test_done
fi

( COLUMNS=1 && test $COLUMNS = 1 ) && test_set_prereq COLUMNS_CAN_BE_1

test_lazy_prereq PIPE '
	# test whether the filesystem supports FIFOs
	rm -f testfifo && mkfifo testfifo
'

test_lazy_prereq SYMLINKS '
	# test whether the filesystem supports symbolic links
	ln -s x y && test -h y
'

test_lazy_prereq NOT_ROOT '
	uid=$(id -u) &&
	test "$uid" != 0
'

# SANITY is about "can you correctly predict what the filesystem would
# do by only looking at the permission bits of the files and
# directories?"  A typical example of !SANITY is running the test
# suite as root, where a test may expect "chmod -r file && cat file"
# to fail because file is supposed to be unreadable after a successful
# chmod.  In an environment (i.e. combination of what filesystem is
# being used and who is running the tests) that lacks SANITY, you may
# be able to delete or create a file when the containing directory
# doesn't have write permissions, or access a file even if the
# containing directory doesn't have read or execute permissions.

test_lazy_prereq SANITY '
	mkdir SANETESTD.1 SANETESTD.2 &&

	chmod +w SANETESTD.1 SANETESTD.2 &&
	>SANETESTD.1/x 2>SANETESTD.2/x &&
	chmod -w SANETESTD.1 &&
	chmod -r SANETESTD.1/x &&
	chmod -rx SANETESTD.2 ||
	BUG "cannot prepare SANETESTD"

	! test -r SANETESTD.1/x &&
	! rm SANETESTD.1/x && ! test -f SANETESTD.2/x
	status=$?

	chmod +rwx SANETESTD.1 SANETESTD.2 &&
	rm -rf SANETESTD.1 SANETESTD.2 ||
	BUG "cannot clean SANETESTD"
	return $status
'

