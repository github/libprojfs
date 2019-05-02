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
		 test_fdtable \
		 test_handlers \
		 test_simple \
		 wait_mount

get_strerror_SOURCES = get_strerror.c $(test_common)
test_fdtable_SOURCES = test_fdtable.c $(test_common) \
		       ../lib/fdtable.c ../lib/fdtable.h
test_handlers_SOURCES = test_handlers.c $(test_common)
test_simple_SOURCES = test_simple.c $(test_common)
wait_mount_SOURCES = wait_mount.c $(test_common)

TESTS = t000-mirror-read.t \
	t001-mirror-mkdir.t \
	t002-mirror-write.t \
	t003-mirror-remove.t \
	t004-mirror-stat.t \
	t005-mirror-links.t \
	t006-mirror-statfs.t \
	t007-mirror-attrs.t \
	t100-fdtable-fill.t \
	t200-event-ok.t \
	t201-event-err.t \
	t202-event-deny.t \
	t203-event-null.t \
	t204-event-allow.t \
	t205-event-locking.t

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

