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

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib
DIST_SUBDIRS = $(SUBDIRS) t

EXTRA_DIST = CODE_OF_CONDUCT.md CONTRIBUTING.md COPYING NOTICE README.md \
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
	-$(MAKE) -C t/ clean

.PHONY: gitclean
gitclean:
	@-git clean -xdf

