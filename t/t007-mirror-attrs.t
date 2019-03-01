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

test_description='projfs filesystem mirroring attribute tests

Check that chmod, chown and utimens function as expected.
'

. ./test-lib.sh

projfs_start test_simple source target || exit 1

test_expect_success 'check for multiple groups owned by the user' '
	ids=$(id -nG | tr " " "\\n") &&
	if test $(echo "$ids" | wc -l) -ge 2; then
		test_set_prereq MULTIPLE_GROUPS
	fi
'

test_expect_success 'create source tree' '
	echo hello > source/xyz &&
	chmod 0600 source/xyz &&
	ln -s xyz source/symlink
'

test_expect_success 'chmod' '
	test $(stat -c %04a source/xyz) -eq 0600 &&
	chmod 0777 target/xyz &&
	test $(stat -c %04a source/xyz) -eq 0777 &&
	test $(stat -c %04a source/symlink) -eq 0777 &&
	chmod 0513 target/symlink &&
	test $(stat -c %04a source/xyz) -eq 0513 &&
	test $(stat -c %04a source/symlink) -eq 0777
'

test_expect_success MULTIPLE_GROUPS 'chown/chgrp' '
	ids=$(id -G | tr " " "\\n") &&
	first=$(echo $ids | cut -d" " -f1) &&
	second=$(echo $ids | cut -d" " -f2) &&

	chgrp +$first source/xyz &&
	test $(stat -c %g target/xyz) -eq "$first" &&
	chgrp +$second target/xyz &&
	test $(stat -c %g source/xyz) -eq "$second"
'

test_expect_success MULTIPLE_GROUPS 'chown/chgrp on symlinks' '
	id=$(id -g) &&
	ids=$(id -G | tr " " "\\n") &&
	first=$(echo $ids | cut -d" " -f1) &&
	second=$(echo $ids | cut -d" " -f2) &&

	chgrp $first source/xyz &&
	test $(stat -c %g -L target/symlink) -eq "$first" &&
	test $(stat -c %g target/symlink) -eq "$id" &&
	chgrp $second target/symlink &&
	test $(stat -c %g -L target/symlink) -eq "$second" &&
	test $(stat -c %g target/symlink) -eq "$id"
'

test_expect_success 'utimensat' '
	test $(stat -c%Y target/xyz) -ne 0 &&
	touch -d"1970-01-01 00:00:00 Z" target/xyz &&
	test $(stat -c%Y target/xyz) -eq 0
'

test_expect_success 'utimensat on symlinks' '
	test $(stat -c%Y target/symlink) -ne 0 &&
	touch -d"1970-01-01 00:00:00 Z" target/symlink &&
	test $(stat -c%Y target/symlink) -ne 0
'

test_expect_success 'truncate' '
	chmod 0644 target/xyz &&
	test $(stat -c%s target/xyz) -eq 6 &&
	truncate -s0 target/xyz &&
	test $(stat -c%s target/xyz) -eq 0 &&
	truncate -s+10 target/xyz &&
	test $(stat -c%s target/xyz) -eq 10 &&
	truncate -s+10 target/xyz &&
	test $(stat -c%s target/xyz) -eq 20
'

test_expect_success 'truncate on symlinks' '
	truncate -s0 target/symlink &&
	test $(stat -c%s target/xyz) -eq 0 &&
	truncate -s+10 target/symlink &&
	test $(stat -c%s target/xyz) -eq 10 &&
	truncate -s+10 target/symlink &&
	test $(stat -c%s target/xyz) -eq 20
'

test_expect_success 'xattrs' '
	test $(getfattr target/xyz | wc -l) -eq 0 &&

	setfattr -n user.testing -v hello target/xyz &&
	test $(getfattr target/xyz | grep ^[^#] | wc -l) -eq 1 &&
	test "$(getfattr -n user.testing --only-values target/xyz)" = hello &&

	setfattr -x user.testing target/xyz &&
	test $(getfattr target/xyz | wc -l) -eq 0
'

test_expect_success 'xattrs on symlinks' '
	test $(getfattr -h target/symlink | wc -l) -eq 0 &&
	test_must_fail setfattr -h -n user.testing -v hello target/symlink &&
	test $(getfattr -h target/symlink | wc -l) -eq 0
'

projfs_stop || exit 1

test_done

# vim: set ft=sh:
