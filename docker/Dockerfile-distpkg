FROM github/fuse3-linux

LABEL org.label-schema.name="projfs-distpkg"
LABEL org.label-schema.description="projfs Linux libprojfs distribution image"
LABEL org.label-schema.vendor="GitHub"
LABEL org.label-schema.schema-version="1.0"

ARG LIBPROJFS_REPO=libprojfs

ENV BUILD_DEPS ' \
            automake \
            build-essential \
            dpkg-dev \
            libtool \
            pkg-config \
    '

RUN apt-get update -qq && \
    apt-get install -y -qq --no-install-recommends $BUILD_DEPS && \
    rm -rf /var/lib/apt/lists/*

COPY . /tmp/$LIBPROJFS_REPO

WORKDIR /tmp/$LIBPROJFS_REPO
RUN ./autogen.sh
RUN ./configure
RUN make -j "$(nproc)" distclean || true
RUN make -j "$(nproc)" dist

WORKDIR /tmp
RUN \
    PROJFS_VERSION=$(pkg-config --modversion $LIBPROJFS_REPO/projfs.pc) && \
    PROJFS_RELEASE="libprojfs-$PROJFS_VERSION" && \
    PROJFS_DISTPKG="$PROJFS_RELEASE.tar.xz" && \
    \
    tar -xJf "$LIBPROJFS_REPO/$PROJFS_DISTPKG" && \
    rm -rf "$LIBPROJFS_REPO" && \
    mv "$PROJFS_RELEASE" libprojfs-release

WORKDIR /tmp/libprojfs-release
RUN \
    DEB_HOST_MULTIARCH="$(dpkg-architecture --query DEB_HOST_MULTIARCH)" && \
    ./configure --prefix=/usr \
                --libdir="\${prefix}/lib/$DEB_HOST_MULTIARCH" \
                --libexecdir="\${prefix}/lib/$DEB_HOST_MULTIARCH" \
                --sysconfdir=/etc \
                --disable-static \
                --enable-shared
RUN make -j "$(nproc)" install

WORKDIR /tmp
RUN \
    DEB_HOST_MULTIARCH="$(dpkg-architecture --query DEB_HOST_MULTIARCH)" && \
    rm -rf /tmp/libprojfs-release && \
    rm -f /usr/lib/$DEB_HOST_MULTIARCH/libprojfs.la

RUN apt-get purge -y --auto-remove $BUILD_DEPS
