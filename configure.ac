# Linux Projected Filesystem
# Copyright (C) 2019 GitHub, Inc.
#
# See the NOTICE file distributed with this library for additional
# information regarding copyright ownership.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library, in the file COPYING; if not,
# see <http://www.gnu.org/licenses/>.

AC_INIT([libprojfs], [0.1])

AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-xz subdir-objects])
LT_INIT

AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_AWK
# AC_PROG_CC adds -g -O2 to CFLAGS by default for gcc
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_SED

AC_SYS_LARGEFILE

libprojfs_cflags='-fno-strict-aliasing'
libprojfs_cflags+=" -Wall -Wextra"
libprojfs_cflags+=" -Wmissing-declarations"
libprojfs_cflags+=" -Wno-sign-compare"
libprojfs_cflags+=" -Wstrict-prototypes"
libprojfs_cflags+=" -Wwrite-strings"dnl

AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],
    [Enable debug output])],
  [AC_DEFINE(PROJFS_DEBUG, 1, [Enable debugging output])]dnl
)dnl

AC_ARG_ENABLE([fuse_debug],
  [AS_HELP_STRING([--enable-fuse-debug],
    [Enable FUSE library debugging output])],
  [AC_DEFINE(PROJFS_FUSE_DEBUG, 1, [Enable FUSE library debugging output])]dnl
)dnl

enable_vfsapi=false
libprojfs_export_regex='projfs'

AC_ARG_ENABLE([vfs_api],
  [AS_HELP_STRING([--enable-vfs-api],
    [Enable .NET VFSForGit API])],
  [AC_DEFINE(PROJFS_VFSAPI, 1, [Enable .NET VFSForGit API])
    enable_vfsapi=true
    libprojfs_export_regex='(projfs|PrjFS)'
  ]dnl
)dnl

AM_CONDITIONAL([ENABLE_VFSAPI], [test :$enable_vfsapi = :true])
AC_SUBST([libprojfs_export_regex])

AC_ARG_WITH([pkgconfigdir],
  [AS_HELP_STRING([--with-pkgconfigdir=DIR],
    [Install pkgconfig file in DIR @<:@LIBDIR/pkgconfig@:>@])],
  [pkgconfigdir=$withval],
  [pkgconfigdir='${libdir}/pkgconfig']dnl
)dnl

AC_SUBST([pkgconfigdir])

AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT64_T

# TODO: for VFS API but can't be conditional; remove if no longer needed
AC_HEADER_STDBOOL
AC_TYPE_UINT16_T

AC_CHECK_HEADER([pthread.h], [],
  [AC_MSG_ERROR([POSIX threads header file not found])]dnl
)dnl

AC_CHECK_HEADERS([sys/fanotify.h])
AC_CHECK_HEADERS([sys/inotify.h])

LIBS=''
# TODO: remove when FUSE no longer used (also Libs.private in projfs.pc)
AC_SEARCH_LIBS([fuse_new_30], [fuse3], [],
  [AC_MSG_ERROR([FUSE version 3 library not found])]dnl
)dnl
AC_SEARCH_LIBS([pthread_create], [pthread], [],
  [AC_MSG_ERROR([POSIX threads library not found])]dnl
)dnl

libprojfs_libs="$LIBS"
AC_SUBST([libprojfs_libs])

AC_CHECK_PROGS([DIFF], [diff])
AC_ARG_VAR([DIFF], [File comparison tool])

AC_CHECK_PROGS([PROVE], [prove])
AC_ARG_VAR([PROVE], [TAP harness command, e.g., 'prove -v'])

AC_CONFIG_FILES([Makefile include/Makefile lib/Makefile t/Makefile
                 config.sh projfs.pc])
AC_OUTPUT

