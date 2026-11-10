#!/usr/bin/env bash
# Build the Jieli AC632N SDK HID "keypage" example in a linux/amd64 container.
# Output: fw/out/app.bin (q32s code for 0x1E00120). Packing into jl_isd.bin is not done here (see README).
# Env: SDK_SRC=<path or URL to a clone of Jieli-Tech/fw-AC63_BT_SDK> to skip the GitHub download.
# fw/sdk is owned by this script: every run resets it to the pinned commit, wipes all
# untracked/ignored files and re-applies fw/patches/*.patch, so no stale intermediate can leak in.
set -euo pipefail
cd "$(dirname "$0")"

SDK_REV=fdc018de81629e21e594d677cf05f87d288be0f0
IMAGE=e2control-jl
BOARD=apps/hid/board/bd19
TOOLS=sdk/cpu/bd19/tools

if [ ! -d sdk/.git ]; then
  git clone --no-checkout "${SDK_SRC:-https://github.com/Jieli-Tech/fw-AC63_BT_SDK}" sdk
fi
# Safety: reset --hard / clean -fdx below must only ever touch the SDK clone. If sdk/.git is missing or
# broken, git would resolve to the parent project repo and clean -fdx would delete its ignored files.
sdk_top=$(git -C sdk rev-parse --show-toplevel 2>/dev/null || true)
[ -n "$sdk_top" ] && [ "$sdk_top" = "$(cd sdk && pwd -P)" ] || {
  echo "refusing to reset: fw/sdk is not its own git repository (git toplevel: '${sdk_top:-none}')" >&2; exit 1; }
git -C sdk cat-file -e "$SDK_REV^{commit}" 2>/dev/null || git -C sdk fetch -q origin "$SDK_REV"
git -C sdk reset -q --hard "$SDK_REV"
git -C sdk clean -q -fdx
[ "$(git -C sdk rev-parse HEAD)" = "$SDK_REV" ] || { echo "SDK HEAD is not $SDK_REV" >&2; exit 1; }
for p in patches/*.patch; do   # name order
  git -C sdk apply "../$p"
done

docker build -q --platform linux/amd64 -t "$IMAGE" . >/dev/null

# Why not plain `make`: the SDK's Linux post-build script is written for Jieli's hosted
# packer (needs /opt/utils/strip-ini and host-client, and assumes cwd = cpu/bd19/tools).
# So: make generates the linker script + sdk.elf, then the first half of download.sh
# (elf -> text/data/bank -> app.bin) runs from the tools dir; the host-client tail is dropped.
# Prints the .text load address of sdk.elf as the last line.
docker run --rm --platform linux/amd64 --ulimit nofile=65536:65536 \
  -v "$PWD/sdk:/sdk" -w "/sdk/$BOARD" "$IMAGE" bash -euo pipefail -c '
    make pre_build
    make -j"$(nproc)" ../../../../cpu/bd19/tools/sdk.elf 2>&1 | { grep -v "stack size limit exceeded" || true; }
    cd ../../../../cpu/bd19/tools
    chmod +x lz4_packet   # stored 0644 in the SDK repo
    export OBJDUMP=/opt/jieli/q32s/bin/objdump OBJCOPY=/opt/jieli/q32s/bin/objcopy OBJSIZEDUMP=/opt/jieli/q32s/bin/objsizedump
    sed "/strip-ini/,\$d" download.sh | PATH="$PWD:$PATH" bash -es sdk
    test -s app.bin
    $OBJDUMP -section-headers -address-mask=0x1ffffff sdk.elf | awk "\$2==\".text\"{print \$4}" > text_addr.txt
  '

# Check the result: size window, q32s prologue (mov sp ; mov ssp), code start address.
size=$(wc -c < "$TOOLS/app.bin")
[ "$size" -ge 102400 ] && [ "$size" -le 245760 ] || { echo "app.bin size $size outside 100-240 KB" >&2; exit 1; }
head8=$(od -An -tx1 -N8 "$TOOLS/app.bin" | tr -s ' ' | sed 's/^ //;s/ $//')
[[ "$head8" =~ ^ee\ ff\ ..\ ..\ ..\ ..\ ed\ ff$ ]] || { echo "app.bin starts with '$head8', not 'ee ff .. .. .. .. ed ff'" >&2; exit 1; }
addr=$(tr -d '\r\n' < "$TOOLS/text_addr.txt" | tr 'A-F' 'a-f')
[ "$((16#$addr))" -eq $((0x1e00120)) ] || { echo "sdk.elf .text starts at 0x$addr, not 0x1e00120" >&2; exit 1; }

mkdir -p out
cp "$TOOLS/app.bin" out/app.bin
echo "app.bin: $size bytes, starts $head8, code at 0x$addr"
