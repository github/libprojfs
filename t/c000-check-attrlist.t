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

test_description='projfs test suite attribute list checks

Check that attribute lists are parsed correctly for projfs test suite.
'

. ./test-lib.sh

ECHO_ATTRLIST="$TEST_DIRECTORY/echo_attrlist"
EXPECT_DIR="$TEST_DIRECTORY/$(basename "$0" .t | sed 's/-.*//')"

MAX_ENTRY=256
MAX_TOTAL=1024

test_expect_success 'check missing attribute list options' '
	test_must_fail "$ECHO_ATTRLIST" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist-file
'

test_expect_success 'check empty attribute lists' '
	"$ECHO_ATTRLIST" --attrlist "" >empty.out &&
	"$ECHO_ATTRLIST" --attrlist "$(printf \\n)" >>empty.out &&
	touch empty.list &&
	"$ECHO_ATTRLIST" --attrlist-file empty.list >>empty.out &&
	printf "   #ignore\n\t\n##\n" >empty.list &&
	"$ECHO_ATTRLIST" --attrlist-file empty.list >>empty.out &&
	test_cmp empty.out "$EXPECT_DIR/empty.msg"
'

test_expect_success 'check simple attribute lists' '
	"$ECHO_ATTRLIST" --attrlist "n1" >simple.out &&
	"$ECHO_ATTRLIST" --attrlist "n2 v2" >>simple.out &&
	"$ECHO_ATTRLIST" --attrlist "n3/n3 v3/v3" >>simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

test_expect_success 'check multi-line attribute lists with whitespace' '
	"$ECHO_ATTRLIST" --attrlist "  n1     
				  n2		v2      
				     n3/n3	  v3/v3	" >simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

sq="'"
dq='"'

test_expect_success 'check attribute list entry parsing' '
	test_must_fail "$ECHO_ATTRLIST" --attrlist "${sq}x" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist "x ${dq} x${sq}" &&
	"$ECHO_ATTRLIST" \
		--attrlist-file "$EXPECT_DIR/quotes.list" >quotes.out &&
	test_cmp quotes.out "$EXPECT_DIR/quotes.echo" &&
	test_must_fail "$ECHO_ATTRLIST" \
		--attrlist-file "$EXPECT_DIR/badquotes1.list" &&
	test_must_fail "$ECHO_ATTRLIST" \
		--attrlist-file "$EXPECT_DIR/badquotes2.list" &&
	test_must_fail "$ECHO_ATTRLIST" \
		--attrlist-file "$EXPECT_DIR/badquotes3.list"
'

# $MAX_ENTRY - 2 = 254
max_entry=$(printf %0${MAX_ENTRY}d | cut -c 3- | tr 0 x)

test_expect_success 'check attribute list entry maximum length' '
	"$ECHO_ATTRLIST" --attrlist "xx$max_entry" &&
	"$ECHO_ATTRLIST" --attrlist "$max_entry x" &&
	"$ECHO_ATTRLIST" --attrlist "x $max_entry" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist "xxx$max_entry" 2>&1 | \
		grep "invalid entry (line too long)" &&
	echo -n "xx$max_entry" >long.list &&
	"$ECHO_ATTRLIST" --attrlist-file long.list &&
	echo "xx$max_entry" >long.list &&
	"$ECHO_ATTRLIST" --attrlist-file long.list &&
	echo "xxx$max_entry" >long.list &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist-file long.list 2>&1 | \
		grep "invalid entry (line too long)" &&
	printf "x \0\nx" >null.list &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist-file null.list 2>&1 | \
		grep "invalid entry (line too long)"
'

max_total="$max_entry x
x $max_entry
$max_entry x
x $max_entry
xx xx
"

test_expect_success 'check attribute list maximum length' '
	"$ECHO_ATTRLIST" --attrlist "$max_total" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist "${max_total}x" &&
	echo "$max_total" >long.list &&
	"$ECHO_ATTRLIST" --attrlist-file long.list &&
	echo "x" >>long.list &&
	"$ECHO_ATTRLIST" --attrlist-file long.list 2>&1 | \
		grep "invalid attribute list file (too long)"
'

test_expect_success 'check invalid attribute lists' '
	test_must_fail "$ECHO_ATTRLIST" --attrlist "x y z" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist "x x x x" &&
	test_must_fail "$ECHO_ATTRLIST" --attrlist "$sq\0$sq x"
'

test_expect_success 'check attribute list option precedence' '
	echo "n1 v1" >ignore.list &&
	"$ECHO_ATTRLIST" --attrlist "n2 v2" \
		--attrlist-file ignore.list >ignore.out &&
	test_cmp ignore.out "$EXPECT_DIR/ignore.echo"
'

test_expect_success 'check attribute file list parsing' '
	printf "   #ignore\n\t\n##\n" >file.list &&
	echo "n1 v1" >>file.list &&
	printf "\"n2 n2%c\"\" \"v2%c%c%c\"	v2\"\n" \
		\\ \\ "$sq" \\ >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	echo "$max_entry x" >>file.list &&
	echo "y $max_entry" >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	printf "x\001x\rx\177x\377x %c%c%c%c%c\r\t\v%c\n" \
		"$sq" \\ \\ \\ "$sq" "$sq" >>file.list &&
	tail -n 1 "$EXPECT_DIR/quotes.list" >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	"$ECHO_ATTRLIST" --attrlist-file file.list >file.out &&
	test_cmp file.out "$EXPECT_DIR/file.echo"
'
test_done

# vim: set ft=sh:
