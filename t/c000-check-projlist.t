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

name_max=$(echo "#include <limits.h>" | "$CC" -E -dM - | \
	   grep "#define NAME_MAX " | sed "s/^.*NAME_MAX \([0-9]*\)\$/\1/")
if test ":$name_max" != ":" && test "$name_max" -gt 0; then
	max_name=$(printf %0${name_max}d | tr 0 x)
	test_set_prereq NAME_MAX
fi

test_expect_success 'check missing projection list options' '
	test_must_fail "$ECHO_PROJLIST" &&
	test_must_fail "$ECHO_PROJLIST" --projlist &&
	test_must_fail "$ECHO_PROJLIST" --projlist-file
'

test_expect_success 'check empty projection lists' '
	"$ECHO_PROJLIST" --projlist "" >empty.out &&
	"$ECHO_PROJLIST" --projlist "$(printf \\n)" >>empty.out &&
	touch empty.list &&
	"$ECHO_PROJLIST" --projlist-file empty.list >>empty.out &&
	printf "   #ignore\n\t\n##\n" >empty.list &&
	"$ECHO_PROJLIST" --projlist-file empty.list >>empty.out &&
	test_cmp empty.out "$EXPECT_DIR/empty.msg"
'

test_expect_success 'check simple projection lists' '
	"$ECHO_PROJLIST" --projlist "d d1 0755" >simple.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0 s1/f1" >>simple.out &&
	"$ECHO_PROJLIST" --projlist "l l1 t1/f1" >>simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

test_expect_success 'check multi-line projection lists with whitespace' '
	"$ECHO_PROJLIST" --projlist "  d    d1	0755      
				     f f1	0644   0 	 s1/f1	
				     l	l1    t1/f1  " >simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

sq_empty="''"
dq_empty='""'

test_expect_success 'check projection list name parsing' '
	test_must_fail "$ECHO_PROJLIST" --projlist "d $sq_empty 0755" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d $dq_empty 0755" &&
	"$ECHO_PROJLIST" \
		--projlist-file "$EXPECT_DIR/quotes.list" >quotes.out &&
	test_cmp quotes.out "$EXPECT_DIR/quotes.echo" &&
	test_must_fail "$ECHO_PROJLIST" \
		--projlist-file "$EXPECT_DIR/badquotes1.list" &&
	test_must_fail "$ECHO_PROJLIST" \
		--projlist-file "$EXPECT_DIR/badquotes2.list" &&
	test_must_fail "$ECHO_PROJLIST" \
		--projlist-file "$EXPECT_DIR/badquotes3.list"
'

# 2*NAME_MAX + 93*"x" + "d " + " 0775" == MAX_PROJLIST_ENTRY_LEN
max_line="d $max_name$max_name$(printf %093d | tr 0 x) 0755"

test_expect_success NAME_MAX 'check projection list name maximum length' '
	"$ECHO_PROJLIST" --projlist "l $max_name $max_name" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l x$max_name x" 2>&1 | \
		grep "invalid entry name (too long)" &&
	echo "f x 0644 0 x$max_name" >long.list &&
	test_must_fail "$ECHO_PROJLIST" --projlist-file long.list 2>&1 | \
		grep "invalid entry source path (too long)" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l x x$max_name" 2>&1 | \
		grep "invalid entry target path (too long)" &&
	echo "l x$max_name x$max_name" >long.list &&
	test_must_fail "$ECHO_PROJLIST" --projlist-file long.list 2>&1 | \
		grep "invalid entry name (too long)" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "$max_line" 2>&1 | \
		grep "invalid entry name (too long)"
'

long_line="l $max_name$max_name $max_name$max_name"

test_expect_success NAME_MAX 'check projection list entry maximum length' '
	test_must_fail "$ECHO_PROJLIST" --projlist "x$max_line" 2>&1 | \
		grep "invalid entry (line too long)" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "$long_line" 2>&1 | \
		grep "invalid entry (line too long)" &&
	echo "$long_line" >long.list &&
	test_must_fail "$ECHO_PROJLIST" --projlist-file long.list 2>&1 | \
		grep "invalid entry (line too long)"
'

test_expect_success 'check projection list mode parsing' '
	"$ECHO_PROJLIST" --projlist "d d1 0000" >mode.out &&
	"$ECHO_PROJLIST" --projlist "d d1 0111" >>mode.out &&
	"$ECHO_PROJLIST" --projlist "d d1 0777" >>mode.out &&
	"$ECHO_PROJLIST" --projlist "d d1 01001" >>mode.out &&
	"$ECHO_PROJLIST" --projlist "d d1 07777" >>mode.out &&
	"$ECHO_PROJLIST" --projlist "f f1 00000 0 s1/f1" >>mode.out &&
	"$ECHO_PROJLIST" --projlist "f f1 05151 0 s1/f1" >>mode.out &&
	test_cmp mode.out "$EXPECT_DIR/mode.echo" &&
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
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1/d2 0755"
'

big_hex="FFFFFFFF"
big_num="0x$big_hex"
bigger_num="0x7FFFFFFF$big_hex"
huge_num="0x$big_hex$big_hex$big_hex$big_hex"

test_expect_success 'check projection list file size parsing' '
	"$ECHO_PROJLIST" --projlist "f f1 0644 0000 s1" >size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0x00 s1" >>size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0xFf s1" >>size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0777 s1" >>size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 9999 s1" >>size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 $big_num s1" >>size.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 $bigger_num s1" >>size.out &&
	test_cmp size.out "$EXPECT_DIR/size.echo" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 x0 s1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 - s1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 -10 s1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 099 s1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 $huge_num s1"
'

test_expect_success 'check invalid file projection lists' '
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0755 10 s1 extra" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1 0644 10" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "f f1/f2 0755 10 s1"
'

test_expect_success 'check invalid link projection lists' '
	test_must_fail "$ECHO_PROJLIST" --projlist "l l1 t1 extra" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l l1" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l l1/l2 t1"
'

test_expect_success 'check projection list option precedence' '
	echo "d d2 0777" >ignore.list &&
	"$ECHO_PROJLIST" --projlist "d d1 0755" \
		--projlist-file ignore.list >ignore.out &&
	test_cmp ignore.out "$EXPECT_DIR/ignore.echo"
'

test_expect_success 'check projection list file parsing' '
	printf "   #ignore\n\t\n##\n" >file.list &&
	echo "f f1 0644 0xFf s1" >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	printf "d x\001x\rx\177x\377x 0755\n" >>file.list &&
	echo "f f1 00000 0 s1/f1" >>file.list &&
	echo "l l1 ///t1///f1///" >>file.list &&
	echo "f f1 0644 $big_num s1" >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	echo "d \"d1   d1\" 07777" >>file.list &&
	head -n 1 "$EXPECT_DIR/quotes.list" >>file.list &&
	printf "   #ignore\n\t\n##\n" >>file.list &&
	"$ECHO_PROJLIST" --projlist-file file.list >file.out &&
	test_cmp file.out "$EXPECT_DIR/file.echo"
'

test_done

# vim: set ft=sh:
