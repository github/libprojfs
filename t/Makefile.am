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

AUTOMAKE_OPTIONS = 1.13  # for default parallel test harness

AM_CFLAGS = $(WARN_CFLAGS)
AM_CPPFLAGS = -I@top_srcdir@/include
AM_LDFLAGS = $(WARN_LDFLAGS)

LDADD = ../lib/libprojfs.la

test_common = test_common.c \
	      test_common.h \
	      $(top_srcdir)/include/projfs.h \
	      $(top_srcdir)/include/projfs_notify.h

check_PROGRAMS = get_strerror \
		 test_projfs_handlers \
		 test_projfs_simple \
		 wait_mount

get_strerror_SOURCES = get_strerror.c $(test_common)
test_projfs_handlers_SOURCES = test_projfs_handlers.c $(test_common)
test_projfs_simple_SOURCES = test_projfs_simple.c $(test_common)
wait_mount_SOURCES = wait_mount.c $(test_common)

if ENABLE_VFSAPI
test_vfsapi_common = $(test_common) $(top_srcdir)/include/projfs_vfsapi.h

check_PROGRAMS += test_vfsapi_handlers \
		  test_vfsapi_simple

test_vfsapi_handlers_SOURCES = test_vfsapi_handlers.c $(test_vfsapi_common)
test_vfsapi_simple_SOURCES = test_vfsapi_simple.c $(test_vfsapi_common)
endif ENABLE_VFSAPI

TESTS = t000-mirror-read.t \
	t001-mirror-mkdir.t \
	t002-mirror-write.t \
	t003-mirror-remove.t \
	t004-mirror-stat.t \
	t005-mirror-links.t \
	t006-mirror-statfs.t \
	t007-mirror-attrs.t \
	t200-event-ok.t \
	t201-event-err.t \
	t202-event-deny.t \
	t203-event-null.t \
	t204-event-allow.t \
	t205-event-locking.t

if ENABLE_VFSAPI
TESTS += t500-vfs-mirror-basic.t \
	 t700-vfs-event-ok.t \
	 t701-vfs-event-err.t \
	 t702-vfs-event-deny.t \
	 t703-vfs-event-null.t \
	 t704-vfs-event-allow.t
else !ENABLE_VFSAPI
SKIP_TESTS = t[5-9]??
endif !ENABLE_VFSAPI

EXTRA_DIST = README.md chainlint.sed clean_test_dirs.sh \
	     test-lib.sh test-lib-event.sh test-lib-functions.sh $(TESTS)

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' HARNESS_ACTIVE=true \
	$(SHELL) ../tap-driver.sh

PROVE_FILE = .prove

check-local: clean-mounts

.PHONY: test
test: check

.PHONY: prove
prove: clean-mounts $(check_PROGRAMS)
	@if test ":$(PROVE)" != ":"; \
	then \
		prove_dir="."; \
		state_opt=`echo "$(PROJFS_PROVE_OPTS)" | \
			$(GREP) -- --state=`; \
		if test ":$$state_opt" != ":" && test -f "$(PROVE_FILE)"; \
		then \
			prove_dir=""; \
		fi && \
		PROJFS_SKIP_TESTS="$(SKIP_TESTS) $$PROJFS_SKIP_TESTS" \
			$(PROVE) --exec $(SHELL) \
				$(PROJFS_PROVE_OPTS) $$prove_dir; \
		./clean_test_dirs.sh; \
	else \
		echo "No 'prove' TAP harness available." && exit 1; \
	fi

clean-mounts:
	@./clean_test_dirs.sh

clean-mounts-output:
	@./clean_test_dirs.sh --all

clean-prove:
	@$(RM) $(PROVE_FILE)

clean-local: clean-mounts-output clean-prove

