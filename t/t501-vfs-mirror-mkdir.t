#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs VFS API filesystem mirroring mkdir tests

Check that the filesystem directory creation operation (mkdir)
functions through a mirrored projfs mount and the VFS API.
'

. ./test-lib.sh

projfs_start test_vfsapi_simple source target || exit 1

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

