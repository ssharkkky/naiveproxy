# Current Native UDP Deployment

Last verified: 2026-09-04 18:55 (Asia/Shanghai)

This page is the authority for currently deployed binaries. Milestone records
in `native-udp-status.md` are historical verification evidence, not deployment
provenance.

## Product release

The current experimental product release is
[`v150.0.7871.63-3-native-udp-m7`](https://github.com/ssharkkky/naiveproxy/releases/tag/v150.0.7871.63-3-native-udp-m7).
It is defined by [`release/product.lock.json`](../release/product.lock.json),
whose SHA256 is
`d05aed4b0f048ff16f196982e270e08c3c41cbfe89162550455e11131a366d66`.

| Repository | Locked `master` commit |
| --- | --- |
| `ssharkkky/naiveproxy` | `742b89aa24131749b62856e5ed9189273a32f26e` |
| `ssharkkky/forwardproxy` | `4265c663dcaf3981a57f676984d1b0b03615dee0` |
| `ssharkkky/caddy` | `0ea5700f64254ba24e39d57b1febece2fa34927e` |
| `ssharkkky/quic-go` | `c308178d8c77061d5e261ce9df37f2bcc0ab22bf` |

Build provenance is in [current client manifest](../release/manifests/current-client.json)
and [current server manifest](../release/manifests/current-server.json).
NaiveProxy `master` build ref is `2e45c5a6e5ff62bb8eb9c713d5e97db72b22c91a`.
The lock names its runtime-source parent `742b89aa24131749b62856e5ed9189273a32f26e`
to avoid recursively pinning the release metadata commit itself; the intervening
commits change only release metadata, documentation, and CI, with no `src/`
diff.
The server release run [`33860117907`](https://github.com/ssharkkky/naiveproxy/actions/runs/33860117907)
is green. Client run [`33860117894`](https://github.com/ssharkkky/naiveproxy/actions/runs/33860117894)
passed the deployed Linux x64 and OpenWrt x86_64 jobs before deployment, then
completed successfully with all 50 jobs green and zero failures.

## Running artifacts

| Role | Host and service | Release binary SHA256 | Runtime configuration |
| --- | --- | --- | --- |
| Production client | `rtr.local` (`192.168.2.1`), `/etc/init.d/native-udp` | `0bec3c3b2204a56611a1a990511d98df58fa2c5f946640e2b55c22b8ab80cab3` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Validation client | `lllinya.com`, `native-udp-client.service` | `31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d` | SOCKS5 `127.0.0.1:1080`, `bbr1` |
| Production server | `triptrip999.qzz.io`, `native-udp-caddy.service` | `52a1ca4f5cb2829c97af1a32359914baae7b93ca6548d8bb71a6291cc9860e3d` | TCP+UDP `:8443`, `bbr-standard` |

The server binary in the `-3` release bundle is byte-identical to the running
server, so it was verified but not restarted. This avoids unnecessary impact
to the co-hosted website and non-Naive proxy services. The clients were
replaced atomically from release archives and restarted; their stable SOCKS5
listeners and the router's sing-box configuration were not changed.

## Live verification

After the router replacement, requests through its actual
`127.0.0.1:1080` SOCKS5 endpoint completed successfully:

- GitHub API and GitHub home: HTTP 200.
- Reddit: TLS/HTTP completed (HTTP 403 is the site's anti-bot response).
- GitHub release asset: 15,451,894 bytes in 2.44 s, SHA256
  `2277d43b98ec0054280f2ac26b53268bae97682444678a59a657dd565da021d6`.
- Both client service checks confirmed a live process and the same
  `127.0.0.1:1080` listener after restart.
- The `lllinya.com` Linux release client completed SOCKS5 `UDP ASSOCIATE` and
  received a public DNS response through the CONNECT-UDP/HTTP/3 DATAGRAM path.

The deployed client includes the Fast Open response-order fixes
`153de92c8e` and `afce211960` plus deterministic regression `742b89aa24`.
They prevent a CONNECT failure that arrives after Fast Open has returned from
leaving the application read permanently pending, and reject malformed H2
CONNECT headers without a null dereference.

## Metrics

The Caddy admin API is loopback-only. On the server, query CONNECT-UDP metrics
with:

```bash
curl -fsS http://127.0.0.1:2019/metrics \
  | grep '^caddy_forward_proxy_connect_udp_'
```

`active_associations_peak`, totals, closures, and duration histograms are
process-local. A Caddy restart resets them; capture a pre-restart snapshot for
longitudinal comparison.

## Rollback

Immediate client rollback copies are retained at:

- `rtr.local`: `/root/native-udp-deploy/native-udp.pre-release-20260904`
- `lllinya.com`: `/root/native-udp-deploy/naive.pre-release-20260904`

Restore only the matching binary after verifying its hash, then restart its
Naive service. Do not change sing-box or its SOCKS5 destination during a
client rollback.
