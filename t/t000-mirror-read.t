#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs filesystem mirroring read tests

Check that basic filesystem read operations (directory listing,
file read) function through a mirrored projfs mount.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create source tree' '
	mkdir source/d1 &&
	mkdir source/d1/d2 &&
	echo file1 > source/f1.txt &&
	echo file2 > source/f2.txt &&
	echo file1 > source/d1/f1.txt &&
	echo file2 > source/d1/d2/f2.txt
'

test_expect_success 'check target tree structure' '
	test_path_is_dir target/d1 &&
	test_path_is_dir target/d1/d2
'

projfs_stop || exit 1

test_done

