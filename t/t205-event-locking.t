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

test_description='projfs file operation locking tests

Check that projfs file operation notification events are issued serially for a
given path.
'

. ./test-lib.sh

projfs_start test_handlers source target --timeout 1 --lock-file lock || exit 1

# wait_mount will trigger a projection, so we need to reset it to empty
setfattr -n user.projection.empty -v 0x01 source

test_expect_success 'test concurrent access does not trigger failure' '
	projfs_run_twice ls target
'

projfs_stop || exit 1

test_expect_success 'check no event error messages' '
	test_must_be_empty test_handlers.err
'

test_done

