# libprojfs

A Linux projected filesystem library, similar in concept to the Windows
[Projected File System][winprojfs] and developed in conjunction with the
[VFSforGit][vfs4git] project.

## Design

See the [design document](/docs/design.md).

## Getting Started

*TBD*

## Contributing

*TBD with Code of Conduct*

## Licensing

The primary libprojfs source code, including header files, is licensed
under the [GNU Lesser General Public License, version 2.1][lgpl-v2];
see the [COPYING.LIB](COPYING.LIB) file for details.

The VFSforGit API is licensed under the [MIT License][mit]; see the
[COPYING.VFSAPI](COPYING.VFSAPI) file for details.  This specifically
means the [projfs_vfsapi.h](include/projfs_vfsapi.h) header file is
cross-licensed under both the LGPLv2.1 and MIT licenses.

Some other portions of this project, such as the test scripts and
test script libraries, and files from the [GNU Build System][gnu-build],
are licensed under the [GNU General Public License, version 2][gpl-v2];
see the [COPYING.TESTLIB](COPYING.TESTLIB) file for details.  These
files are not compiled into the libprojfs library but are used for
build and test functionality only.

## Development Roadmap

We are developing the libprojfs library first, using FUSE to prototype
and test its performance, before migrating functionality into a
Linux kernel module (assuming that proves to be necessary to meet
our performance criteria).

The VFSforGit API, which is currently supported through the use of
the `--enable-vfs-api` configuration option to libprojfs, may at some
point refactored out of this library entirely and handled exclusively
within Linux-specific code in the VFSforGit project.  However, for
the moment it has proven efficient to keep it within this library
while libprojfs undergoes rapid early development.

## Authors

The libprojfs library is currently maintained and developed by
several members of GitHub's Engineering organization, including:

* [@chrisd8088](https://github.com/chrisd8088)
* [@kivikakk](https://github.com/kivikakk)

[gnu-build]: https://www.gnu.org/software/automake/manual/html_node/GNU-Build-System.html
[gpl-v2]: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
[lgpl-v2]: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html
[mit]: https://github.com/Microsoft/VFSForGit/blob/master/License.md
[winprojfs]: https://docs.microsoft.com/en-us/windows/desktop/api/_projfs/
[vfs4git]: https://github.com/Microsoft/VFSForGit

