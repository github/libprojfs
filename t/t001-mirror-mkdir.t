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

test_description='projfs filesystem mirroring mkdir tests

Check that the filesystem directory creation operation (mkdir)
functions through a mirrored projfs mount.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create source tree' '
	mkdir source/d1 &&
	mkdir source/d1/d2
'

test_expect_success 'create target tree' '
	mkdir target/d3 &&
	mkdir target/d3/d4 &&
	mkdir target/d1/d5 &&
	mkdir target/d1/d5/d6 &&
	mkdir target/d1/d2/d7
'

test_expect_success 'check source tree' '
	find source >find.source &&
	sort find.source >sort.source &&
	test_cmp sort.source "$EXPECT_DIR/sort.source"
'
test_expect_success 'check target tree' '
	find target >find.target &&
	sort find.target >sort.target &&
	test_cmp sort.target "$EXPECT_DIR/sort.target"
'

projfs_stop || exit 1

test_done

