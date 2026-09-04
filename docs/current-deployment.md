# Current Native UDP Deployment

Last verified: 2026-09-04 12:56 (Asia/Shanghai)

This page is the authority for what is deployed now. Historical milestone
records in `native-udp-status.md` prove earlier gates, but do not identify the
current production binaries.

## Product lock and manifests

The deployed artifacts derive from [`release/product.lock.json`](../release/product.lock.json)
(`v150.0.7871.63-2-native-udp-m7`, experimental):

| Repository | Locked `master` commit |
| --- | --- |
| `ssharkkky/naiveproxy` | `9842de51eb67902af178615f02f5ba878e8e1505` |
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
| Production client | `rtr.local` (`192.168.2.1`), `/etc/init.d/native-udp` | Local hotfix `c8ebb943bd` (based on locked client), OpenWrt x86_64 | `66035c78b788dc5e70f9bba112cdb1ddbdcc6765c406d21c7d5d5772e5a4a9fa` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
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

- `lllinya.com`:
  `/root/native-udp-deploy/naive.pre-standard-20260903-182150`
- `triptrip999.qzz.io`:
  `/root/native-udp-deploy/caddy-naive-udp.pre-standard-20260903-102206`

Restore only the matching role's binary, verify its recorded hash, and restart
that service. Do not change the router's sing-box configuration during a
binary rollback; its SOCKS5 endpoint is deliberately stable.
