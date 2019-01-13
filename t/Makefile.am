AUTOMAKE_OPTIONS = 1.13  # for default parallel test harness

AM_CFLAGS = $(libprojfs_cflags)
AM_CPPFLAGS = -I@top_srcdir@/include

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
wait_mount_SOURCES = wait_mount.c

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
	t200-event-ok.t \
	t201-event-err.t \
	t202-event-deny.t \
	t203-event-null.t \
	t204-event-allow.t

if ENABLE_VFSAPI
TESTS += t500-vfs-mirror-read.t \
	 t501-vfs-mirror-mkdir.t \
	 t502-vfs-mirror-write.t \
	 t503-vfs-mirror-remove.t \
	 t700-vfs-event-ok.t \
	 t701-vfs-event-err.t \
	 t702-vfs-event-deny.t \
	 t703-vfs-event-null.t \
	 t704-vfs-event-allow.t
	 
endif ENABLE_VFSAPI

EXTRA_DIST = README.md chainlint.sed \
	     test-lib.sh test-lib-event.sh test-lib-functions.sh $(TESTS)

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' HARNESS_ACTIVE=true \
	$(SHELL) ../tap-driver.sh

TEST_MOUNTS_DIR = test-mounts
TEST_OUTPUT_DIR = test-output
PROVE_FILE = .prove

check-local: clean-mounts

.PHONY: test
test: check

.PHONY: prove
prove: clean-mounts clean-output $(check_PROGRAMS)
	@if test ":$(PROVE)" != ":"; \
	then \
		prove_dir="."; \
		state_opt=`echo "$(PROJFS_PROVE_OPTS)" | $(GREP) -- --state=`; \
		if test ":$$state_opt" != ":" && test -f "$(PROVE_FILE)"; \
		then \
			prove_dir=""; \
		fi && \
		$(PROVE) --exec $(SHELL) $(PROJFS_PROVE_OPTS) $$prove_dir; \
		$(RM) -r "$(TEST_MOUNTS_DIR)"; \
	else \
		echo "No 'prove' TAP harness available." && exit 1; \
	fi

clean-mounts:
	@$(RM) -r "$(TEST_MOUNTS_DIR)"

clean-output:
	@$(RM) -r "$(TEST_OUTPUT_DIR)"

clean-prove:
	@$(RM) $(PROVE_FILE)

clean-local: clean-mounts clean-output clean-prove

