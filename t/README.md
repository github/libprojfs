# libprojfs Test Suite

Both this [README](README.md) and the shell test suite libraries used in this
project are derived in large part from those written for the [Git][git]
version control system.  Please see the [NOTICE](../NOTICE) file
distributed with this library for additional information regarding copyright
ownership, and the [`git/t` source][git-tests] for further reading.

## Running Tests

The easiest way to run tests is to say `make test` (or `make check`, which
is equivalent).  This runs all the tests, using the
[Automake][automake-tests] TAP test harness:
```
PASS: t000-mirror-read.t 1 - create source tree
PASS: t000-mirror-read.t 2 - check target tree structure
PASS: t000-mirror-read.t 3 - check target tree content
...
============================================================================
Testsuite summary for libprojfs 0.1
============================================================================
# TOTAL: 42
# PASS:  27
# SKIP:  5
# XFAIL: 10
# FAIL:  0
# XPASS: 0
# ERROR: 0
============================================================================
```

Since the tests all output [TAP][tap] they can be run with any TAP harness.
If [`prove(1)`][prove] is available, it can be run using `make prove`:
```
./t000-mirror-read.t ...... ok
./t001-mirror-mkdir.t ..... ok
./t002-mirror-write.t ..... ok
...
All tests successful.
Files=10, Tests=42,  1 wallclock secs ( ... )
Result: PASS
```

### Test Harness Options

Both TAP harnesses come with a variety of options, although those
offered by the Automake harness do not have many obvious uses.

The (somewhat limited) set of options available for the default
Automake TAP harness are as follows:
```
[--color-tests {yes|no}]
[--comments|--no-comments]
[--diagnostic-string <string>]
[--expect-failure {yes|no}]
[--ignore-exit]
[--merge|--no-merge]
```

These Automake TAP options may be passed using `LOG_DRIVER_FLAGS`;
for example:
```
$ LOG_DRIVER_FLAGS='--comments --merge' make test
```
See [Command-line arguments for test drivers][automake-cmd]
and [Use TAP with the Automake test harness][automake-tap] for more details.

By contrast, the options available to `prove` are much richer and
potentially more useful, and may be passed using `PROJFS_PROVE_OPTS`;
for example:
```
$ PROJFS_PROVE_OPTS='--timer --jobs 4' make prove
```
See [`man 1 prove`][prove-man] for more details.

### Re-Running Failed Tests

