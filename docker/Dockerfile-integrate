FROM microsoft/dotnet:2.2-sdk-stretch

ARG FUSE_VERSION=3.3.0

WORKDIR /data/integrate
ENV HOME /data/integrate

COPY --from=github/fuse3-linux \
     /usr/lib/x86_64-linux-gnu/libfuse3.so.$FUSE_VERSION \
     /usr/lib/x86_64-linux-gnu/

RUN set -ex && \
    cd /usr/lib/x86_64-linux-gnu && \
    ln -s libfuse3.so.$FUSE_VERSION libfuse3.so.3 && \
    ln -s libfuse3.so.3 libfuse3.so

COPY --from=github/fuse3-linux \
    /usr/bin/fusermount3 \
    /usr/bin/

RUN chmod 4755 /usr/bin/fusermount3

# Allow VFSForGit to load locally-compiled libprojfs binaries for development
ENV LD_LIBRARY_PATH /data/projfs/lib/.libs

ARG UID
RUN useradd -d /data -M -s /bin/bash -u $UID -G 0 user
