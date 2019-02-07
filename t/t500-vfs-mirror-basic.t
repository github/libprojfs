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

test_description='projfs VFS API filesystem mirroring read tests

Check that basic filesystem operations function through a mirrored
projfs mount started with the VFS API.
'

. ./test-lib.sh

projfs_start test_vfsapi_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

projfs_stat() {
	stat -c '%a %F %g %h %s %u %W %X %Y %Z' $1
}

test_expect_success 'create target tree' '
	mkdir -p target/d1/d2 &&
	echo file1 >target/f1.txt &&
	echo file1 >target/d1/f1.txt &&
	ln -s d1 target/sd1 &&
	ln -s f1.txt target/sf1.txt &&
	ln target/d1/f1.txt target/d1/lf1.txt
'

test_expect_success 'check source tree structure' '
	test_path_is_dir source/d1/d2 &&
	test_path_is_file source/f1.txt &&
	test_path_is_file source/d1/f1.txt &&
	test -L source/sd1 &&
	test -L source/sf1.txt &&
	test_path_is_file source/d1/lf1.txt &&
	find source >find.source &&
	sort find.source >sort.source &&
	test_cmp sort.source "$EXPECT_DIR/sort.source"
'

test_expect_success 'check source tree content' '
	test_cmp source/f1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp source/d1/lf1.txt "$EXPECT_DIR/f1.txt" &&
	test_cmp source/sf1.txt "$EXPECT_DIR/f1.txt"
'

test_expect_success 'check target links' '
	test "$(stat -c %i target/d1/f1.txt)" -eq \
		"$(stat -c %i target/d1/lf1.txt)" &&
	test_cmp source/d1/lf1.txt "$EXPECT_DIR/f1.txt" &&
	test "$(readlink target/sd1)" = d1 &&
	test "$(stat -c %F target/sf1.txt)" = "symbolic link" &&
	test $(stat -c %04a target/sf1.txt) -eq 0777
'

test_expect_success 'modify target tree using links' '
	echo file1-NEW >target/sf1.txt &&
	echo file1-NEW >target/sd1/lf1.txt &&
	rm target/sd1/lf1.txt &&
	rm target/sf1.txt &&
	mv target/d1 target/nd1 &&
	mv target/nd1/f1.txt target/nd1/nf1.txt
'

test_expect_success 'check modified target tree structure' '
	test_path_is_missing target/d1 &&
	test_path_is_missing target/sf1.txt &&
	test_path_is_dir target/nd1 &&
	test_path_is_file target/nd1/nf1.txt &&
	test_path_is_missing target/nd1/f1.txt &&
	test_path_is_missing target/nd1/lf1.txt
'

test_expect_success 'check modified tree contents' '
	test_cmp target/f1.txt "$EXPECT_DIR/nf1.txt" &&
	test_cmp source/f1.txt target/f1.txt &&
	test_cmp target/nd1/nf1.txt "$EXPECT_DIR/nf1.txt" &&
	test_cmp source/nd1/nf1.txt target/nd1/nf1.txt
'

test_expect_success 'check modified tree metadata' '
	test "$(stat -f -c %T target)" = fuseblk &&
	projfs_stat source/f1.txt >stat.file.source &&
	projfs_stat target/f1.txt >stat.file.target &&
	test_cmp stat.file.source stat.file.target &&
	test $(stat -c %Y target/nd1/nf1.txt) -ne 0
'

test_expect_success 'check target tree access' '
	chmod 0600 target/nd1/nf1.txt &&
	test $(stat -c %04a source/nd1/nf1.txt) -eq 0600 &&
	test $(stat -c %04a target/nd1/nf1.txt) -eq 0600 &&
	chmod 0644 target/nd1/nf1.txt &&
	test $(stat -c %04a source/nd1/nf1.txt) -eq 0644 &&
	test $(stat -c %04a target/nd1/nf1.txt) -eq 0644
'

test_expect_success 'check target tree xattrs' '
	test $(getfattr target/f1.txt | wc -l) -eq 0 &&
	setfattr -n user.test -v hello target/f1.txt &&
	test "$(getfattr -n user.test --only-values target/f1.txt)" = hello &&
	test "$(getfattr -n user.test --only-values source/f1.txt)" = hello &&
	setfattr -x user.test source/f1.txt &&
	test $(getfattr target/f1.txt | wc -l) -eq 0
'

projfs_stop || exit 1

test_done

