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

test_description='projfs filesystem mirroring statfs tests

Check that the filesystem stat function as expected.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'check statfs' '
	stat -f target > stat &&
	grep "Type: fuseblk" stat &&
	grep "Blocks: Total: " stat | awk "{print \$3}" > stat.blocks.total &&
	grep "Inodes: Total: " stat | awk "{print \$3}" > stat.inodes.total &&
	stat -f . | grep "Blocks: Total: "`cat stat.blocks.total` &&
	stat -f . | grep "Inodes: Total: "`cat stat.inodes.total`
'

projfs_stop || exit 1

test_done

