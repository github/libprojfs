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

# highest version required by local m4 macros
AC_PREREQ([2.64])
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

AX_COMPILER_FLAGS()

AC_CHECK_PROGS([DIFF], [diff])
AC_ARG_VAR([DIFF], [File comparison tool])

AC_CHECK_PROGS([PKGCONFIG], [pkg-config])
AC_ARG_VAR([PKGCONFIG], [Package configuration tool])

AC_CHECK_PROGS([PROVE], [prove])
AC_ARG_VAR([PROVE], [TAP harness command, e.g., 'prove -v'])

# TODO: remove explicit --with-libfusepkg when we no longer need a custom
# libfuse, and/or when we no longer use FUSE at all
AC_ARG_WITH([libfusepkg],
  [AS_HELP_STRING([--with-libfusepkg=PCFILE],
    [Use FUSE library with pkg-config .pc file PCFILE])]
)dnl

# NOTE: altering user-supplied CPPFLAGS and LDFLAGS is normally frowned upon,
#       but we want to make using a custom libfuse installation easy
AS_IF([test ":$with_libfusepkg" != ":no" &&
       test ":$with_libfusepkg" != ":yes" &&
       test ":$with_libfusepkg" != ":"],
  [AC_MSG_NOTICE([querying pkg-config using $with_libfusepkg file])
   fuse_includedir=$("$PKGCONFIG" "$with_libfusepkg" --variable=includedir) ||
     AC_MSG_ERROR([Failed to get libfuse include path using $with_libfusepkg])
   fuse_libdir=$("$PKGCONFIG" "$with_libfusepkg" --variable=libdir) ||
     AC_MSG_ERROR([Failed to get libfuse library path using $with_libfusepkg])
   fuse_cppflags="-I$fuse_includedir"
   AXLOCAL_PREPEND_FLAG([$fuse_cppflags], [CPPFLAGS])dnl
   fuse_ldflags="-Wl,-R$fuse_libdir"
   AXLOCAL_PREPEND_FLAG([$fuse_ldflags], [LDFLAGS])dnl
   fuse_ldflags="-L$fuse_libdir"
   AXLOCAL_PREPEND_FLAG([$fuse_ldflags], [LDFLAGS])dnl
  ]dnl
)dnl

pkgconfigdir='${libdir}/pkgconfig'

AC_ARG_WITH([pkgconfigdir],
  [AS_HELP_STRING([--with-pkgconfigdir=DIR],
    [Install pkgconfig file in DIR @<:@LIBDIR/pkgconfig@:>@])]
)dnl

AS_IF([test ":$with_pkgconfigdir" != ":no" &&
       test ":$with_pkgconfigdir" != ":yes" &&
       test ":$with_pkgconfigdir" != ":"],
  [pkgconfigdir="$with_pkgconfigdir"]
)dnl

AC_SUBST([pkgconfigdir])

AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT64_T

AC_CHECK_HEADER([pthread.h], [],
  [AC_MSG_ERROR([POSIX threads header file not found])]dnl
)dnl
AC_SEARCH_LIBS([pthread_create], [pthread], [],
  [AC_MSG_ERROR([POSIX threads library not found])]dnl
)dnl

AC_CHECK_HEADERS([sys/fanotify.h], [],
  [AC_MSG_ERROR([Linux fanotify header file not found])]dnl
)dnl
AC_CHECK_HEADERS([sys/inotify.h], [],
  [AC_MSG_ERROR([Linux inotify header file not found])]dnl
)dnl

AC_CHECK_HEADER([attr/xattr.h], [],
  [AC_MSG_ERROR([Extended attributes header file not found])]dnl
)dnl
AC_SEARCH_LIBS([fsetxattr], [attr], [],
  [AC_MSG_ERROR([Extended attributes library not found])]dnl
)dnl

# TODO: remove when FUSE no longer used (also Libs.private in projfs.pc)
AC_CHECK_HEADER([fuse3/fuse.h], [],
  [AC_MSG_ERROR([FUSE version 3.2+ header file not found])],
  [@%:@define FUSE_USE_VERSION 32]dnl
)dnl
# NOTE: checking for fuse_loop_mt_31() ensures libfuse version 3.2 or higher
AC_SEARCH_LIBS([fuse_loop_mt_31], [fuse3], [],
  [AC_MSG_ERROR([FUSE version 3.2+ library not found])]dnl
)dnl

AC_CONFIG_FILES([Makefile include/Makefile lib/Makefile t/Makefile
                 config.sh projfs.pc])
AC_OUTPUT

