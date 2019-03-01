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

test_description='projfs filesystem mirroring link tests

Check that links function as expected.
'

. ./test-lib.sh

projfs_start test_simple source target || exit 1

test_expect_success 'create links' '
	echo text >target/file &&
	ln target/file target/link &&
	ls -li target &&
	ln -s file target/symlink
'
test_expect_success 'check link stat' '
	test "$(stat -c %i target/file)" -eq "$(stat -c %i target/link)"
'

test_expect_success 'check symlink stat' '
	test "$(readlink target/symlink)" = file &&
	test "$(stat -c %F target/symlink)" = "symbolic link" &&
	test $(stat -c %04a target/symlink) -eq 0777
'

projfs_stop || exit 1

test_done

# vim: set ft=sh:
