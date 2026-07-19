#!/bin/sh

set -eu

mode=${1:---contract}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)

if [ "$(uname -s)" != Darwin ]; then
  echo "M5's production trust contract currently requires macOS" >&2
  exit 1
fi

command -v security >/dev/null
command -v openssl >/dev/null

case "$mode" in
  --contract)
    security help add-trusted-cert 2>&1 | grep -q 'default is user'
    security help remove-trusted-cert 2>&1 | grep -q 'default is user'
    grep -q 'CreateSslSystemTrustStoreChromeRoot' \
      "$repo_dir/src/net/cert/internal/system_trust_store.cc"
    grep -q 'TrustImplType::kDomainCacheFullCerts' \
      "$repo_dir/src/net/cert/internal/system_trust_store.cc"
    grep -q 'kSecTrustSettingsDomainUser' \
      "$repo_dir/src/net/cert/internal/trust_store_mac.cc"
    echo M5_G0_EPHEMERAL_TRUST_CONTRACT_OK
    exit 0
    ;;
  --exercise)
    ;;
  *)
    echo "usage: $0 [--contract|--exercise]" >&2
    exit 2
    ;;
esac

trust_tmp_root=${M5_TRUST_TMP_ROOT:-/private/tmp}
tmp_dir=$(mktemp -d "$trust_tmp_root/naive-m5-trust.XXXXXX")
keychain="$tmp_dir/m5.keychain-db"
certificate="$tmp_dir/root.pem"
private_key="$tmp_dir/root.key"
password=naive-m5-ephemeral
cleanup_done=0

cleanup() {
  if [ "$cleanup_done" -eq 1 ]; then
    return
  fi
  cleanup_done=1
  security remove-trusted-cert "$certificate" >/dev/null 2>&1 || true
  security delete-keychain "$keychain" >/dev/null 2>&1 || true
  find "$tmp_dir" -type f -exec unlink {} \; >/dev/null 2>&1 || true
  rmdir "$tmp_dir" >/dev/null 2>&1 || true
}

on_signal() {
  trap - EXIT HUP INT TERM
  cleanup
  exit 130
}

trap cleanup EXIT
trap on_signal HUP INT TERM

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=Naive M5 Ephemeral Trust Contract' \
  -addext 'basicConstraints=critical,CA:TRUE' \
  -addext 'keyUsage=critical,keyCertSign,cRLSign' \
  -keyout "$private_key" -out "$certificate" >/dev/null 2>&1

if security verify-cert -c "$certificate" -p ssl >/dev/null 2>&1; then
  echo "ephemeral root was unexpectedly trusted before installation" >&2
  exit 1
fi

security create-keychain -p "$password" "$keychain"
security unlock-keychain -p "$password" "$keychain"
security add-trusted-cert -r trustRoot -p ssl -k "$keychain" "$certificate"
security verify-cert -c "$certificate" -p ssl >/dev/null
security remove-trusted-cert "$certificate"

if security verify-cert -c "$certificate" -p ssl >/dev/null 2>&1; then
  echo "ephemeral root remained trusted after removal" >&2
  exit 1
fi

cleanup
trap - EXIT HUP INT TERM

echo M5_G0_EPHEMERAL_TRUST_PRIMITIVE_OK
