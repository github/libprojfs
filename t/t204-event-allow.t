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

test_description='projfs file operation permission allowed tests

Check that projfs file operation permission requests respond to
explicit allowed responses from event handlers.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/test-lib-event.sh

projfs_start test_handlers source target --retval-file retval || exit 1
echo allow > retval

projfs_event_printf notify create_dir d1
test_expect_success 'test event handler on directory creation' '
	projfs_event_exec mkdir target/d1 &&
	test_path_is_dir target/d1
'

projfs_event_printf notify create_file f1.txt
projfs_event_printf notify close_file f1.txt
test_expect_success 'test event handler on file creation' '
	projfs_event_exec touch target/f1.txt &&
	test_path_is_file target/f1.txt
'

projfs_event_printf perm rename_dir d1 d1a
projfs_event_printf notify rename_dir d1 d1a
test_expect_success 'test event handler on directory rename' '
	projfs_event_exec mv target/d1 target/d1a &&
	test_path_is_missing target/d1 &&
	test_path_is_dir target/d1a
'

projfs_event_printf perm rename_file f1.txt f1a.txt
projfs_event_printf notify rename_file f1.txt f1a.txt
test_expect_success 'test event handler on file rename' '
	projfs_event_exec mv target/f1.txt target/f1a.txt &&
	test_path_is_missing target/f1.txt &&
	test_path_is_file target/f1a.txt
'

projfs_event_printf notify link_file f1a.txt l1a.txt
test_expect_success 'test event handler on file hard link' '
	projfs_event_exec ln target/f1a.txt target/l1a.txt &&
	test_path_is_file target/l1a.txt
'

projfs_event_printf perm delete_file f1a.txt
projfs_event_printf notify delete_file f1a.txt
test_expect_success 'test permission request allowed on file deletion' '
	projfs_event_exec rm target/f1a.txt &&
	test_path_is_missing target/f1a.txt
'

projfs_event_printf perm delete_dir d1a
projfs_event_printf notify delete_dir d1a
test_expect_success 'test permission request allowed on directory deletion' '
	projfs_event_exec rmdir target/d1a &&
	test_path_is_missing target/d1a
'

rm retval
projfs_stop || exit 1

test_expect_success 'check all event notifications' '
	test_cmp test_handlers.out "$EVENT_OUT"
'

test_expect_success 'check no unexpected error output' '
	test_must_be_empty test_handlers.err
'

test_done

