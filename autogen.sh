#!/bin/sh

set -e

aclocal -Wall --install -I m4 &&
  libtoolize -Wall --copy &&
  autoheader -Wall &&
  autoconf -Wall &&
  automake -Wall --add-missing --copy &&
  ./configure "$@"

