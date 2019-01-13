#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
#

test_description='projfs filesystem mirroring remove tests

Check that the basic filesystem delete operations (rmdir, rm)
function through a mirrored projfs mount.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create source tree' '
	mkdir -p source/d1 &&
	mkdir -p source/d1/d2 &&
	mkdir -p source/d3 &&
	echo file1 > source/f1.txt &&
	echo file2 > source/f2.txt &&
	echo file1 > source/d1/f1.txt &&
	echo file2 > source/d1/d2/f2.txt
'

test_expect_success 'remove target tree components' '
	rm target/d1/d2/f2.txt &&
	rm target/f2.txt &&
	rmdir target/d1/d2 &&
	rmdir target/d3
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

