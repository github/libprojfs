#!/bin/sh
#
# Copyright (C) 2019 GitHub, Inc.
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

test_description='projfs file descriptor table fill test

Check that the file descriptor hash table grows to its maximum size.
'

. ./test-lib.sh

test_expect_success 'check fdtable fill operations' '
	"$TEST_DIRECTORY/test_fdtable"
'

test_done

