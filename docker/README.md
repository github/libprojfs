Build libprojfs with Docker.

  - [Getting started](#getting-started)
  - [How it works](#how-it-works)

## Getting started

``` console
$ ./projfs
usage: ./projfs [command]

command:
  setup
    Build all containers and run all build scripts necessary for
    development work (excludes distpkg).

  image fuse3|develop|distpkg|vfs|integrate
    Build the Docker build environment for the specified component.

  develop autogen|configure|make|test|dist|clean
    Builds libprojfs for local development and testing.

    autogen:   runs autogen
    configure: runs configure with appropriate switches
    make:      runs make
    test:      runs make test
    dist:      runs make dist
    clean:     runs make clean

  vfs restore|make
    Build VFSForGit.

    restore: do a NuGet restore
    make:    build everything

  integrate clone|mount
    Run the integration.

    clone: run the MirrorProvider clone
    mount: mount the MirrorProvider

  run IMAGE [OPTION ...] -- CMD [ARG ...]
    Run a command in the specified image (`fuse3', `develop', `vfs', etc.)
    Everything up until `--' is passed as options to `docker run', i.e.
    before the image name is given. If no options are specified, defaults
    for the image will be used (e.g. enables FUSE for the 'integrate'
    image).

  exec IMAGE [OPTION ...] -- CMD [ARG ...]
    Run a command in a container already started with `./projfs run'.

  test [--force]
    Run the test suite.  Will fail if there's already a running integration
    container, unless --force is specified, in which case the running
    container is stopped before tests are run.
```

Here's how to get started:

``` shell
# Build all images, and run all build scripts.  After this finishes, build
# artifacts will be present in your local tree.
./projfs setup

# Clone the test repository (found at build/PathToMirror).  The created
# repository will be at build/integrate/TestRoot.
./projfs integrate clone

# Run the built-in end-to-end test suite (will clean up after itself if
# successful).
./projfs test

# Mount the test root.
./projfs integrate mount

# In another terminal, interact with the created mount.
./projfs exec integrate -- touch TestRoot/src/xyz
./projfs exec integrate -- rm TestRoot/src/xyz

# Start a shell to play more.  (pass `-t' to `docker exec')
./projfs exec integrate -t -- bash
```

## How it works

The command `./projfs image COMPONENT` will build one of five tagged Docker images.

  - `./projfs image fuse3` creates `github/fuse3-linux` from `Dockerfile-fuse3`. It installs development tools and build
    prerequisites in a stock Debian stretch image, then fetches and builds our fork of FUSE.

  - `./projfs image develop` creates `github/projfs-dev-linux` from `Dockerfile-develop`. It is based on
    `github/fuse3-linux`, and installs only the build tools necessary for building libprojfs.

  - `./projfs image distpkg` creates `github/projfs-dist-linux` from `Dockerfile-distpkg`. It is based on
    `github/fuse3-linux`. The Dockerfile builds the dist package for libprojfs, then installs it. The resulting image
    represents what a clean system with the libprojfs dist package installed should look like.

  - `./projfs image vfs` creates `github/vfs-linux` from `Dockerfile-vfs`. Right now this image is a plain
    Microsoft-supplied .NET SDK image with light filesystem modifications for testing.

  - `./projfs image integrate` creates `github/projfs-vfs-linux` from `Dockerfile-integrate`. It uses the .NET SDK image
    as base, then copies the built FUSE objects from `github/fuse3-linux` and sets up the environment so libraries from
    the ProjFS build (when mounted) will be located correctly.

Note that each image is only a build environment, and does not contain any source code. Each component has a range of
subcommands which will run commands in containers from each of these images; when doing so, `./projfs` mounts
directories on the Docker host (your local filesystem) into the running containers, so that up-to-date source is seen
and build artifacts are preserved.

The `projfs` component will mount the root of this repository at `/data/projfs` in the container. `./projfs develop
autogen` will run `/data/projfs/autogen.sh` in the container, with the output files being placed on the Docker host.
`./projfs develop configure` will then run `configure --enable-vfs-api`, and
`./projfs develop make` will run `make`, with all build artifacts again placed on the Docker host. (You'll have a
bunch of ELF objects which you won't be able to do anything with on macOS.)

The `vfs` component mounts the source at `/data/vfs/src`, and additional directories in `build` so that NuGet restored
packages and all build output are preserved on the Docker host.

Finally, the `integrate` component mounts all of the above plus one more (the destination of the MirrorProvider clone
operation), in locations such that everything Just Works™. Build artifacts from each of the previous steps are seen in
the integration container, and paths and environment variables are set appropriately for the whole stack to function.

Note the filesystem layering that occurs when actually running the integration:

  - `./projfs integrate clone` runs the MirrorProvider clone script, cloning `/data/PathToMirror` (`build/PathToMirror`)
    to `/data/integrate/TestRoot` (`build/integrate/TestRoot`). The source and destination of the clone are different
    Docker-mounted paths, i.e. both are on your local disk.

  - `./projfs integrate mount` runs the MirrorProvider mount script on `/data/integrate/TestRoot`. `/data/integrate` is
    a mount from the host path `build/integrate` in this repo, so the `TestRoot` scaffolding created by the clone
    operation still exists. We then mount `/data/integrate/TestRoot/src`. This mount happens in the Docker container,
    meaning the container itself accessing `/data/integrate/TestRoot/src` will interact with FUSE. Meanwhile,
    `build/integrate/TestRoot/src` will appear as an empty directory on the Docker host.

We do this to allow rapid iteration without having to rebuild entire containers every time, which is required if the
entire container build process also builds the project.
