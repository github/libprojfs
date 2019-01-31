# libprojfs

A Linux projected filesystem library, similar in concept to the Windows
[Projected File System][winprojfs] and developed in conjunction with the
[VFSForGit][vfs4git] project.

While the libprojfs C library may also be used independently of VFSForGit,
our primary goal is to enable users to run the VFSForGit client
on a Linux system.

**PLEASE NOTE:**
At present libprojfs is undergoing rapid development and its design
and APIs may change significantly; some features are also incomplete,
and so we do not recommend libprojfs for any production environments.

However, we wanted to share our progress to date, and we hope others
are as excited as we are at the prospect of running the VFSForGit
client on Linux in the not-too-distant future!

## Current Status

The library is under active development and supports basic
directory projection, with additional features added regularly.

We will only make a first, official versioned release once initial
development is complete.

A libprojfs filesystem can currently be used to mount a VFSForGit
[`MirrorProvider`][vfs4git-mirror] test client; we expect to continue our
pre-release development until the VFSForGit client proper is also
functional with libprojfs, at which point we may make an initial
tagged release.

## Design

The libprojfs library is designed to function as a stackable Linux
filesystem supporting a "provider" process which implements custom
callbacks to populate files and directories on demand.  This is
illustrated in the context of a VFSForGit provider below:

![Illustration of libprojfs in provider context](docs/images/phase1.png)

Actual file storage is delegated to a "lower" storage filesystem,
which may be any Linux filesystem, e.g., ext4.  At the time
when a libprojfs filesystem mount is created, there may be no
files or directories in the lower filesystem yet.

As normal filesystem requests are made within the libprojfs mount
(e.g., by `ls` or `cat`), libprojfs intercepts the requests
and queries the provider process for the actual contents of the
file or directory.  The provider's response is then written to
the lower filesystem so future accesses may be satisfied without
querying the provider again.

See our [design document][design-linux] for more details.

We anticipate that whether libprojfs remains a [FUSE][fuse-man]-based
library, or becomes a [libfuse][libfuse]-like interface to a Linux kernel
module, it may be useful for purposes other than running a VFSForGit
client.

For this reason, we have tried to ensure that our native
[event notification API](include/projfs_notify.h)
is aligned closely with the Linux kernel's
[fanotify][fanotify]/[inotify][inotify]/[fsnotify][fsnotify] APIs.

## Getting Started

So long as libprojfs remains based on [FUSE][fuse-man], the primary
depedency for libprojfs is the user-space [libfuse][libfuse] library,
as well as having the Linux [`fuse`][fuse-mod] kernel module installed.

If you are using a [Docker](https://www.docker.com) container, you
may need to ensure the `fuse` kernel module is installed on your
host OS.  See the [Using Docker containers][build-docker]
section for more details.

At present we require some custom modifications to the libfuse version 3.x
library, and therefore building libprojfs against a default installation
of libfuse will not work.  We hope to work with the libfuse maintainers
to develop patches will can eventually be accepted upstream; see
[PR #346](https://github.com/libfuse/libfuse/pull/346) in the libfuse
project for the current status of this work.

To test libprojfs with the Microsoft VFSForGit `MirrorProvider`
(as we do not support the primary VFSForGit GVFS provider yet), .NET Core
must be installed and parts of the VFSForGit project built.  See the
[Building and Running MirrorProvider][build-mirror] section for details.

Please see our detailed [Build and Installation][build]
page for step-by-step instructions on how to build and install
libprojfs and its depedencies on Linux, and (optionally) also build and
run the VFSForGit `MirrorProvider` test utility with libprojfs.

## Contributing

Thank you for your interest in libprojfs!

We welcome contributions; please see our [CONTRIBUTING](CONTRIBUTING.md)
guidelines and our contributor [CODE_OF_CONDUCT](CODE_OF_CONDUCT.md).

## Licensing

The libprojfs library is licensed under the [LGPL v2.1](COPYING).

See the [NOTICE](NOTICE) file for a list of other licenses used in the
project, and the comments in each file for the licenses applicable to them.

## Development Roadmap

We are developing the libprojfs library first, using FUSE to prototype and
test its performance, before migrating functionality into a Linux kernel
module (assuming that proves to be necessary to meet our performance
criteria).

For more details on the planned development phases, see our
[design document][design-process].

The VFSForGit API, which is currently supported through the use of
the `--enable-vfs-api` configuration option to libprojfs, may at some
point refactored out of this library entirely and handled exclusively
within the [ProjFS.Linux][projfs-linux] code in the VFSForGit project.

However, for the moment it has proven efficient to keep the VFS API
within this library while libprojfs undergoes rapid early development.

## Authors

The libprojfs library is currently maintained and developed by
several members of GitHub's Engineering organization, including:

* [@chrisd8088](https://github.com/chrisd8088)
* [@kivikakk](https://github.com/kivikakk)
* [@wrighty](https://github.com/wrighty)

You can also contact the GitHub project team at
[opensource+libprojfs@github.com](mailto:opensource+libprojfs@github.com).

[build]: docs/build-install.md
[build-docker]: docs/build-install.md#using-docker-containers
[build-mirror]: docs/build-install.md#building-and-running-mirrorprovider
[design-linux]: docs/design.md#vfsforgit-on-linux
[design-process]: docs/design.md#development-process
[fanotify]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/fanotify.h
[fsnotify]: https://github.com/torvalds/linux/blob/master/include/linux/fsnotify_backend.h
[fuse-man]: http://man7.org/linux/man-pages/man4/fuse.4.html
[fuse-mod]: https://www.kernel.org/doc/Documentation/filesystems/fuse.txt
[inotify]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/inotify.h
[libfuse]: https://github.com/libfuse/libfuse
[projfs-linux]: https://github.com/github/VFSForGit/tree/features/linuxprototype/ProjFS.Linux
[winprojfs]: https://docs.microsoft.com/en-us/windows/desktop/api/_projfs/
[vfs4git]: https://github.com/Microsoft/VFSForGit
[vfs4git-mirror]: https://github.com/Microsoft/VFSForGit/tree/features/linuxprototype/MirrorProvider
