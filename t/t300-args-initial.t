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

test_description='projfs argument passthrough test

Check that arguments are interpeted and passed through correctly.
'

. ./test-lib.sh

projfs_start test_args source target || exit 1

test_expect_success 'mount without args does not mark initial' '
	test_must_fail getfattr -n user.projection.empty source
'

projfs_stop || exit 1

projfs_start test_args source target -o initial || exit 1

test_expect_success 'mount with initial arg does mark initial' '
	getfattr -n user.projection.empty source
'

projfs_stop || exit 1

test_done

