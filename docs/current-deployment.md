# Current Native UDP Deployment

Last verified: 2026-09-04 16:10 (Asia/Shanghai)

This page is the authority for what is deployed now. Historical milestone
records in `native-udp-status.md` prove earlier gates, but do not identify the
current production binaries.

## Product lock and manifests

The deployed artifacts derive from [`release/product.lock.json`](../release/product.lock.json)
(`v150.0.7871.63-2-native-udp-m7`, experimental):

| Repository | Locked `master` commit |
| --- | --- |
| `ssharkkky/naiveproxy` | `742b89aa24131749b62856e5ed9189273a32f26e` |
| `ssharkkky/forwardproxy` | `4265c663dcaf3981a57f676984d1b0b03615dee0` |
| `ssharkkky/caddy` | `0ea5700f64254ba24e39d57b1febece2fa34927e` |
| `ssharkkky/quic-go` | `c308178d8c77061d5e261ce9df37f2bcc0ab22bf` |

Machine-readable build and deployment evidence:

- [current client manifest](../release/manifests/current-client.json)
- [current server manifest](../release/manifests/current-server.json)

The product-lock SHA256 embedded in both manifests is
`03a95a5d05b510e420c14cc0cba24d34cffa2cca7122d1d36524eefb4927bfd0`.

## Deployed binaries

