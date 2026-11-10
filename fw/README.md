# fw: Jieli SDK build

Builds the unmodified Jieli AC632N SDK HID "keypage" example (`apps/hid/board/bd19`,
board `CONFIG_BOARD_AC632N_DEMO`) in a `linux/amd64` container. Output: `fw/out/app.bin`,
raw q32s code linked for entry `0x1E00120` (first bytes `eeff xxxx xxxx edff`).

    fw/build.sh                       # (or `make fw-build`) clones the SDK into fw/sdk/ (gitignored), builds, writes fw/out/app.bin
    SDK_SRC=/path/to/clone fw/build.sh   # same, cloning from a local copy instead of GitHub

- SDK: `https://github.com/Jieli-Tech/fw-AC63_BT_SDK` at commit `fdc018de81629e21e594d677cf05f87d288be0f0` (HEAD is verified after checkout).
- `fw/sdk` is owned by the script: each run does `git reset --hard` to that commit, `git clean -fdx`, then applies every
  `patches/*.patch` in name order. Do not keep work in it. Every build is therefore a full rebuild (about 20 s).
- After the container step the script fails unless `app.bin` is 100-240 KB, starts `ee ff .. .. .. .. ed ff`, and the `.text`
  section of `sdk.elf` starts at `0x1e00120`.
- `patches/0001-select-keypage.patch` is applied to the clone: `CONFIG_APP_KEYBOARD 0`, `CONFIG_APP_KEYPAGE 1`.
- Reference result (unmodified SDK + that patch): `app.bin` = 228176 bytes, starts `ee ff d0 19 00 00 ed ff`. Three clean
  builds gave the same size and differed in exactly one byte: the SDK logs `__DATE__`/`__TIME__` (`apps/hid/app_main.c`), so the
  seconds digit of the build time is embedded and the SHA-256 changes on every build (examples: `dd2a3515...`, `ad57b50c...`,
  `8c5a93c7...`). The build is not bit-for-bit reproducible.

## Toolchain

Jieli's download page (`http://pkgman.jieliapp.com/doc/all`, linked from the SDK Makefile header) redirects to:

| What | URL | SHA-256 |
|---|---|---|
| Linux toolchain `jieli-linux-toolchains-20260730.1` (26027508 B), holds `q32s/`, `common/`, `pi32/`, `pi32v2/`; clang 4.0.1, x86-64 | `https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz` (short link `https://pkgman.jieliapp.com/s/linux-toolchain`) | `a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd` |
| Linux post-build tools `jieli-linux-post-build-tools-20260923.1` (38662628 B): `isd_download`, `fw_add`, `ufw_maker`, `packres`, ... | `https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-post-build-tools-20260923.1.tar.xz` (short link `https://pkgman.jieliapp.com/s/linux-postbuild`) | `6949d48c4cd82e274d7943d4ca67f214593fdb4f699ef9f51c2ecc78125ab739` |

The `Dockerfile` downloads both inside the image build, checks the hashes and unpacks them under
`/opt/jieli` (toolchain) and `/opt/jieli/post-build` (packers). Nothing from these archives runs on the host.
The short links are "latest" pointers, so a future toolchain release will change the hash; the pinned OSS URLs stay.

## Why build.sh does not call the SDK's `make` end to end

`make` compiles and links fine (`cpu/bd19/tools/sdk.elf`). Its Linux post-build script
(`cpu/bd19/tools/download.sh`, generated from `download.c`) then breaks:
it runs from the wrong directory, `lz4_packet` is stored non-executable, and its tail needs
`/opt/utils/strip-ini` and `host-client` (Jieli's hosted packer), which are not in the public archives.
`build.sh` therefore runs `make pre_build`, links `sdk.elf`, `chmod +x lz4_packet`, and runs the first half of
`download.sh` from `cpu/bd19/tools` (objcopy sections, `lz4_packet`, `cat ... > app.bin`).

## Does the SDK packer run on Linux, and does it take a chip key?

Packer runs: yes. `isd_download` (v4.2.81, x86-64 ELF, a Qt program) runs in the container. It needs Qt runtime libraries
that the image does not install (add them if you want to pack): `libsm6 libice6 libx11-6 libxext6 libfontconfig1 libglib2.0-0 libgl1
libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 libxcb1 libxcb-xinerama0 libfreetype6 libxrender1 libgbm1 libegl1 libxcb-cursor0
libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xkb1 libxcb-randr0 libxcb-xfixes0 libxcb-sync1
libxcb-shm0 libpulse0 libasound2 libnss3 libxcomposite1 libxdamage1 libxrandr2 libxi6 libxtst6 libxkbfile1 libwayland-client0
libdrm2 libpng16-16 libharfbuzz0b libicu72 libpcre2-16-0 libdouble-conversion3 libmd4c0 libglx0 libopengl0` and `QT_QPA_PLATFORM=offscreen`.
With the arguments from the SDK's `download/data_trans/download.bat`
(`isd_download isd_config.ini -tonorflash -dev bd19 -boot 0x2000 -div8 -wait 300 -uboot uboot.boot -app app.bin cfg_tool.bin -res p11_code.bin -uboot_compress -flash-params flash_params.bin`,
run in a directory holding `app.bin`, `isd_config.ini` (from `isd_config_rule.c`), `uboot.boot`, `cfg_tool.bin`, `p11_code.bin`,
`flash_params.bin`, `ota.bin`) it prints "Device offline, only package the file", writes `jl_isd.bin` (241664 B, a flash image) and
`jl_isd.fw`, and prints the flash layout (`ENTRY_ADDR 0x1e00120`). It exits 245 with "no valid ota upgrade found" /
OTA "minimal space size" FAIL lines, because the demo layout leaves too little flash for the stock OTA images.

Chip key: partly. `isd_download` has a `-key` option (`ERR_INVALIDARG` without a value). It rejects every value I tried
(`1234`, `0x1234`, `ffff`, 8/16/32 hex digits, a 4-byte file) with `Invalid key text` / `Invalid key file`, so the accepted
key syntax is unknown. The SDK's own flow passes no key. Task 2 does not depend on the packer.
