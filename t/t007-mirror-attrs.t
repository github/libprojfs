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

Check that chmod, chown and utimens functionas expected.
'

. ./test-lib.sh

projfs_start test_projfs_simple source target || exit 1

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
	stat source/xyz | grep "Access: (0600/-rw-------)" &&
	chmod 0777 target/xyz &&
	stat source/xyz | grep "Access: (0777/-rwxrwxrwx)" &&
	stat source/symlink | grep "Access: (0777/lrwxrwxrwx)" &&
	chmod 0513 target/symlink &&
	stat source/symlink | grep "Access: (0777/lrwxrwxrwx)" &&
	stat source/xyz | grep "Access: (0513/-r-x--x-wx)"
'

test_expect_success MULTIPLE_GROUPS 'chown/chgrp' '
	ids=$(id -nG | tr " " "\\n") &&
	first=$(echo $ids | cut -d" " -f1) &&
	second=$(echo $ids | cut -d" " -f2) &&

	chgrp $first source/xyz &&
	stat target/xyz | grep -E "Gid: \\(\s*[0-9]+/\s*$first" &&
	chgrp $second target/xyz &&
	stat source/xyz | grep -E "Gid: \\(\s*[0-9]+/\s*$second"
'

test_expect_success MULTIPLE_GROUPS 'chown/chgrp on symlinks' '
	ids=$(id -nG | tr " " "\\n") &&
	first=$(echo $ids | cut -d" " -f1) &&
	second=$(echo $ids | cut -d" " -f2) &&

	chgrp $first target/symlink &&
	stat target/symlink | grep -E "Gid: \\(\s*[0-9]+/\s*$first" &&
	chgrp $second target/symlink &&
	stat source/symlink | grep -Ev "Gid: \\(\s*[0-9]+/\s*$second"
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
	test $(getfattr -n user.testing --only-values target/xyz) = hello &&

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