| Role | Host and service | Artifact | Online SHA256 | Runtime configuration |
| --- | --- | --- | --- | --- |
| Production client | `rtr.local` (`192.168.2.1`), `/etc/init.d/native-udp` | Local build of locked `742b89aa24` (Fast Open hotfix `c8ebb943bd` plus audit fixes `153de92c8e`, `afce211960`), OpenWrt x86_64 | `6e3a6655415b0f0e4481e1d524f906aa382d2314c5d96001afd7dfabc4b063b2` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Validation client | `lllinya.com`, `native-udp-client.service` | Same run, Linux x64 | `4b24c709396a03a299f7dc5784b56c6b598e535f84072599b11ae52d8bfb1dca` | SOCKS5 `127.0.0.1:1080`, `bbr1` |
| Production server | `triptrip999.qzz.io`, `native-udp-caddy.service` | Product server run [`33743443764`](https://github.com/ssharkkky/naiveproxy/actions/runs/33743443764) | `52a1ca4f5cb2829c97af1a32359914baae7b93ca6548d8bb71a6291cc9860e3d` | TCP+UDP `:8443`, `bbr-standard` |

The client workflow ran at `0864269b`. Between the locked NaiveProxy source
`9842de51` and that workflow commit only `release/product.lock.json` changed;
`git diff --quiet 9842de51..0864269b -- src` passed. The runtime client source
therefore matches the locked commit while retaining the workflow run as the
build provenance.

## Fast Open response-order hotfix

The production router client was replaced on 2026-09-04 with commit
`c8ebb943bd` after reproducing an intermittent HTTPS stall on GitHub release
assets. The fix re-notifies a pending body read after initial response headers
are delivered when body data or FIN arrived first. It does not alter sing-box,
the server, the SOCKS5 endpoint, or the native UDP wire protocol. This is an
emergency local build and is intentionally not represented as a green CI
release artifact yet; the next standard release must include this commit in
the product lock and regenerate the client manifest.

Validation through the unchanged router SOCKS5 listener after replacement:

- GitHub API: 5/5 HTTP 200.
- GitHub home: 5/5 HTTP 200.
- Reddit: 5/5 completed TLS/HTTP (HTTP 403 responses from the site).
- GitHub `release-assets` 15,451,894-byte archive: 3/3 complete downloads.

The previous client reproduced one timeout in an 8-run comparison. The fixed
temporary client completed 8/8 identical downloads before production
replacement. A rollback copy is stored under `/root/native-udp-deploy/`.

The production router already had the exact verified OpenWrt artifact, so it
was not restarted. The Linux validation client was replaced atomically and its
SOCKS listener remained `127.0.0.1:1080`. The server candidate passed Caddyfile
validation and module inspection before replacement. After restart, TCP and
UDP `8443` were listening, the service was active with `NRestarts=0`, and live
CONNECT-UDP associations repopulated. A SOCKS TCP probe through `lllinya`
returned HTTP 204; a one-shot loopback UDP echo through the same SOCKS listener
returned `CURRENT_DEPLOYMENT_UDP_OK`; the router Clash API Native UDP probe also
returned a delay. The temporary UDP echo process and log were removed.

## Fast Open audit fixes (F1/F2) deployment, 2026-09-04

The independent release audit of the Fast Open hotfix named two release
blockers. Both were fixed with minimal changes on top of `c8ebb943bd`,
locked at `742b89aa24131749b62856e5ed9189273a32f26e`, and the router client
was redeployed from a local OpenWrt x86_64 build of exactly that source:

- `153de92c8e` (F1): a Fast Open `Connect()` can complete before the app
  issues a `Read()`; when the CONNECT response then fails asynchronously and
  the server does not FIN the stream, `QuicProxyClientSocket` now completes
  the pending read with the tunnel failure and resets the stream instead of
  waiting for a close that may never come.
- `afce211960` (F2): `SpdyProxyClientSocket::OnHeadersReceived` now fails
  closed on a malformed CONNECT response instead of leaving
  `response_.headers` null for `DoReadReplyComplete()` to dereference.
- `742b89aa24`: deterministic regression `tests/fastopen_async_failure.sh`
  (masque server `--fail_connects`: 502 + no FIN + 500 ms delay) plus the
  `naive_fastopen_fail_runner` target.

Build and deployment evidence (local emergency build, no CI run):

- OpenWrt x86_64 build: `out/OpenWrt/naive`, SHA256
  `6e3a6655415b0f0e4481e1d524f906aa382d2314c5d96001afd7dfabc4b063b2`,
  11,500,944 bytes; dynamic dependencies identical to the previous artifact
  (`libgcc_s.so.1`, `libc.so`).
- Same source, Linux x64: `out/Release/naive`, SHA256
  `d2fbfe24ce1078341237cd45336ba5a8f51a6f377480a5e212d5c4751104c11e`,
  `naive 150.0.7871.63`.
- Pre-deployment backup: `rtr.local:/root/native-udp-deploy/
  native-udp.pre-F1F2-20260904-155351` (previous SHA256 `66035c78...`).
- Deployed binary SHA256 verified on the router before restart; service
  restarted via `/etc/init.d/native-udp restart`, running as `nativeudp`,
  no respawn entries in `logread`.
- `scripts/verify-product-lock.sh` over all four fork checkouts printed
  `PRODUCT_LOCK_OK v150.0.7871.63-2-native-udp-m7`.

Regression matrix on the fixed Linux build (all green):

- `tests/basic.sh`: 56/56 PASS (28 https + 28 http).
- `tests/masque_g1_smoke.sh`, `masque_g2_naive_tunnel.sh`,
  `masque_g3_basic_auth.sh`, `masque_g5_lifecycle.sh`: GREEN.
- `tests/socks5_udp_m2.sh`, `socks5_udp_m3.sh`: GREEN.
- `tests/fastopen_async_failure.sh`: GREEN (3 consecutive runs).
- `naive_quic_congestion_test`: GREEN (M7 G1 markers).
- Unit suite: `naive_socks5_udp_test`, `naive_socks5_server_socket_state_test`,
  `naive_socks5_udp_association_test`, `naive_connect_udp_backend_test`,
  `naive_socks5_udp_fuzz_test` (250k iterations): GREEN.

Round-2 live validation through the unchanged router SOCKS5 listener
(`127.0.0.1:1080`, via SSH tunnel) after replacement:

- GitHub API: 5/5 HTTP 200.
- GitHub home: 5/5 HTTP 200.
- Reddit: TLS/HTTP completed (HTTP 403 from the site, same as Round 1).
- GitHub release asset (12,981,610 bytes): 3/3 complete downloads with
  byte-exact local size.

Sibling observations (documented, not fixed this cycle): datagram socket
pending-I/O completion is already bounded by `OnClose` plus the idle timeout;
`url::SchemeHostPort` with `CHECK_CANONICALIZATION` silently produces an empty
host for unbracketed IPv6 literal proxies (test-only impact; production uses
the hostname proxy). See the status ledger for details.

## Metrics

The server admin API is loopback-only. Query the CONNECT-UDP metrics on the
server with:

```bash
curl -fsS http://127.0.0.1:2019/metrics \
  | grep '^caddy_forward_proxy_connect_udp_'
```

Important semantics:

- `active_associations` is the current admitted association count.
- `active_associations_peak` is the maximum only since the current Caddy
  process started; it is not an all-time persisted peak.
- `associations_total`, closure counters, and the duration histogram are also
  process-local.
- A process restart resets all of these series to zero. Active and peak values
  can become nonzero immediately as the router recreates associations. Capture
  a pre-restart snapshot when longitudinal comparison matters.

The 2026-09-03 deployment observed `active=33`, `peak=64` immediately before
restart. The old process had long-lived CONNECT streams and did not finish its
graceful shutdown before systemd's default 90-second stop timeout, so systemd
killed that old process and then started the verified replacement. This was a
manual replacement, so the new unit reports `NRestarts=0`; that field counts
automatic service restarts, not operator-initiated starts.

## Rollback

The immediately previous binaries remain on their hosts:

- `rtr.local`: `/root/native-udp-deploy/native-udp.pre-F1F2-20260904-155351`
  (previous hotfix SHA256 `66035c78b788dc5e70f9bba112cdb1ddbdcc6765c406d21c7d5d5772e5a4a9fa`)
- `lllinya.com`:
  `/root/native-udp-deploy/naive.pre-standard-20260903-182150`
- `triptrip999.qzz.io`:
  `/root/native-udp-deploy/caddy-naive-udp.pre-standard-20260903-102206`

Restore only the matching role's binary, verify its recorded hash, and restart
that service. Do not change the router's sing-box configuration during a
binary rollback; its SOCKS5 endpoint is deliberately stable.
