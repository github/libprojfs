AM_CFLAGS = $(libprojfs_cflags)
AM_CPPFLAGS = -I@top_srcdir@/include

lib_LTLIBRARIES = libprojfs.la

libprojfs_la_SOURCES = projfs.c projfs_i.h \
		       $(top_srcdir)/include/projfs.h \
		       $(top_srcdir)/include/projfs_notify.h

libprojfs_la_LIBADD = @libprojfs_libs@

if ENABLE_VFSAPI
libprojfs_la_SOURCES += projfs_vfsapi.c \
			$(top_srcdir)/include/projfs_vfsapi.h
endif ENABLE_VFSAPI

libprojfs_la_LDFLAGS = -version-number 0:0:0 \
		       -export-symbols-regex "^@libprojfs_export_regex@"

