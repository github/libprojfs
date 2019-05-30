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

HELPER_LOG='test_simple.log'

projfs_start test_simple source target --log="$HELPER_LOG" || exit 1
ls target
projfs_stop || exit 1

test_expect_success 'mount without initial option not projected on read' '
	test_must_be_empty "$HELPER_LOG"
'

projfs_start test_simple source target --log="$HELPER_LOG" --initial || exit 1
ls target
projfs_stop || exit 1

test_expect_success 'mount with initial option projected on read' '
	grep "directory projected .*: \.$" "$HELPER_LOG"
'

test_done

