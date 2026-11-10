#!/usr/bin/env bash
# Build the Jieli AC632N SDK HID "keypage" example in a linux/amd64 container.
# Output: fw/out/app.bin (q32s code for 0x1E00120). Packing into jl_isd.bin is not done here (see README).
# Env: SDK_SRC=<path or URL to a clone of Jieli-Tech/fw-AC63_BT_SDK> to skip the GitHub download.
set -euo pipefail
cd "$(dirname "$0")"

SDK_REV=fdc018d
IMAGE=e2control-jl
BOARD=apps/hid/board/bd19

if [ ! -d sdk/.git ]; then
  git clone --no-checkout "${SDK_SRC:-https://github.com/Jieli-Tech/fw-AC63_BT_SDK}" sdk
fi
git -C sdk checkout -q "$SDK_REV"
for p in patches/*.patch; do   # apply once; skip if already applied
  git -C sdk apply --check -R "../$p" 2>/dev/null || git -C sdk apply "../$p"
done

docker build -q --platform linux/amd64 -t "$IMAGE" . >/dev/null

# Why not plain `make`: the SDK's Linux post-build script is written for Jieli's hosted
# packer (needs /opt/utils/strip-ini and host-client, and assumes cwd = cpu/bd19/tools).
# So: make generates the linker script + sdk.elf, then the first half of download.sh
# (elf -> text/data/bank -> app.bin) runs from the tools dir; the host-client tail is dropped.
rm -f sdk/cpu/bd19/tools/app.bin sdk/cpu/bd19/tools/sdk.elf
docker run --rm --platform linux/amd64 --ulimit nofile=65536:65536 \
  -v "$PWD/sdk:/sdk" -w "/sdk/$BOARD" "$IMAGE" bash -euo pipefail -c '
    make pre_build
    make -j"$(nproc)" ../../../../cpu/bd19/tools/sdk.elf 2>&1 | grep -v "stack size limit exceeded"
    cd ../../../../cpu/bd19/tools
    chmod +x lz4_packet   # stored 0644 in the SDK repo
    export OBJDUMP=/opt/jieli/q32s/bin/objdump OBJCOPY=/opt/jieli/q32s/bin/objcopy OBJSIZEDUMP=/opt/jieli/q32s/bin/objsizedump
    sed "/strip-ini/,\$d" download.sh | PATH="$PWD:$PATH" bash -s sdk >/dev/null
    test -s app.bin
  '

mkdir -p out
cp sdk/cpu/bd19/tools/app.bin out/app.bin
ls -l out
