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

AM_CFLAGS = $(WARN_CFLAGS)
AM_CPPFLAGS = -I@top_srcdir@/include
AM_LDFLAGS = $(WARN_LDFLAGS)

lib_LTLIBRARIES = libprojfs.la

libprojfs_la_SOURCES = projfs.c projfs_i.h \
		       $(top_srcdir)/include/projfs.h \
		       $(top_srcdir)/include/projfs_notify.h

if ENABLE_VFSAPI
libprojfs_la_SOURCES += projfs_vfsapi.c \
			$(top_srcdir)/include/projfs_vfsapi.h
endif ENABLE_VFSAPI

libprojfs_la_LDFLAGS = -version-number 0:0:0 \
		       -export-symbols-regex "^@libprojfs_export_regex@"

