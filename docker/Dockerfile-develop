FROM github/fuse3-linux

LABEL org.label-schema.name="projfs-develop"
LABEL org.label-schema.description="projfs Linux libprojfs development image"
LABEL org.label-schema.vendor="GitHub"
LABEL org.label-schema.schema-version="1.0"

WORKDIR /data/projfs

RUN set -ex && \
    apt-get update -qq && \
    BUILD_DEPS=' \
            attr \
            automake \
            build-essential \
            libtool \
            libattr1-dev \
    ' && \
    apt-get install -y -qq --no-install-recommends $BUILD_DEPS && \
    rm -rf /var/lib/apt/lists/*

ARG UID
RUN useradd -d /data -M -s /bin/bash -u $UID -G 0 user
