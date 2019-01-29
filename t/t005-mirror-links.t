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

test_description='projfs filesystem mirroring symlink tests

Check that symlinks function as expected.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create link' '
	ln -s file target/link
'

test_expect_success 'check stat' '
	stat target/link >stat &&
	grep "File: target/link -> file" stat &&
	grep "symbolic link" stat &&
	grep "Access: (0777/lrwxrwxrwx)" stat
'

test_expect_success 'check readlink' '
	readlink target/link >readlink &&
	test_cmp readlink "$EXPECT_DIR/readlink"
'

projfs_stop || exit 1

test_done

