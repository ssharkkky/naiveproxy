#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
helper="$repo_dir/tests/m5/windows_trusted_leaf.ps1"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-win-trust.XXXXXX")
certificate="$tmp_dir/windows-leaf.pem"
private_key="$tmp_dir/windows-leaf.key"
fingerprint=

run_store() {
  operation=$1
  MSYS2_ARG_CONV_EXCL='*' powershell.exe -NoProfile -NonInteractive \
    -ExecutionPolicy Bypass -File "$(cygpath -w "$helper")" \
    -Operation "$operation" \
    -CertificatePath "$(cygpath -w "$certificate")" \
    -ExpectedThumbprint "$fingerprint"
}

cleanup() {
  if [ -n "$fingerprint" ] && [ -f "$certificate" ]; then
    run_store Remove >/dev/null 2>&1 || true
  fi
  find "$tmp_dir" -type f -exec rm -f {} \; >/dev/null 2>&1 || true
  find "$tmp_dir" -depth -type d -exec rmdir {} \; >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *) echo "Windows trust preflight requires a native Windows runner" >&2; exit 2 ;;
esac

MSYS2_ARG_CONV_EXCL=/CN= openssl req -x509 -newkey rsa:2048 -nodes \
  -days 1 -sha256 -subj '/CN=Naive M6 Windows Trust Preflight' \
  -addext 'basicConstraints=critical,CA:FALSE' \
  -addext 'keyUsage=critical,digitalSignature,keyEncipherment' \
  -addext 'extendedKeyUsage=serverAuth' \
  -keyout "$private_key" -out "$certificate" >/dev/null 2>&1
fingerprint=$(openssl x509 -in "$certificate" -noout -fingerprint -sha1 | \
  sed 's/^.*=//; s/://g')

run_store Install
run_store Check
run_store Remove
fingerprint=
echo M6_G5D_WINDOWS_TRUST_PREFLIGHT_OK
