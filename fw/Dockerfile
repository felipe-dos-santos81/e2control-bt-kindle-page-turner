# Jieli q32s toolchain (x86-64 Linux) for the AC632N / BD19 SDK.
# Build:  docker build --platform linux/amd64 -t e2control-jl fw
# Archives are fetched and unpacked only inside this build; hashes are pinned.
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends bash ca-certificates curl make python3 xz-utils \
 && rm -rf /var/lib/apt/lists/*

ARG TC_URL=https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz
ARG TC_SHA256=a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd
ARG PB_URL=https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-post-build-tools-20260923.1.tar.xz
ARG PB_SHA256=6949d48c4cd82e274d7943d4ca67f214593fdb4f699ef9f51c2ecc78125ab739

# toolchain -> /opt/jieli/{q32s,common,pi32,pi32v2}; post-build packers -> /opt/jieli/post-build
RUN set -eux; cd /tmp; \
    curl -fsSL --retry 5 --retry-all-errors -o tc.tar.xz "$TC_URL"; \
    echo "$TC_SHA256  tc.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/jieli; tar -xJf tc.tar.xz -C /opt/jieli --strip-components=1; \
    curl -fsSL --retry 5 --retry-all-errors -o pb.tar.xz "$PB_URL"; \
    echo "$PB_SHA256  pb.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/jieli/post-build; tar -xJf pb.tar.xz -C /opt/jieli/post-build --strip-components=1; \
    rm -f tc.tar.xz pb.tar.xz

ENV PATH=/opt/jieli/post-build:$PATH
WORKDIR /sdk
