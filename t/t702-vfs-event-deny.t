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

test_description='projfs VFS API file operation permission denial tests

Check that projfs file operation permission requests respond to
denial responses from event handlers through the VFS API.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_vfsapi_handlers source target --retval-file retval || exit 1
echo deny > retval

# TODO: test_vfsapi_handlers always returns EPERM with --retval deny, unlike
#	test_projfs_handlers, so mkdir gets a handler error; like t701.1,
#	we expect mkdir to create a dir despite the handler error and
#	regardless of mkdir's failure exit code
projfs_event_printf error EPERM vfs create_dir d1
test_expect_success 'test event handler on directory creation' '
	test_must_fail projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

# TODO: test_vfsapi_handlers always returns EPERM with --retval deny, unlike
#	test_projfs_handlers, so touch sees a handler error; like t701.2,
#	we expect touch to create a file despite the handler error and
#	to not report a failure exit code
projfs_event_printf error EPERM vfs create_file f1.txt
test_expect_success 'test event handler on file creation' '
	projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf vfs delete_file f1.txt
test_expect_success 'test permission request denied on file deletion' '
	test_must_fail projfs_event_exec rm target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf vfs delete_dir d1
test_expect_success 'test permission request denied on directory deletion' '
	test_must_fail projfs_event_exec rmdir target/d1 &&
	test_path_is_dir target/d1
'

rm retval
projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_vfsapi_handlers.log "$EVENT_LOG"
'

test_expect_success 'check all event error messages' '
	test_cmp test_vfsapi_handlers.err "$EVENT_ERR"
'

test_done

