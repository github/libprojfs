#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs VFS API file operation event handler error tests

Check that projfs file operation notification and permission request
events respond to handler errors through the VFS API.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_vfsapi_handlers source target --retval EOutOfMemory || exit 1

# TODO: we expect mkdir to create a dir despite the handler error and
#	regardless of mkdir's failure exit code
projfs_event_printf error ENOMEM vfs create_dir d1
test_expect_success 'test event handler error on directory creation' '
	test_must_fail projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

# TODO: we expect touch to create a file despite the handler error and
#	to not report a failure exit code
projfs_event_printf error ENOMEM vfs create_file f1.txt
test_expect_success 'test event handler error on file creation' '
	test_might_fail projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf error ENOMEM vfs delete_file f1.txt
test_expect_success 'test event handler error on file deletion' '
	test_must_fail projfs_event_exec rm target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf error ENOMEM vfs delete_dir d1
test_expect_success 'test event handler error on directory deletion' '
	test_must_fail projfs_event_exec rmdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_vfsapi_handlers.log "$EVENT_LOG"
'

test_expect_success 'check all event error messages' '
	test_cmp test_vfsapi_handlers.err "$EVENT_ERR"
'

test_done

