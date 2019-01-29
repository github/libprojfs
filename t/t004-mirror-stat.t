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

test_description='projfs filesystem mirroring stat tests

Check that stat returns correct results.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create source tree' '
	mkdir source/dir &&
	echo file > source/file &&
	mkfifo source/fifo &&
	ln -s file source/link
'

test_expect_success 'check directory stat' '
	stat target/dir | remove_stat_minutiae >stat.dir &&
	stat source/dir | remove_stat_minutiae >stat.dir.source &&
	test_cmp stat.dir stat.dir.source
'

test_expect_success 'check file stat' '
	stat target/file | remove_stat_minutiae >stat.file &&
	stat source/file | remove_stat_minutiae >stat.file.source &&
	test_cmp stat.file stat.file.source
'

test_expect_success 'check fifo stat' '
	stat target/fifo | remove_stat_minutiae >stat.fifo &&
	stat source/fifo | remove_stat_minutiae >stat.fifo.source &&
	test_cmp stat.fifo stat.fifo.source
'

test_expect_success 'check link stat' '
	stat target/link | remove_stat_minutiae >stat.link &&
	stat source/link | remove_stat_minutiae >stat.link.source &&
	test_cmp stat.link stat.link.source
'

projfs_stop || exit 1

test_done

