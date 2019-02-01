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

test_description='projfs filesystem mirroring link tests

Check that links function as expected.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create links' '
	echo text > target/file &&
	ln target/file target/link &&
	ls -li target &&
	ln -s file target/symlink
'
test_expect_success 'check link stat' '
	stat target/file >link.file.stat &&
	stat target/link >link.stat &&
	grep "Inode: " link.file.stat | awk "{print \$4}" >inode &&
	grep "Inode: $(cat inode)" link.stat
'

test_expect_success 'check symlink stat' '
	stat target/symlink >symlink.stat &&
	readlink target/symlink > symlink.read &&
	grep "^file$" symlink.read &&
	grep "symbolic link" symlink.stat &&
	grep "Access: (0777/lrwxrwxrwx)" symlink.stat
'

projfs_stop || exit 1

test_done

