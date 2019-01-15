ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib
DIST_SUBDIRS = $(SUBDIRS) t

EXTRA_DIST = COPYING LICENSE.md README.md \
	     config.sh.in projfs.pc.in tap-driver.sh

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = projfs.pc

$(pkgconfig_DATA): config.status

check-local:
	$(MAKE) -C t/ check

.PHONY: prove
prove: all
	$(MAKE) -C t/ prove

.PHONY: test
test: check

.PHONY: recheck
recheck:
	$(MAKE) -C t/ recheck

clean-local:
	$(MAKE) -C t/ clean

.PHONY: gitclean
gitclean:
	@-git clean -xdf

