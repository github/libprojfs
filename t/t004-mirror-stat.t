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

projfs_stat() {
	stat -c '%a %F %g %h %s %u %W %X %Y %Z' $1
}

test_expect_success 'check directory stat' '
	projfs_stat source/dir >stat.dir.source &&
	projfs_stat target/dir >stat.dir &&
	test_cmp stat.dir stat.dir.source
'

test_expect_success 'check file stat' '
	projfs_stat source/file >stat.file.source &&
	projfs_stat target/file >stat.file &&
	test_cmp stat.file stat.file.source
'

test_expect_success 'check fifo stat' '
	projfs_stat source/fifo >stat.fifo.source &&
	projfs_stat target/fifo >stat.fifo &&
	test_cmp stat.fifo stat.fifo.source
'

# stat source/link once to update atime
test_expect_success 'check link stat' '
	projfs_stat source/link &&
	projfs_stat source/link >stat.link.source &&
	projfs_stat target/link >stat.link &&
	test_cmp stat.link stat.link.source
'

projfs_stop || exit 1

test_done

