#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs file operation permission denial tests

Check that projfs file operation permission requests respond to
denial responses caused by event handlers returning null.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_projfs_handlers source target --retval null || exit 1

projfs_event_printf notify create_dir d1
test_expect_success 'test event handler on directory creation' '
	projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_event_printf notify create_file f1.txt
test_expect_success 'test event handler on file creation' '
	projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf perm delete_file f1.txt
test_expect_success 'test permission request denied on file deletion' '
	test_must_fail projfs_event_exec rm target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf perm delete_dir d1
test_expect_success 'test permission request denied on directory deletion' '
	test_must_fail projfs_event_exec rmdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_projfs_handlers.log "$EVENT_LOG"
'

test_expect_success 'check no event error messages' '
	test_must_be_empty test_projfs_handlers.err
'

test_done

