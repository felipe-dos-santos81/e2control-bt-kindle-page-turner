#!/bin/sh
# Stage, check and zip a release of the compiled app:
#   dist/e2-kindle-click-<version>.zip and dist/SHA256SUMS
# usage: tools/release/package.sh VERSION   (needs fw/out/app.bin and fw/sdk/LICENSE from fw/build.sh)
# The archive never holds a vendor image: the user packs app.bin into the image of their own unit.
set -eu
ver=$1
cd "$(dirname "$0")/../.."
name="e2-kindle-click-$ver"
stage="dist/$name"

rm -rf "$stage" "$stage.zip" dist/SHA256SUMS
mkdir -p "$stage"
cp fw/out/app.bin tools/jlimg.py tools/e2boot.py LICENSE "$stage/"
cp fw/sdk/LICENSE "$stage/LICENSE.jieli-sdk.txt"
sed "s/@VERSION@/$ver/g" tools/release/README.txt > "$stage/README.txt"

# Smoke test (no device, no vendor image here): the app carries the Kindle Click identity that the Kindle
# matches on, and the staged tools start and still refuse a write without --yes.
python3 -I - "$stage/app.bin" <<'EOF'
import sys
app = open(sys.argv[1], "rb").read()
for what, hexbytes in (("advertising data", "020106" "05ff71010443"), ("PnP ID", "01710143040001")):
    assert app.count(bytes.fromhex(hexbytes)) == 1, f"{what} {hexbytes}: not exactly once in app.bin"
EOF
python3 -I -B "$stage/jlimg.py" 2>&1 | grep -q 'jlimg.py pack BASE_IMAGE APP_BIN OUT'
python3 -I -B "$stage/e2boot.py" flash "$stage/app.bin" 2>&1 | grep -q -- '--yes'

# Exactly these files: nothing else, and so no vendor image, can get into the archive.
want="LICENSE LICENSE.jieli-sdk.txt README.txt app.bin e2boot.py jlimg.py"
# shellcheck disable=SC2012 # the names are checked against the list; odd ones make it fail, as they should
got=$(cd "$stage" && LC_ALL=C ls | tr '\n' ' ' | sed 's/ $//')
[ "$got" = "$want" ] || { echo "staged files: '$got', expected: '$want'" >&2; exit 1; }

(cd dist && zip -q -r -X "$name.zip" "$name" && shasum -a 256 "$name.zip" > SHA256SUMS)
rm -rf "$stage"
cat dist/SHA256SUMS
