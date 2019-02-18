#!/bin/sh
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

test_description='projfs file operation event tests

Check that projfs file operation notification and permission request
events are received and handled correctly.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_projfs_handlers source target --retval-file retval || exit 1
touch retval

projfs_event_printf notify create_dir d1
test_expect_success 'test event handler on parent directory creation' '
	projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_event_printf notify create_dir d1/d2
test_expect_success 'test event handler on nested directory creation' '
	projfs_event_exec mkdir target/d1/d2 &&
	test_path_is_dir target/d1/d2
'

# XXX: `touch` and `echo >` and `echo >>>` all issue `open(2)`; none issue
# `creat(2)`.  Probably need a helper tool to issue one or the other so we can
# test this precisely.
projfs_event_printf notify create_file f1.txt
test_expect_success 'test event handler on top-level file creation' '
	projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf notify create_file d1/d2/f2.txt
test_expect_success 'test event handler on nested file creation' '
	projfs_event_exec touch target/d1/d2/f2.txt &&
	test_path_is_file target/d1/d2/f2.txt
'

projfs_event_printf perm delete_file d1/d2/f2.txt
test_expect_success 'test permission granted on nested file deletion' '
	projfs_event_exec rm target/d1/d2/f2.txt &&
	test_path_is_missing target/d1/d2/f2.txt
'

projfs_event_printf perm delete_file f1.txt
test_expect_success 'test permission granted on top-level file deletion' '
	projfs_event_exec rm target/f1.txt &&
	test_path_is_missing target/f1.txt
'

projfs_event_printf perm delete_dir d1/d2
test_expect_success 'test permission granted on nested directory deletion' '
	projfs_event_exec rmdir target/d1/d2 &&
	test_path_is_missing target/d1/d2
'

projfs_event_printf perm delete_dir d1
test_expect_success 'test permission granted on parent directory deletion' '
	projfs_event_exec rmdir target/d1 &&
	test_path_is_missing target/d1
'

rm retval
projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_projfs_handlers.log "$EVENT_LOG"
'

test_expect_success 'check no event error messages' '
	test_must_be_empty test_projfs_handlers.err
'

test_done

