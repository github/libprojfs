!i/bin/sh
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
	touch empty.list &&
	"$ECHO_PROJLIST" --projlist-file empty.list >>empty.out &&
	test_cmp empty.out "$EXPECT_DIR/empty.msg"
'

test_expect_success 'check simple projection lists' '
	"$ECHO_PROJLIST" --projlist "d d1 0755" >simple.out &&
	"$ECHO_PROJLIST" --projlist "f f1 0644 0 s1/f1" >>simple.out &&
	"$ECHO_PROJLIST" --projlist "l l1 s1/f1" >>simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

test_expect_success 'check multi-line projection lists with whitespace' '
	"$ECHO_PROJLIST" --projlist "  d    d1	0755      
				     f f1	0644   0 	 s1/f1	
				     l	l1    s1/f1  " >simple.out &&
	test_cmp simple.out "$EXPECT_DIR/simple.echo"
'

sq_empty="''"
dq_empty='""'

esc_ok="x\\tx\\nx\\\"x\\'x\\\\x"
sq_esc_ok="'$esc_ok'"
dq_esc_ok="\"$esc_ok\""

esc_notok="x\\vx\\ x\\Xx"
sq_esc_notok="'$esc_notok'"
dq_esc_notok="\"$esc_notok\""

test_expect_success 'check projection list name parsing' '
	test_must_fail "$ECHO_PROJLIST" --projlist "d $sq_empty 0755" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d $dq_empty 0755" &&
	echo "d $sq_esc_ok 0755" >quotes.list &&
	echo "f $dq_esc_ok 0644 10 $sq_esc_ok" >>quotes.list &&
	echo "l $sq_esc_ok $dq_esc_ok" >>quotes.list &&
	printf "d x\001x\rx\177x\377x 0755\n" >>quotes.list &&
	printf "d \"x\001x\rx x\177x\377x\" 0755" >>quotes.list &&
	"$ECHO_PROJLIST" --projlist-file quotes.list >quotes.out &&
	test_cmp quotes.out "$EXPECT_DIR/quotes.echo" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d $sq_esc_notok 0755" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d $dq_esc_notok 0755"
'

test_expect_success NAME_MAX 'check projection list name maximum length' '
	echo $max_name > foo &&
	"$ECHO_PROJLIST" --projlist "l $max_name $max_name" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l x$max_name x" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l x x$max_name" &&
	test_must_fail "$ECHO_PROJLIST" --projlist "l x$max_name x$max_name" \
		2>name.err &&
	grep "invalid entry name (too long)" name.err
'

max_line="l $max_name$max_name $max_name$max_name"

test_expect_success NAME_MAX 'check projection list entry maximum length' '
	test_must_fail "$ECHO_PROJLIST" --projlist "$max_line" 2>line.err &&
	grep "invalid entry (line too long)" line.err
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
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1 " &&
	test_must_fail "$ECHO_PROJLIST" --projlist "d d1/d2 0755"
'

# DEBUG: segfault on "d x$max_name$max_name 0755"
# DEBUG: allow null lines (whitespace only) and fix printf with \n above
# TODO: test comment lines
# TODO: valid/invalid file projection lists: size (dec, hex, octal)
# TODO: valid/invalid link projection lists

test_expect_success 'check projection list option precedence' '
	echo "d d2 0777" >ignore.list &&
	"$ECHO_PROJLIST" --projlist "d d1 0755" \
		--projlist-file ignore.list >ignore.out &&
	test_cmp ignore.out "$EXPECT_DIR/ignore.echo"
'

# TODO: smoke test using projlist-file

test_done

