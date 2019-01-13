#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs VFS API file operation event tests

Check that projfs file operation notification and permission request
events are received and handled correctly through the VFS API.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_vfsapi_handlers source target || exit 1

projfs_event_printf vfs create_dir d1
test_expect_success 'test event handler on parent directory creation' '
	projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_event_printf vfs create_dir d1/d2
test_expect_success 'test event handler on nested directory creation' '
	projfs_event_exec mkdir target/d1/d2 &&
	test_path_is_dir target/d1/d2
'

# TODO: also use 'echo ... >' to exercise open() not create()

projfs_event_printf vfs create_file f1.txt
test_expect_success 'test event handler on top-level file creation' '
	projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf vfs create_file d1/d2/f2.txt
test_expect_success 'test event handler on nested file creation' '
	projfs_event_exec touch target/d1/d2/f2.txt &&
	test_path_is_file target/d1/d2/f2.txt
'

projfs_event_printf vfs delete_file d1/d2/f2.txt
test_expect_success 'test permission granted on nested file deletion' '
	projfs_event_exec rm target/d1/d2/f2.txt &&
	test_path_is_missing target/d1/d2/f2.txt
'

projfs_event_printf vfs delete_file f1.txt
test_expect_success 'test permission granted on top-level file deletion' '
	projfs_event_exec rm target/f1.txt &&
	test_path_is_missing target/f1.txt
'

projfs_event_printf vfs delete_dir d1/d2
test_expect_success 'test permission granted on nested directory deletion' '
	projfs_event_exec rmdir target/d1/d2 &&
	test_path_is_missing target/d1/d2
'

projfs_event_printf vfs delete_dir d1
test_expect_success 'test permission granted on parent directory deletion' '
	projfs_event_exec rmdir target/d1 &&
	test_path_is_missing target/d1
'

projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_vfsapi_handlers.log "$EVENT_LOG"
'

test_expect_success 'check no event error messages' '
	test_must_be_empty test_vfsapi_handlers.err
'

test_done

