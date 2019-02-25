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

test_description='projfs test suite projection list checks

Check that projection lists are parsed correctly for projfs test suite.
'

. ./test-lib.sh

ECHO_PROJLIST="$TEST_DIRECTORY/echo_projlist"
EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

test_expect_success 'check missing projection list options' '
	test_must_fail "$ECHO_PROJLIST" &&
	test_must_fail "$ECHO_PROJLIST" --projlist &&
	test_must_fail "$ECHO_PROJLIST" --projlist-file
'

test_expect_success 'check empty projection lists' '
	"$ECHO_PROJLIST" --projlist "" > empty.out &&
	touch empty.list &&
	"$ECHO_PROJLIST" --projlist-file empty.list >> empty.out &&
	test_cmp empty.out "$EXPECT_DIR/empty.out"
'

test_expect_success 'check simple projection lists' '
	"$ECHO_PROJLIST" --projlist "d d1 0755" > simple.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0 s1/f1" >> simple.out &&
	"$ECHO_PROJLIST" --projlist "l l1 s1/f1" >> simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.out"
'

test_expect_success 'check multi-line projection lists with whitespace' '
	"$ECHO_PROJLIST" --projlist "  d    d1	0755      
				     f f1	0644   0 	 s1/f1	
				     l	l1    s1/f1  " > simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.out"
'

test_expect_success 'check projection list mode parsing' '
	"$ECHO_PROJLIST" --projlist "d d1 0000" &&
	"$ECHO_PROJLIST" --projlist "d d1 0111" &&
	"$ECHO_PROJLIST" --projlist "d d1 0777" &&
	"$ECHO_PROJLIST" --projlist "d d1 01001" &&
	"$ECHO_PROJLIST" --projlist "d d1 07777" &&
	"$ECHO_PROJLIST" --projlist "f f1 00000 0 s1/f1" &&
	"$ECHO_PROJLIST" --projlist "f f1 05151 0 s1/f1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 a" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 0" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 0x000" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 1000" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 010000"
'

test_expect_success 'check invalid directory projection lists' '
	test_must_fail "$ECHO_PROJLIST" --projlist "x d1 0755" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 0755 extra" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 " &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1/d2 0755"
'

# TODO: name parsing: null, max length, quoting, escaping
# TODO: valid/invalid file projection lists: size (dec, hex, octal)
# TODO: valid/invalid link projection lists
# TODO: smoke test using projlist-file

test_done