Both TAP harnesses (Automake's and `prove`) offer the ability to
re-run only selected tests, particularly those tests which failed
in a prior run and which may now pass.

The default Automake harness can be executed in this mode by running
`make recheck`, which will examine the metadata in any per-script `t/t*.trs`
files left from a previous `make check` (or `make test`) to determine
which tests need to be run again.

The `prove` harness can achieve the same result when using its `--state`
option.  For example, to run only those tests which failed (assuming
the same command was used at least once before so `prove` has saved
its state):
```
$ PROJFS_PROVE_OPTS='--state=failed,save' make prove
```

### Running Individual Tests

You can also run each test individually from command line (without using
either TAP harness), like this:
```
$ ./t000-mirror-read.t 
ok 1 - create source tree
ok 2 - check target tree structure
ok 3 - check target tree content
# passed all 3 test(s)
1..3
```

### Test Options

In addition to the options available for the TAP harnesses, the tests
themselves accept a number of options, which may be supplied directly
as command-line arguments when running tests individually, or by setting
the `PROJFS_TEST_OPTS` environment variable before running `make test`
or `make check`.

To check whether all test scripts have correctly chained commands using `&&`,
use `--chain-lint`:
```
$ PROJFS_TEST_OPTS=--chain-lint make test
```

To log per-script exit codes and verbose output, including shell commands,
into `*.exit` and `*.out` files under `t/test-output/`:
```
$ PROJFS_TEST_OPTS='-V -x' make test
```

To run tests individually with verbose output, including shell commands:
```
$ ./t000-mirror-read.t -x
```

The full set of available options is described below.

* `-v` or `--verbose`:

  This makes the test more verbose.  Specifically, the `test_*()` commands
  being invoked and their results are output to stdout.  Note that this
  option is *not* available when using a TAP harness (e.g., when running
  `make test` or `make prove`); see `-V` for the alternative.

* `--verbose-only=<test-selector>`:

  Like `--verbose` but the effect is limited to tests whose numbers
  match `<test-selector>`.  Note that this option is *not* available when
  using a TAP harness.

  Test numbers are matched against the running count of `test_*()`
  commands within each script.  The syntax of the `<test-selector>`
  pattern is the same as for the `--run` option, and is described in
  more detail in the [Skipping Tests](#skipping-tests) section below.

* `--tee`:

  In addition to printing the test output to the terminal,
  write it to files named `t/test-results/<test-name>.t.out`.
  As the output filenames depend on the test scripts' filenames,
  it is safe to run the tests in parallel when using this option.

  The exit codes from each test script are also written into files
  named `t/test-results/<test-name>.t.exit`.

* `-V` or `--verbose-log`:

  Write verbose output to the same logfile as `--tee`, but do
  *not* write it to stdout.  Unlike `--tee --verbose`, this option
  is safe to use when stdout is being consumed by a TAP parser
  like `prove`.  Implies both `--tee` and `--verbose`.

* `-x`:

  Turn on shell tracing (i.e., `set -x`) during the tests
  themselves.  Implies `--verbose` unless `--verbose-log` is set.
  Note that use with `--verbose-log` is supported but short options
  cannot be combined; that is, use `-V -x`, not `-Vx`.

  Will be ignored in test scripts which set the variable `test_untraceable`
  to a non-empty value, unless run with a Bash version supporting
  `BASH_XTRACEFD`, i.e., v4.1 or later.

* `-q` or `--quiet`:

  Do not print individual `ok`/`not ok` per-test output to stdout;
  only print the summary of test results from within a test script.
  Ignored when running under a TAP harness.

* `--no-color`:

  Do not use color when printing the summary of test results from
  within a test script.  Not applicable when running under a TAP
  harness.

* `-d` or `--debug`:

  This may help the person who is developing a new test.
  It causes any command defined with `test_debug` to run.
  The "trash" directory (used to store all temporary data
  during testing under `t/test-mounts/<test-name>`) is not deleted even if
  there are no failed tests so that you can inspect its contents after
  the test finished.

* `-i` or `--immediate`:

  This causes test scripts to immediately exit upon the first
  failed test.  Cleanup commands requested with `test_when_finished` are
  not executed if the test failed, in order to keep the state for
  inspection by the tester to diagnose the bug.

  In particular, use of this option may leave the projected filesystem
  still mounted, and so is not recommended for use when running multiple
  tests under a TAP harness, as manual `umount` operations may be
  required to remove "leftover" filesystem mounts.

* `-r <test-selector>` or `--run=<test-selector>`:

  Run only the subset of tests indicated by `<test-selector>` from within
  each test script.  See the section [Skipping Tests](#skipping-tests)
  below for the `<test-selector>` syntax.

* `--root=<directory>`:

  Create the "trash" directories used to store all temporary data,
  including any temporary filesystem mounts, under `<directory>`
  instead of the `t/` directory.

  Using this option with a RAM-based filesystem (such as tmpfs)
  can massively speed up the test suite, but note that even after
  a successful test run, an empty `<directory>/test-mounts/` subdirectory
  may be left behind.

* `--chain-lint` (and `--no-chain-lint`):

  If `--chain-lint` is enabled, check each test script to make sure that
  it properly "`&&`-chains" all commands (so that a failure in the middle
  does not go unnoticed by the final exit code of the script).
  This check is performed in addition to running the tests themselves.

  You may also enable or disable this feature by setting the
  `PROJFS_TEST_CHAIN_LINT` environment variable to `1` or `0`,
  respectively.

* `--valgrind=<tool>` and `--valgrind-only=<test-selector>`:

  Not available yet.

## Skipping Tests

As described above, the `--run` and `--verbose-only` test options
accept a `<test-selector>` which identifies the specific tests to be
executed within one or more test scripts.  

The `<test-selector>` syntax is a list of individual test numbers or
ranges, with an optional negation prefix, that defines what tests in
a test suite to include in the run.

A range is two numbers separated with a dash and matches a range of
tests with both ends been included.  You may omit the first or the
second number to mean "from the first test" or "up to the very last test"
respectively.

Optional prefix of '!' means that the test or a range of tests
should be excluded from the run.

If `--run` or `--verbose-only` starts with an unprefixed number or range,
then the initial set of tests to run is empty.  If the first item starts
with a `!` prefix, then all the tests are added to the initial set.

After the initial set is determined, each test number or range in the
`<test-selector>` list is added to the set of tests to run (or excluded
from the set, in the case of `!`), parsing from left to right through the
list.  Individual numbers or ranges within the list may be separated
either by a space or a comma.

For example, to run only tests up to a specific test (8), one
could do this:
```
$ ./t200-event-ok.t -r -8
```

A common case is to run several setup tests (1, 2, 3) and then a
specific test (9) that relies on that setup:
```
$ ./t200-event-ok.t --run='-3,9'
```

As noted above, the test set is built by going through the items
from left to right, so this:
```
$ ./t200-event-ok.t --run='1-4 !3'
```
will run tests 1, 2, and 4 only, not 3.  Items that come later have
higher precedence.  It means that this:
```
$ ./t200-event-ok.t --run='!3 2-4'
```
would just run all tests starting from 1, including 3, because
the leading negated item (`!3`) causes the initial set to be defined as
all the tests, from which test 3 is then removed, and then test 3 is
added back again as part of the range 2-4.

You may use negation with ranges.  The following will run all
tests in the test script except from 3 up to 6:
```
$ ./t200-event-ok.t --run='!3-6'
```

Some tests in a test script rely on the previous tests performing
certain actions; specifically some tests are designated as a
"setup" test, so you cannot _arbitrarily_ disable one test and
then expect the rest to function correctly.

The `--run` option is typically most useful when you want to focus on
a specific test and know what setup is needed for it, or when you want
to run everything up to a certain test.  And while `--run` can be passed
via the `PROJFS_TEST_OPTS` environment variable, note that when combined
with a TAP harness, the `<test-selector>` list will apply to _all_ test
scripts, which is rarely what is desired.

Alternatively, the `PROJFS_SKIP_TESTS` environment variable may be
used with a TAP harness (or individual tests), and it has the
advantage that test scripts may also be specifically identified.

The syntax of `PROJFS_SKIP_TESTS` is a space-separated
list of patterns which identify the tests to skip,
and either can match the `t[0-9]{3}` part to skip the whole
test script, or `t[0-9]{3}` followed by `.$number` to identify
a test within a test script.  For example:
```
$ PROJFS_SKIP_TESTS=t200.8 make test
```
or:
```
$ PROJFS_SKIP_TESTS='t[1-4]?? t000.[1-3]' make test
```

## Writing Tests

Each test script is written as a shell script, and should start
with the standard `#!/bin/sh`, and an
assignment to variable `test_description`, like this:
``` shell
#!/bin/sh

test_description='xxx test (option --frotz)

This test tries the --frotz option on a projfs mount.
'
```

### Starting Tests

After assigning `test_description`, the test script should source
[`test-lib.sh`](test-lib.sh) like this:
``` shell
. ./test-lib.sh
```

This test harness library does the following things:

* If the script is invoked with command line argument `--help`
  (or `-h`), it prints the `test_description` and exits.

* Defines standard test helper functions for your scripts to
  use.  These functions are designed to make all scripts behave
  consistently when command line arguments like `--verbose` (or `-v`),
  `--debug` (or `-d`), and `--immediate` (or `-i`) are given.

* Creates an empty trash directory under `t/test-mounts` and
  and [`chdir(2)`][chdir] into it.  This directory is
  `t/test-mounts/<test-name>`, with `t/` subject to change by the
  `--root` option documented above.

In most cases, tests should then call the `projfs_start` function
to execute a test mount helper program such as
[`test_projfs_handlers.c`](test_projfs_handlers.c).

The mount helper normally takes at least two arguments; these should
be directory names which will be used to create a temporary source
(lower) directory and a target (projected) mount of that directory,
both within the test script's trash directory.  For example, given
`source` and `target` as arguments, the temporary directories and
mount point created would be
`t/test-mounts/<test-name>/source` and `t/test-mounts/<test-name>/target`.

The `projfs_start` function should be called after the test library is
loaded:
``` shell
. ./test-lib.sh

projfs_start test_projfs_handlers source target || exit 1
```

The mount helper program's ID will be recorded so it can be stopped
by the `projfs_stop` function after all tests are complete; see the
next section for details.

### Ending Tests

Your script will be a sequence of tests, using helper functions
from the test harness library.  At the end of the script, call
`test_done` to signal to the harness that there will be no further
test output.

Assuming your test script called the `projfs_start` function, it must
also call `projfs_stop` just prior to calling `test_done`, like this:
``` shell
projfs_stop || exit 1
 
test_done
```

The `projfs_stop` function will signal the test mount helper program
to exit and then wait for its mount to be un-mounted.

### Do's & Don'ts

Here are a few examples of things you probably should and shouldn't do
when writing tests.

Here are the "do's":

* Put all code inside `test_expect_success` and other assertions.

  Even code that isn't a test per se, but merely some setup code
  should be inside a test assertion.

* Chain your test assertions.

  Write test code like this:
  ``` shell
  echo foo > source/bar &&
  test_cmp target/bar ... &&
  test ...
  ```
  Instead of:
  ``` shell
  echo foo > source/bar
  test_cmp target/bar ...
  test ...
  ```

  That way all of the commands in your tests will succeed or fail.
  If you must ignore the return value of something, consider prepending
  the command with `test_might_fail` or `test_must_fail`, or using a
  helper function (e.g., use `sane_unset` instead of `unset`, in order
  to avoid issues caused by non-portable return values in the case when
  a variable was already unset).

* When a test checks for an absolute path, construct the expected value
  using `$(pwd)` rather than `$PWD`, `$TEST_DIRECTORY`, or
  `$TRASH_DIRECTORY`.  While not strictly necessary on Linux, this
  aids readability and consistency across scripts.

* Remember that inside the `<script>` part, the standard output and
  standard error streams are discarded, and the test harness only
  reports `ok` or `not ok` to the end user running the tests.  However,
  when using `--verbose` or `--verbose-log`, the standard output and error
  streams are captured to help debug the tests.

  See the [Test Harness Library](#test-harness-library) section below
  for a description of the `<script>` part of a test helper function.

And here are the "don'ts":

* Don't `exit` within the `<script>` part.

  The harness will catch this as a programming error of the test.
  Use `test_done` instead if you need to stop the tests early (see
  [Programmatically Skipping Tests](#programmatically-skipping-tests)
  section below).

* Don't use `! cmd ...` when you want to make sure a command
  exits with failure in a controlled way.  Instead,
  use `test_must_fail cmd ...`.  This will signal a failure if the
  commands dies in an unexpected way (e.g., a segfault).

  On the other hand, don't use `test_must_fail` for running regular
  platform OS commands; just use `! cmd ...`.  We are not in the business
  of verifying that the world given to us works sanely.

* Don't feed the output of a command to a pipe, as in:
  ``` shell
  cmd ... |
  xargs -n 1 basename |
  grep foo
  ```
  which will discard the command's exit code and may mask a crash.
  In the above example, all exit codes are ignored except those from
  `grep`.

  Instead, write the output of the command to a temporary
  file with `>` or assign it to a variable with `x=$(cmd ..)` rather
  than pipe it.

* Don't use command substitution in a way that discards a command's exit
  code.  When assigning to a variable, the exit code is not discarded,
  e.g.:
  ``` shell
  x=$(cmd ...) &&
  ...
  ```
  is OK because a crash in `cmd ...` will cause the `&&` chain
  to fail, but:
  ``` shell
  test "foo" = "$(cmd ...)"
  ```
  is not OK and a crash could go undetected.

* Don't use `sh` without spelling it as `$SHELL_PATH`.  Although not
  strictly necessary on Linux, doing so aids readability and consistency
  across scripts.

* Don't `cd` around in tests.  It is not sufficient to `chdir` to
  somewhere and then `chdir` back to the original location later in
  the test, as any intermediate step can fail and abort the test,
  causing the next test to start in an unexpected directory.  Do so
  inside a subshell if necessary, e.g.:
  ``` shell
  mkdir foo &&
  (
          cd foo &&
          cmd ... &&
          test ...
  ) &&
  rm -rf foo
  ```

* Don't save and verify the standard error of compound commands, i.e.,
  group commands, subshells, and shell functions (except test helper
  functions like `test_must_fail`) like this:
  ``` shell
  ( cd dir && cmd ... ) 2>error &&
  test_cmp expect error
  ```
  When running the test with `-x` tracing flag, then the trace of commands
  executed in the compound command will be included in standard error
  as well, quite possibly throwing off the subsequent checks examining
  the output.  Instead, save only the relevant command's standard
  error:
  ``` shell
  ( cd dir && cmd ... 2>../error ) &&
  test_cmp expect error
  ```

* Don't break the TAP output.

  The raw output from your test may be interpreted by a TAP harness.  TAP
  harnesses will ignore everything they don't know about, but don't step
  on their toes in these areas:

  * Don't print lines like `$x..$y` where `$x` and `$y` are integers.

  * Don't print lines that begin with `ok` or `not ok`.

  TAP harnesses expect a line that begins with either `ok` and `not
  ok` to signal a test passed or failed (and our libraries already
  produce such lines), so your script shouldn't emit such lines to
  their output.

  You can glean some further possible issues from the
  [TAP grammar][tap-grammar] but the best indication is to just run the
  tests with `make prove` as `prove` will complain if anything is amiss.

### Programmatically Skipping Tests

If you need to skip tests programmatically based on some condition
detected at test run time, you should do so by using the three-arg form
of the `test_*` functions (see the
[Test Harness Library](#test-harness-library) section below), e.g.:
``` shell
test_expect_success PIPE 'I need pipes' '
        echo foo | cmd ... &&
        ...
'
```

The advantage of skipping tests this way is that platforms that don't
have the PIPE prerequisite flag set will get an indication of how
many tests they're missing.

If the test code is too hairy for that (i.e., does a lot of setup work
outside test assertions) you can also skip all remaining tests by
setting `skip_all` and immediately call `test_done`:
``` shell
if ! test_have_prereq PIPE
then
        skip_all='skipping pipe tests, I/O pipes not available'
        test_done
fi
```

The string you give to `skip_all` will be used as an explanation for why
the test was skipped.

The set of pre-defined prerequisite flags is relatively limited
(see the [Prerequisites](#prerequisites) section below),
but additional flags may be defined a test run time using the
`test_set_prereq` function, described below.

### Test Harness Library

There are a handful of helper functions defined in the test harness
library for your script to use.

* `test_expect_success [<prereq>] <message> <script>`

  Usually takes two strings as parameters, and evaluates the
  `<script>`.  If it yields success, test is considered
  successful.  The `<message>` should state what it is testing.

  Example:
  ``` shell
  test_expect_success 'check command output' '
          cmd ... >output &&
          test_cmp expect output
  '
  ```

  If you supply three parameters the first will be taken to be a
  prerequisite; see the `test_set_prereq` and `test_have_prereq`
  documentation below:
  ``` shell
  test_expect_success SYMLINKS 'check symlinks' '
          ln -s foo bar &&
          test_cmp foo bar
  '
  ```

  You can also supply a comma-separated list of prerequisites, in the
  rare case where your test depends on more than one:
  ``` shell
  test_expect_success PIPE,SYMLINKS 'check output to symlink' '
          cmd ... >output &&
          ln -s output sym &&
          test_cmp expect sym
  '
  ```

* `test_expect_failure [<prereq>] <message> <script>`

  This is _not_ the opposite of `test_expect_success`, but is used
  to mark a test that demonstrates a known breakage.  Unlike
  the usual `test_expect_success` tests, which say `ok` on
  success and `FAIL` on failure, this will say `FIXED` on
  success and `still broken` on failure.

  Failures from these tests won't cause the test script to stop,
  even if the `-i` (or `--immediate`) option was specified.

  Like `test_expect_success` this function can optionally use a three
  argument invocation with a prerequisite as the first argument.

* `test_debug <script>`

  This takes a single argument, `<script>`, and evaluates it only
  when the test script is started with the `-d` (or `--debug`)
  option.  This is primarily meant for use during the
  development of a new test script.

* `debug <command>`

  Run a command inside a debugger.  This is primarily meant for
  use when debugging a failing test script.

* `test_done`

  Your test script must have `test_done` at the end.  Its purpose
  is to summarize successes and failures in the test script and
  exit with an appropriate error code.

* `test_set_prereq <prereq>`

  Set a test prerequisite to be used later with `test_have_prereq`. The
  test library will set some prerequisites for you; see the
  [Prerequisites](#prerequisites) section below for a full list of these.

  Others you can set yourself, and then use later with either
  `test_have_prereq` directly or a three-argument invocation of
  `test_expect_success` and `test_expect_failure`.

* `test_have_prereq <prereq>`

  Check if we have a prerequisite previously set with `test_set_prereq`.
  The most common way to use this explicitly (as opposed to the
  implicit use when an third argument is passed to `test_expect_*`) is to
  skip all the tests at the start of the test script if we don't have some
  essential prerequisite:
  ``` shell
  if ! test_have_prereq PIPE
  then
          skip_all='skipping pipe tests, I/O pipes not available'
          test_done
  fi
  ```

* `test_external [<prereq>] <message> <external> <script>`

  Execute a `<script>` with an `<external>` interpreter (like Python).

  If the test outputs its own TAP-formatted results then you should set
  the `test_external_has_tap` variable to a non-zero value before calling
  the first `test_external*` function, e.g.:
  ``` shell
  # The external test will output its own plan
  test_external_has_tap=1
  test_external 'check python output' python "$TEST_DIRECTORY"/t000/test.py
  ```

* `test_external_without_stderr [<prereq>] <message> <external> <script>`

  Like `test_external` but will fail if there's any output on stderr,
  instead of checking the exit code.

* `test_expect_code <exit-code> <command>`

  Run a command and ensure that it exits with the given exit code.
  For example:
  ``` shell
  test_expect_success 'check command exit code' '
          echo foo > foo &&
          echo bar > bar &&
          test_expect_code 1 diff foo bar
          test_expect_code 2 diff foo
  '
  ```

* `test_must_fail [<options>] <command>`

  Run a command and ensure it fails in a controlled way.  Use
  this instead of `! <command>`.  When the command dies due to a
  segfault, `test_must_fail` diagnoses it as an error, whereas using
  `! <command>` would treat it as an expected failure, which could let
  a bug go unnoticed.

  Accepts the following options:

  * `ok=<signal-name>[,<signal-name>[,...]]`

    Don't treat an exit caused by the given signal as error.
    Multiple signals can be specified as a comma separated list.

    Currently recognized signal names are: `sigpipe` and `success`
    (but don't use `success`, use `test_might_fail` instead; see below).

* `test_might_fail [<options>] <command>`

  Similar to `test_must_fail`, but tolerates success, too.  Use this
  instead of `<command> || :` to catch failures due to segfaults.

  Accepts the same options as `test_must_fail`.

* `test_match_signal <expected> <actual>`

  Check whether the signal number in `<actual>` matches that in
  `<expected>`, accounting for shell signal number offsets.
  Both parameters should be given numerically.

* `test_env [<var>=<value> [...]] <command>`

  Run `<command>` in a subshell after setting any environment
  variables defined by the `<var>=<value>` parameters, e.g.:
  ``` shell
  test_env PATH=/tmp TRACE=true cmd ...
  ```

* `test_cmp <expected> <actual>`

  Check whether the content of the `<actual>` file matches the
  `<expected>` file.  This behaves like `cmp` but produces more
  helpful output when the test is run with `-v` or `-V` (or `--verbose`
  or `--verbose-log`) options.

* `test_cmp_bin <expected> <actual>`

  Check whether the binary content of the `<actual>` file matches the
  `<expected>` file, using `cmp`.

* `test_line_count <op> <length> <file>`

  Check whether a file has the length it is expected to, using an
  `<op>` test operator available to the [`test(1)`][test-man] command.
  For example:
  ``` shell
  test_write_lines 1 2 3 4 5 >foo &&
  test_line_count -gt 4 foo &&
  test_line_count -lt 6 foo &&
  test_line_count = 5 foo
  ```

* `test_path_is_file <path> [<diagnosis>]`\
  `test_path_is_dir <path> [<diagnosis>]`\
  `test_path_exists <path> [<diagnosis>]`\
  `test_path_is_missing <path> [<diagnosis>]`\
  `test_dir_is_empty <path>`\
  `test_must_be_empty <path>`

  Check if the named path is a file, if the named path is a
  directory, if the named path exists, if the named path does not exist,
  if the named path is an empty directory, or if the named path
  is an empty file, respectively, and fail otherwise, showing the
  `<diagnosis>` text where applicable.

* `test_when_finished <script>`

  Prepend `<script>` to a list of commands to run to clean up
  at the end of the current test.  If some clean-up command
  fails, the test will not pass.  For example:
  ``` shell
  test_expect_success 'test with cleanup' '
          mkdir foo &&
          test_when_finished "rm -fr foo" &&
          ( cd foo && ... )
  '
  ```

* `test_skip_or_die auto|true <message>`

  Exit the test script, either by skipping all remaining tests or by
  exiting with an error.  If the first argument is `auto` then skip
  all remaining tests; otherwise, if it is true, report an error.
  The `<message>` is output in either case.

* `test_tristate <var>`

  Normalize the value of the environment variable `<var>` to one
  of `auto`, `true`, or `false`.  This allows a test script to
  decide whether to perform a specific test based on a user-supplied
  environment variable, for example, to skip any large-file tests
  in the test suite:
  ```
  $ PROJFS_TEST_BIGFILES= make test
  ```

  If the user sets the variable `<var>` to an empty string or the
  value `false`, then `test_tristate <var>` will normalize the value
  to `false`.  If the variable is unset or has the value `auto`, the
  value is normalized to `auto`.  Any other value is normalized to `true`.

  After calling `test_tristate`, the test script can therefore decide
  whether to execute or skip a test based on the tri-state value in
  `<var>`, with `true` meaning "test", `false` meaning "do not test",
  and `auto` meaning "automatically decide".

* `test_write_lines <lines>`

  Write `<lines>` on standard output, one line per argument.
  Useful to prepare multi-line files in a compact form.  For example,
  ``` shell
  test_write_lines a b c d e f g >foo
  ```
  is a more compact equivalent of:
  ``` shell
  cat >foo <<-EOF
  a
  b
  c
  d
  e
  f
  g
  EOF
  ```

* `test_seq [<start>] <end>`

  Print a sequence of integers in increasing order from `<start>` to
  `<end>`, or from 1 to `<end>` if `<start>` is not supplied.

* `test_pause`

  This command is useful for writing and debugging tests, but should be
  removed before committing a new test.

  It halts the execution of the test and spawns a shell in the trash
  directory, allowing the developer to examine the state of the test
  at this point.  Exit the shell to continue the test.  Example:
  ``` shell
  test_expect_success 'test under development' '
          cmd ... >actual &&
          test_pause &&
          test_cmp expect actual
  '
  ```

### Prerequisites

These are the prerequisites that the test library pre-defines with
`test_have_prereq` for use with the `test_have_prereq` function.

See also the discussion of the `<prereq>` argument to the `test_*`
functions, in the [Test Harness Library](#test-harness-library) section
above.

You can also use `test_set_prereq` to define your own test
prerequisite flags.

* `PIPE`

  The filesystem we're on supports creation of FIFOs (named pipes)
  via [`mkfifo(1)`][mkfifo].

* `SYMLINKS`

  The filesystem we're on supports symbolic links.

* `NOT_ROOT`

  Test is not run by the super-user.

* `SANITY`

  Test is not run by the super-user, and an attempt to write to an
  unwritable file is expected to fail correctly.

[automake-cmd]: https://www.gnu.org/software/automake/manual/html_node/Command_002dline-arguments-for-test-drivers.html
[automake-tap]: https://www.gnu.org/software/automake/manual/html_node/Use-TAP-with-the-Automake-test-harness.html
[automake-tests]: https://www.gnu.org/software/automake/manual/html_node/Tests.html
[chdir]: http://man7.org/linux/man-pages/man2/chdir.2.html
[git]: https://git-scm.com/
[git-tests]: https://github.com/git/git/tree/master/t
[mkfifo]: http://man7.org/linux/man-pages/man1/mkfifo.1.html
[prove]: https://metacpan.org/pod/prove
[prove-man]: https://perldoc.perl.org/prove.html
[tap]: https://testanything.org/
[tap-grammar]: https://metacpan.org/pod/TAP::Parser::Grammar#TAP-GRAMMAR
[test-man]: http://man7.org/linux/man-pages/man1/test.1.html
