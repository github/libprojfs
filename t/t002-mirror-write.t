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

test_description='projfs filesystem mirroring write tests

Check that the basic filesystem file-write operations
(create, copy, overwrite) function through a mirrored projfs mount.
'

. ./test-lib.sh

projfs_start test_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create source tree' '
	mkdir -p source/d1 &&
	mkdir -p source/d1/d2 &&
	echo file1 > source/f1.txt &&
	echo file1 > source/d1/f1.txt
'

test_expect_success 'create target tree' '
	echo file2 > target/f2.txt &&
	echo file2 > target/d1/f2.txt &&
	cp target/f2.txt target/d1/d2 &&
	echo file1-NEW > target/f1.txt &&
	echo file1-NEW > target/d1/f1.txt
'

test_expect_success 'check source tree' '
	test_path_is_file source/f1.txt &&
	test_path_is_file source/f2.txt &&
	test_path_is_file source/d1/f1.txt &&
	test_path_is_file source/d1/f2.txt &&
	test_path_is_file source/d1/d2/f2.txt &&
	test_cmp source/f1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp source/f2.txt "$EXPECT_DIR/f2.txt" &&
	test_cmp source/d1/f1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp source/d1/f2.txt "$EXPECT_DIR/f2.txt" &&
	test_cmp source/d1/d2/f2.txt "$EXPECT_DIR/f2.txt"
'

test_expect_success 'check target tree' '
	test_path_is_file target/f1.txt &&
	test_path_is_file target/f2.txt &&
	test_path_is_file target/d1/f1.txt &&
	test_path_is_file target/d1/f2.txt &&
	test_path_is_file target/d1/d2/f2.txt &&
	test_cmp target/f1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp target/f2.txt "$EXPECT_DIR/f2.txt" &&
	test_cmp target/d1/f1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp target/d1/f2.txt "$EXPECT_DIR/f2.txt" &&
	test_cmp target/d1/d2/f2.txt "$EXPECT_DIR/f2.txt"
'

projfs_stop || exit 1

test_done

# vim: set ft=sh:
