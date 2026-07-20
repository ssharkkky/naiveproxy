#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
apk=${M6_ANDROID_APK:?set M6_ANDROID_APK}
naive_bin="$repo_dir/src/out/Release/naive"

test "$(uname -s)" = Linux
test -f "$apk"
test -x "$naive_bin"
git -C "$repo_dir" merge-base --is-ancestor 72fef639e5 HEAD
git -C "$repo_dir" diff --quiet -- src/net apk

readelf_bin="$repo_dir/src/third_party/llvm-build/Release+Asserts/bin/llvm-readelf"
test -x "$readelf_bin"
"$readelf_bin" -h "$naive_bin" | grep -q 'Machine:.*AArch64'

python3 - "$apk" "$naive_bin" <<'PY'
import hashlib
import pathlib
import sys
import zipfile

apk_path = pathlib.Path(sys.argv[1])
naive_path = pathlib.Path(sys.argv[2])
entry = "lib/arm64-v8a/libnaive.so"
with zipfile.ZipFile(apk_path) as archive:
    names = archive.namelist()
    assert entry in names, entry
    assert not any(
        name.startswith("lib/") and not name.startswith("lib/arm64-v8a/")
        for name in names
    ), "unexpected APK ABI"
    packaged = archive.read(entry)
assert packaged == naive_path.read_bytes(), "packaged Naive binary differs"
assert hashlib.sha256(packaged).digest(), "empty binary hash"
PY

grep -q 'android:authorities="io.nekohasekai.sagernet.plugin.naive.BinaryProvider"' \
  "$repo_dir/apk/app/src/main/AndroidManifest.xml"
grep -q 'android:value="libnaive.so"' \
  "$repo_dir/apk/app/src/main/AndroidManifest.xml"

echo M6_G5E_ANDROID_ARM64_ELF_OK
echo M6_G5E_ANDROID_PLUGIN_PACKAGE_OK
echo M6_G5E_ANDROID_ARM64_BUILD_READY
