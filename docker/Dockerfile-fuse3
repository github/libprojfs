FROM debian:stretch

LABEL org.label-schema.name="projfs-fuse3"
LABEL org.label-schema.description="projfs Linux libfuse v3 image"
LABEL org.label-schema.vendor="GitHub"
LABEL org.label-schema.schema-version="1.0"

ARG FUSE_VERSION=3.3.0
ARG FUSE_SHA256=c554863405477cd138c38944be9cdc3781a51d78c369ab878083feb256111b65

WORKDIR /tmp

# fuse3 requires meson 0.42, available from stretch-backports
# wget depends on ca-certificates

RUN \
	echo 'deb http://deb.debian.org/debian stretch-backports main' >> /etc/apt/sources.list.d/stretch-backports.list && \
	apt-get update -qq && \
	apt-get install -y -qq --no-install-recommends build-essential ca-certificates pkg-config wget udev && \
	apt-get install -y -qq --no-install-recommends -t=stretch-backports meson && \
	rm -rf /var/lib/apt/lists/*

ENV FUSE_RELEASE "fuse-$FUSE_VERSION"
ENV FUSE_DOWNLOAD "$FUSE_RELEASE.tar.xz"
ENV FUSE_URL_PATH "releases/download/$FUSE_RELEASE/$FUSE_DOWNLOAD"

RUN \
	wget -q "https://github.com/libfuse/libfuse/$FUSE_URL_PATH" && \
	echo "$FUSE_SHA256 $FUSE_DOWNLOAD" | sha256sum -c -

RUN tar -xJf "$FUSE_DOWNLOAD"
WORKDIR "$FUSE_RELEASE/build"

RUN meson --prefix=/usr --sysconfdir=/etc --localstatedir=/var ..
RUN ninja -j "$(nproc)"
RUN ninja install
