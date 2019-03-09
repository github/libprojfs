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

test_description='projfs filesystem projection directory tests

Check that directory projection succeeds through a projfs mount.
'

. ./test-lib.sh

projfs_start test_projection lower upper --projlist-file projlist || exit 1

cat <<EOF >projlist
d d1 0755
d d2 0777
EOF

setfattr -n user.projection.empty -v y lower || exit 1

EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'check projected directory' '
	ls -a upper >ls.upper &&
	test_cmp ls.upper "$EXPECT_DIR/ls.upper"
'

test_expect_success 'check projected subdirectories' '
	ls -a upper/d1 >ls.upper &&
	test_cmp ls.upper "$EXPECT_DIR/ls.upper" &&
	ls -a upper/d1/d1 >ls.upper &&
	test_cmp ls.upper "$EXPECT_DIR/ls.upper" &&
	ls -a upper/d2/d1 >ls.upper &&
	test_cmp ls.upper "$EXPECT_DIR/ls.upper" &&
	ls -a upper/d2 >ls.upper &&
	test_cmp ls.upper "$EXPECT_DIR/ls.upper"
'

# TODO: check permissions
#   remove directories, check lower vs. upper
#   create common cat <<EOF + setfattr function

rm projlist
projfs_stop || exit 1

test_done

# vim: set ft=sh:
