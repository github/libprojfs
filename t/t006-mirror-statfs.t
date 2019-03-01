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

test_description='projfs filesystem mirroring statfs tests

Check that the filesystem stat function works as expected.
'

. ./test-lib.sh

projfs_start test_simple source target || exit 1

test_expect_success 'check statfs type' '
	test "$(stat -f -c %T target)" = fuseblk
'

test_expect_success 'check statfs data' '
	stat -f -c "%b %c" source > stat.source &&
	stat -f -c "%b %c" target > stat.target &&
	test_cmp stat.source stat.target
'

projfs_stop || exit 1

test_done

# vim: set ft=sh:
