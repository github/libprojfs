# Build and Installation Notes

## Installing Dependencies

The [libfuse][libfuse] version 3.x source requires the [Meson][meson] and
[Ninja][ninja] build systems (specifically Meson version 0.42 or higher),
while the libprojfs project depends (for now, at least) on the
traditional [GNU Build System][gnu-build].

On a recent Ubuntu system (version 18.04 or higher), the version of
Meson installed by `apt-get` should be sufficient, but on some other
distributions you may need to locate a newer version than is installed
by default.

For example, on Ubuntu 18.04, as the superuser (use `sudo` as needed):
```
$ apt-get update
$ apt-get install -y build-essential pkg-config udev
$ apt-get install -y meson
```

On [Debian stretch][debian-stretch], a more recent version of Meson
is only available from [`stretch-backports`][debian-meson]:
```
$ echo 'deb http://deb.debian.org/debian stretch-backports main' >> \
    /etc/apt/sources.list.d/stretch-backports.list
$ apt-get update

$ apt-get install -y build-essential pkg-config udev
$ apt-get install -y -t=stretch-backports meson
```

For libprojfs, use the following additional commands:
```
$ apt-get install -y \
    attr automake build-essential dpkg-dev libattr1-dev libtool pkg-config
```

While it is difficult to provide a comprehensive list of dependencies
suitable for all Linux distributions, we hope the preceding information
will suffice to assist those using other distros.  Please let us know
if there are specific dependencies we should further enumerate!

## Building libfuse with Modifications

We are currently using a slightly-modified fork of the upstream
libfuse project which allows libprojfs to store custom per-inode
data in memory, specifically a mutex and an empty-vs-full projection
status flag.

To build our modified version of libfuse, clone (or download as a
Zip archive) the [`context-node-userdata` branch][libfuse-userdata]
of our forked libfuse repository, and then build libfuse using Meson
and Ninja:
```
$ git clone https://github.com/kivikakk/libfuse.git libfuse-userdata
$ cd libfuse-userdata
$ git checkout context-node-userdata

$ mkdir build
$ cd build

$ meson .. && \
  ninja
```

If you wish to install the modified libfuse somewhere other than
the default `/usr/local` system location, you may wish to supply
that path in the `--prefix` option when running Meson.  Note, though,
that the `DESTDIR` environment variable can also be used; see below
for an example.

Finally, run the `ninja install` command as the superuser so it can
set the necessary permissions on the libfuse binaries:
```
$ meson --prefix=/path/to/install .. && \
  ninja && \
  sudo ninja install
```

**Please note** that if you choose to install into a system location
such as `/usr` or `/usr/local`, you should be *very cautious* not to
overwrite your distribution's default libfuse v3.x package, if one
is already in place!

Because many distributions still supply libfuse v2.x as their default
libfuse package under `/usr`, however, unless you specifically have a
system which has a libfuse v3.x installation already, you may be safe
installing our modified libfuse into a system location.  Note also that
libfuse v3.x is designed to co-exist with libfuse v2.x; both libraries
can be installed under `/usr` as the v3.x libfuse header files will be
located in `/usr/include/fuse3`.  But please *be cautious* and check
your system before trying this!

After installing in a system location such as `/usr`, you may need to
refresh the linker's cache using [`ldconfig`][ldconfig-man]:
```
$ sudo ldconfig
```

If you choose not to install your modified libfuse into a system
location, you will still likely benefit from installing it into
at least a working directory, for several reasons.  First, libprojfs
will expect the version 3.x `fuse.h` to be within a `fuse3` directory,
so having it installed for you under such a path is convenient.

Second, the libfuse installation (if run as the superuser) will
set the setuid flag on the [`fusermount3(1)`][libfuse-mount3] binary,
which is required to allow non-privileged users to mount virtual
filesystems (like libprojfs!)

To install libfuse into such a working location, you can use the
`--prefix` Meson option, or the `DESTDIR` environment variable when
running `ninja install`, as shown below:
```
sudo DESTDIR=/path/to/libfuse-userdata-install ninja install
```

Whether you use the `--prefix` Meson option or the `DESTDIR`
variable, you will need to supply the path to this location when
building libprojfs (see below).  You may also need to supply this
path in your `LD_LIBRARY_PATH` environment variable when running the
[VFSForGit `MirrorProvider`][vfs4git-mirror] test programs, unless you
build libprojfs with the `-Wl,-R` option (again, see below).

## Building libprojfs

Because we have not yet made a tagged, versioned release package,
you will need to clone our repository:
```
$ git clone https://github.com/github/libprojfs.git
```
(Alternatively you could download and unzip a package of the libprojfs
source code using the "Clone or download" button on this page.)

Next, run the `autogen.sh` script to generate an [Autoconf][autoconf]
`configure` script.  (If you don't have your modified libfuse in a
system location, you may need to supply `CPPFLAGS` and `LDFLAGS`
to `autogen.sh` too; see the next paragraph for an example.)
```
$ ./autogen.sh
```
(This `autogen.sh` step will not be necessary in the future for those
who have downloaded a versioned release package of libprojfs.)

The basic build process at this point is the typical Autoconf and
[Make][make] one, in effect:
```
$ ./configure && make && make test
```

Running `./configure --help` will output the full set of configuration
options available, including the usual `--prefix` option.

However, unless you installed your modified libfuse library into a system
location, you will need to ensure that the `configure` script finds your
libfuse installation by setting the `CPPFLAGS` and `LDFLAGS` environment
variables appropriately.  For example (but check first that the paths you
supply actually contain `fuse3/fuse.h` for the `-I` option and `libfuse3.so`
for the `-L` and `-Wl,-R` options; you may need to append additional path
segments such as `x86_64-linux-gnu` or similar subdirectories):
```
$ CPPFLAGS=-I/path/to/libfuse/include \
    LDFLAGS='-L/path/to/libfuse/lib -Wl,-R/path/to/libfuse/lib' \
    ./configure && \
  make && \
  make test
```

The `-Wl,-R` option is shorthand for `-Wl,-rpath`; the
[`-Wl,<args>` compiler option][gcc-wl] ensures the directory given in the
[`-R` or `-rpath` suffix][ld-rpath] is passed to the linker
as a runtime library search path.

To build libprojfs with the VFSForGit API option (which is
required if you plan to run a VFSForGit "provider" such as MirrorProvider
the test provider we are developing against for now), add the
`--enable-vfs-api` flag to the arguments for `configure`:

```
$ ./configure --enable-vfs-api && make && make test
```

Note that as described in the [Getting Started][readme-start] section,
support for `user.*` extended attributes will be required for libprojfs
to function, including the test suite, which may fail if extended
attributes are not available on the filesystem used to build libprojfs.

## Installing libprojfs

If you would like to install the library in a location other than
`/usr/local`, supply the usual `--prefix=/path/to/install` argument
to the `configure` command, for example:
```
$ ./configure --prefix=/usr && make && make test
```

You may then choose to install the library; note that `sudo` may be
required if you are installing into a system location such as `/usr`
or `/usr/local`, and you may also want to run `ldconfig` to refresh
the linker's shared library cache:
```
$ sudo make install
$ sudo ldconfig
```

If you do not install the library into a system location where your
linker will automatically find it, you will need to supply a path to
its build location in the `LD_LIBRARY_PATH` environment variable when
running the MirrorProvider scripts, as shown in the section below.

## Installing Microsoft .NET Core SDK

Because there is no VFSForGit Linux release package, building from
VFSForGit sources (specifically the
[`features/linuxprototype` branch][vfs4git-linux]) is required.

You will need to install the Microsoft [.NET Core][dotnet-core]
packages before you can build or run the VFSForGit MirrorProvider
source code.

Your best resource here is Microsoft's [own documentation][dotnet-ubuntu].
You will need the [.NET Core SDK][dotnet-github], not just the Runtime,
because you will be building the VFSForGit application as well as running it.

Per Microsoft's [Preparing your Linux system for .NET Core][dotnet-linux]
instructions, you will need to install some of the .Net Core dependencies
first, including [ICU][icu], [OpenSSL][openssl], and optionally
[Kerberos][kerberos] version 5, [libunwind][libunwind], and [LTTng][lttng],
although we have only found ICU and OpenSSL to be required (but YMMV).

On an Ubuntu 18.10 system, the following commands should install
the .NET Core SDK and its dependencies (use `sudo` as needed):
```
$ wget -q \
    https://packages.microsoft.com/config/ubuntu/18.10/packages-microsoft-prod.deb
$ dpkg -i packages-microsoft-prod.deb
$ apt-get install apt-transport-https
```

Instructions for other distributions and versions are given on Microsoft's
Linux installation page, for example, for [Ubuntu 18.04][dotnet-ubuntu].
One may also choose another distribution from that page's menu.

If you need to download packages directly (e.g., to unpack a `.rpm`
package and install its contents manually), Microsoft maintains a
set of packages for common distributions on its
[package distribution][dotnet-pkgs] site, such as individual
[RPM packages for Fedora 27][dotnet-fedora].

Once you have the .NET Core SDK and its dependencies installed, you
should be able to run the `dotnet` command and build a
[small example .NET application][dotnet-hello]:
```
$ dotnet new console && \
  dotnet run
```

If you want to avoid the default .NET Core behavior of collecting
and publishing anonymous [telemetry data][dotnet-telemetry], you can
set the `DOTNET_CLI_TELEMETRY_OPTOUT` environment variable to `1`.
We have also found the following other environment variables useful
in simplifying some .NET defaults:
```
$ export DOTNET_CLI_TELEMETRY_OPTOUT="1"
$ export DOTNET_SKIP_FIRST_TIME_EXPERIENCE="1"
$ export NUGET_XMLDOC_MODE="skip"
```

Finally, if you installed the .NET Core packages into a non-standard
location, you may need to set the `DOTNET_ROOT` variable before running
the `dotnet` command, e.g.:
```
$ DOTNET_ROOT=/usr/local/share/dotnet dotnet new --help
```

If you are using Docker containers, you may find it simplest to use
the latest available [`microsoft/dotnet` image][dotnet-docker]
in your `Dockerfile`:
``` dockerfile
FROM microsoft/dotnet:latest
```

## Building and Running MirrorProvider

We are maintaining a [GitHub fork][vfs4git-github] of the upstream Microsoft
[VFSForGit][vfs4git] repository, where any changes to the
`features/linuxprototype` branch will be committed first.  While
you are encouraged to watch our repository, we recommend only forking
the primary upstream Microsoft repository if you plan to submit your
own pull requests.

To build the VFSForGit MirrorProvider, first check out (or download
as a Zip archive) the repository:
```
$ git clone https://github.com/Microsoft/VFSForGit.git \
    -b features/linuxprototype
```

Next, run the `MirrorProvider/Scripts/Linux/Build.sh` script:
```
$ cd MirrorProvider/Scripts/Linux
$ ./Build.sh
```

During your first build, the [`dotnet build`][dotnet-build] command
will automatically download the set of [NuGet][dotnet-nuget] packages
needed to build the VFSForGit source.  These will be cached for later
reuse by subsequent builds, and only updates should be downloaded in
the future.

If the build succeeded, you are ready to try running the
MirrorProvider together with libprojfs!  Congratulations on making
it this far!

If you installed libprojfs into a system location like `/usr`, then
you should be able to run the two other scripts in the
`MirrorProvider/Scripts/Linux` directory as follows:
```
$ ./MirrorProvider_Clone.sh && \
  ./MirrorProvider_Mount.sh
```

If the `libprojfs.so` dynamic library you built in the preceding
[Building libprojfs](#building-libprojfs) section is not installed,
or if it's installed in a custom location, you will need to provide
that path in the `LD_LIBRARY_PATH` environment variable.

For instance, to use the `libprojfs.so` shared library without having
to install it (and assuming you supplied the location of your modified
libfuse to the linker by using the `-Wl,-R` compiler option when configuring
your libprojfs build), you may reference the `lib/.libs` hidden directory
that was created by the libprojfs build process:
```
$ export LD_LIBRARY_PATH=/path/to/libprojfs/lib/.libs
$ ./MirrorProvider_Clone.sh` && \
  ./MirrorProvider_Mount.sh
```

The `MirrorProvider_Clone.sh` script will set up an "enlistment"
configuration file between a source directory and a target directory;
by default, these are `~/PathToMirror` and `~/TestRoot`, but
you can override these defaults:
```
$ ./MirrorProvider_Clone.sh /path/to/source /path/to/target && \
  ./MirrorProvider_Mount.sh /path/to/target
```

The `MirrorProvider_Clone.sh` script simply creates the file
`<target-dir>/.mirror/config` which contains the path to the source
directory.  After this has been done once, you no longer need to
run this script for the same pair of source and target directories.

The `MirrorProvider_Mount.sh` script actually runs the mirroring
test framework.  As filesystem requests are made within the target
directory, they are intercepted by FUSE and libprojfs and callbacks
to the [`MirrorProvider/FileSystemVirtualizer.cs`][vfs4git-mirror-virt]
are made, which simply checks the contents of the source directory
and replies to libprojfs with those contents.

Therefore the expected result is that any directories found within
the source directory should be reflected (mirrored) into the
`<target-dir>/src` directory.  In the longer term, replacing
MirrorProvider with the real VFSForGit provider should reflect the
contents of a VFSForGit-hosted Git repository into the target (i.e.,
working) directory under `<target-dir>/src`.

But, more than simple mirrorings, as directories are queried (or created)
within the target directory they will be "locally" created on demand
within the `<target-dir>/.mirror/lower` "storage" directory.
That is, while the provider happens to be mirroring the contents of
`<source-dir>`, libprojfs doesn't have any knowledge of this fact;
the provider could be reporting data from any source.  As a user
traverses the directory tree within the projected target directory
(`<target-dir>/src`), the data supplied by the provider are written
to the user's local disk in the lower storage directory.  See the
[VFSForGit on Linux][design-linux] section of our design document
for more details on how directory contents transition between
projected (placeholder) and full states.

At this time, libprojfs only supports directory projection, but
support for files, symlinks, pipes, and other esoterica will be added
soon!  :-)

Here is an example test run of `MirrorProvider_Mount.sh` and the
currently expected output.  First, create some "content" in the
form of directories in `~/PathToMirror`:
```
$ mkdir ~/PathToMirror/foo
$ mkdir -p ~/PathToMirror/bar/123/abc
```

Now start MirrorProvider; it should start successfully and
wait for a keypress to exit.  The example below assumes that you have
compiled libprojfs but not installed it, and that you supplied an
`-Wl,-R/path/to/libfuse/lib` option in `LDFLAGS` when building libprojfs,
so that the location of your modified libfuse is in the runtime library
search path of `libprojfs.so`.
```
$ LD_LIBRARY_PATH=/path/to/libprojfs/lib/.libs \
  ./MirrorProvider_Mount.sh 
Mounting /home/user/TestRoot
Virtualization instance started successfully
Press Enter to end the instance
```

You may already see some additional output right away, as various
system daemons begin to probe the new filesystem mount.  Confusingly,
on many distributions, these may include the [GNOME GVfs][gvfs]
[gvfs-udisks2-volume-monitor][gvfs-volmon].  Do not be fooled!  :-)
This process is not part of either libprojfs or VFSForGit; it is just
looking for new filesystem mounts using the [GNOME I/O library][gvfs-gio]'s
[GUnixMount API][gvfs-gunix].  You can safely ignore these messages,
which may look like the following:
```
OnEnumerateDirectory(0, '.xdg-volume-info', 11410, /usr/libexec/gvfs-udisks2-volume-monitor)
projfs: event handler failed: No such file or directory; event mask 0x0001-40000000, pid 11410
OnEnumerateDirectory(0, '.', 1621, /usr/libexec/gvfs-udisks2-volume-monitor)
```

The good news is, this means that queries by these system processes are
already being handled by libprojfs and the MirrorProvider process.

Now, in another shell, visit the projected filesystem and explore the
directory tree:
```
$ cd ~/TestRoot/src
$ ls
$ ls bar
$ ls bar/123
$ ls bar/123/abc
```

In the first shell session where MirrorProvider is running, you should see
something like:
```
OnEnumerateDirectory(0, 'bar', 14401, /usr/bin/ls)
OnEnumerateDirectory(0, 'bar/123', 14402, /usr/bin/ls)
OnEnumerateDirectory(0, 'bar/123/abc', 14403, /usr/bin/ls)
```

If you then create and delete a few directories inside `~/TestRoot/src`,
like this, in the second shell session:
```
$ mkdir -p xxx/yyy
```

then in the MirrorProvider shell session, you should see:
```
OnNewFileCreated (isDirectory: True): xxx
OnNewFileCreated (isDirectory: True): xxx/yyy
OnPreDelete (isDirectory: True): xxx/yyy
OnPreDelete (isDirectory: True): xxx
```

And that's about it for now, with more functionality expected soon
as we work on persistence, file projection, and more.

Also note that, for the present, you should delete the contents of the
`<target-dir>/.mirror/lower` storage directory between invocations
of `MirrorProvider_Mount.sh`, just to reset your test environment
to a clean state.  We expect to be able to persist the state of the
projected filesystem between mounts, but this is work-in-progress
at the moment.

## Using Docker Containers

We are using Docker containers for our Continuous Integration repeatable
builds and to facilitate cross-platform development.

To this end we have some `Dockerfile`s and a command-line `projfs` script
which may be useful; please see our [`docker` directory][readme-docker]
for these files and more details.

Before using Docker, you will want to ensure that the `fuse`
kernel module is available in your Host OS.  If you are running on macOS,
note that we have had difficulty with [Docker Machine][docker-machine]
and prefer [Docker for Mac][docker4mac], as it appears to come with
the `fuse` module in its Host OS.

The [`docker run`][docker-run] command will need at least the
following arguments:
```
--device /dev/fuse --cap-add SYS_ADMIN --pid=host
```
and you may also want to use the `--user <user|pid>` argument to
specify the user ID which should run the libprojfs process.

[autoconf]: https://www.gnu.org/software/autoconf/
[debian-stretch]: https://www.debian.org/releases/stretch/
[debian-meson]: https://packages.debian.org/stretch-backports/meson
[design-linux]: design.md#vfsforgit-on-linux
[docker-machine]: https://docs.docker.com/machine/
[docker-run]: https://docs.docker.com/engine/reference/commandline/run/
[docker4mac]: https://docs.docker.com/docker-for-mac/
[dotnet-build]: https://docs.microsoft.com/en-us/dotnet/core/tools/dotnet-build
[dotnet-core]: https://dotnet.microsoft.com/download
[dotnet-docker]: https://hub.docker.com/r/microsoft/dotnet/
[dotnet-fedora]: https://packages.microsoft.com/fedora/27/prod/
[dotnet-github]: https://github.com/dotnet/core/blob/master/release-notes/download-archive.md#net-core-runtime-and-sdk-download-archive
[dotnet-hello]: https://docs.microsoft.com/en-us/dotnet/core/tutorials/using-with-xplat-cli
[dotnet-linux]: https://github.com/dotnet/core/blob/master/Documentation/linux-setup.md#preparing-your-linux-system-for-net-core
[dotnet-nuget]: https://www.nuget.org/
[dotnet-pkgs]: https://packages.microsoft.com/
[dotnet-telemetry]: https://docs.microsoft.com/en-us/dotnet/core/tools/telemetry
[dotnet-ubuntu]: https://dotnet.microsoft.com/download/linux-package-manager/ubuntu18-04/sdk-current
[gcc-wl]: https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html#index-Wl
[gnu-build]: https://www.gnu.org/savannah-checkouts/gnu/autoconf/manual/autoconf-2.69/html_node/The-GNU-Build-System.html#The-GNU-Build-System
[gvfs]: https://wiki.gnome.org/Projects/gvfs
[gvfs-gio]: https://developer.gnome.org/gio/stable/
[gvfs-gunix]: https://developer.gnome.org/gio/stable/gio-Unix-Mounts.html
[gvfs-volmon]: https://wiki.gnome.org/Projects/gvfs/doc#udisks2_volume_monitor
[icu]: http://site.icu-project.org/home
[inotify]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/inotify.h
[kerberos]: https://web.mit.edu/kerberos/
[ld-rpath]: https://sourceware.org/binutils/docs-2.32/ld/Options.html#index-runtime-library-search-path
[ldconfig-man]: http://man7.org/linux/man-pages/man8/ldconfig.8.html
[libfuse]: https://github.com/libfuse/libfuse
[libfuse-mount3]: http://man7.org/linux/man-pages/man1/fusermount3.1.html
[libfuse-userdata]: https://github.com/kivikakk/libfuse/tree/context-node-userdata
[libunwind]: https://www.nongnu.org/libunwind/
[lttng]: https://lttng.org/
[make]: https://www.gnu.org/software/make/
[meson]: https://mesonbuild.com/
[ninja]: https://ninja-build.org/
[openssl]: https://www.openssl.org/
[readme-docker]: ../docker/README.md
[readme-start]: ../README.md#getting-started
[vfs4git]: https://github.com/Microsoft/VFSForGit
[vfs4git-github]: https://github.com/github/VFSForGit
[vfs4git-linux]: https://github.com/Microsoft/VFSForGit/tree/features/linuxprototype
[vfs4git-mirror]: https://github.com/Microsoft/VFSForGit/tree/features/linuxprototype/MirrorProvider
[vfs4git-mirror-virt]: https://github.com/Microsoft/VFSForGit/blob/features/linuxprototype/MirrorProvider/MirrorProvider/FileSystemVirtualizer.cs
