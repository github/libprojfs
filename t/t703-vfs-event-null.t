#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs VFS API file operation permission denial tests

Check that projfs file operation permission requests respond to
denial responses caused by event handlers returning null through
the VFS API.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_vfsapi_handlers source target --retval null || exit 1

# TODO: test_vfsapi_handlers always returns EINVAL with --retval null, unlike
#	test_projfs_handlers, so mkdir sees a handler error; like t701.1,
#	we expect mkdir to create a dir despite the handler error and
#	regardless of mkdir's failure exit code
projfs_event_printf error EINVAL vfs create_dir d1
test_expect_success 'test event handler on directory creation' '
	test_must_fail projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

# TODO: test_vfsapi_handlers always returns EINVAL with --retval deny, unlike
#	test_projfs_handlers, so touch sees a handler error; like t701.2,
#	we expect touch to create a file despite the handler error and
#	to not report a failure exit code
projfs_event_printf error EINVAL vfs create_file f1.txt
test_expect_success 'test event handler on file creation' '
	test_might_fail projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf error EINVAL vfs delete_file f1.txt
test_expect_success 'test permission request denied on file deletion' '
	test_must_fail projfs_event_exec rm target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf error EINVAL vfs delete_dir d1
test_expect_success 'test permission request denied on directory deletion' '
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

