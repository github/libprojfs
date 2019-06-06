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

test_description='projfs filesystem permission mode tests

Check that user read file permission modes are enforced and
sufficient for projection through a mirrored projfs mount.
'

. ./test-lib.sh

umask 0000
projfs_start test_simple source target || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'create target tree' '
	mkdir -p target/d1 &&
	mkdir -p -m 0333 target/d1/d2 &&
	touch target/f1.txt &&
	chmod 0444 target/f1.txt &&
	umask 0444 &&
	touch target/d1/f2.txt &&
	umask 0000 &&
	chmod 0555 target/d1 &&
	mkfifo -m 0222 target/p1 &&
	ln -s dummy target/l1
'

test_expect_success 'check user read permission enforced' '
	test $(stat -c %04a target/d1) -eq 0555 &&
	test $(stat -c %04a target/d1/d2) -eq 0733 &&
	test $(stat -c %04a target/f1.txt) -eq 0444 &&
	test $(stat -c %04a target/d1/f2.txt) -eq 0622 &&
	test $(stat -c %04a target/p1) -eq 0622
'

test_expect_success 'check projection test requires only read permission' '
	mv target/d1 target/d1a &&
	mv target/f1.txt target/f1a.txt
'

test_expect_success 'check projection test does not block' '
	mv target/p1 target/p1a
'

test_expect_success 'check projection test does not follow links' '
	mv target/l1 target/l1a
'

test_expect_success 'reset write permissions' '
	chmod -R u+w target
'

projfs_stop || exit 1

test_done

