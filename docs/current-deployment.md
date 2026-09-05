# Current Native UDP Deployment

Last verified: 2026-09-05 (Asia/Shanghai)

This page is the authority for currently deployed binaries. Milestone records
in `native-udp-status.md` are historical verification evidence, not deployment
provenance.

## Current release and deployment: September 5 release 4

The current matching experimental release is
[`v150.0.7871.63-4-native-udp-m7`](https://github.com/ssharkkky/naiveproxy/releases/tag/v150.0.7871.63-4-native-udp-m7).
This section and the current manifests supersede the historical release-3
record below. The [product lock](../release/product.lock.json) SHA256 is
`fdbaf9c155f9f983ef6d1d7d169a6db793efe4d551a40a3223a43bd05c127c97`.

| Repository | Locked source commit |
| --- | --- |
| NaiveProxy | `c86e73859ea1a65eac467378ba3188edc5145a98` |
| forwardproxy | `d50ef3ff5c92164ff88e8e7f0a1ff2d342b7ecab` |
| Caddy | `0ea5700f64254ba24e39d57b1febece2fa34927e` |
| quic-go | `c308178d8c77061d5e261ce9df37f2bcc0ab22bf` |

Both CONNECT fixes are pushed to their owners' `master`: client `b652d34aa5`
and server `7307332`. Release tag commit
`1ca173d3f2b6e82af543ee4d6c98dfd332f1c1bb` has no `src/` difference from the
locked client. Subsequent master changes before deployment repair CI only.

- Client release run [33961210543](https://github.com/ssharkkky/naiveproxy/actions/runs/33961210543): 50/50 successful jobs, 46 client packages across Linux, Windows, macOS, Android and OpenWrt.
- Server release run [33961210559](https://github.com/ssharkkky/naiveproxy/actions/runs/33961210559): successful Linux amd64 package with Go 1.26.0 and verified module provenance.
- Product combination [33962958046](https://github.com/ssharkkky/naiveproxy/actions/runs/33962958046): `PRODUCT_COMBINATION_OK`, CONNECT, 56 TCP cases, M1-M5, normal/race and default certificate-verifier checks passed.
- Dependency lock [33960919829](https://github.com/ssharkkky/naiveproxy/actions/runs/33960919829): successful.

All 47 packages were downloaded and checked against GitHub asset digests.
The release includes `release-manifest.json` with archive/binary hashes and
run/job/artifact IDs, complete `SHA256SUMS`, `server-go-provenance.txt` and
`server-modules.txt`. See [client](../release/manifests/current-client.json)
and [server](../release/manifests/current-server.json) deployment manifests.
M7 G5 and its independent audit remain deferred; the channel is experimental.

| Role | Running binary SHA256 | Configuration |
| --- | --- | --- |
| Router `192.168.2.1`, `/etc/init.d/native-udp` | `993bdf31785839f0041f1a82ed746f103925c43096daad3bd5fd5ca44f2e5f68` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Server `triptrip999.qzz.io`, `native-udp-caddy.service` | `d8d886126fee26a2777248b9081566cb79618d407258a690af8ec3c48749d230` | TCP/UDP `:8443`, `bbr-standard` |
| Validation client `lllinya.com`, retained release `-3` | `31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d` | Last verified before this deployment; outside replacement scope |

Exact release binaries were installed without rebuilding. Only the two Naive
services were restarted. Router configuration/init-script and server Caddyfile
hashes match pre-deployment values. SOCKS and sing-box configuration were not
changed. Server nginx, hysteria-server, x-ui and singbox-merger PIDs remained
unchanged.

Server activation: `2026-09-05T12:57:30Z`, active/running, PID `294733`,
`NRestarts=0`. The bounded 15-second stop completed without SIGKILL. The first
readiness poll raced startup; the subsequent poll returned expected `407` and
TCP/UDP listeners passed.

Router successful replacement began at `2026-09-05T12:59:44Z`. The first
attempt passed TCP but timed out on public UDP DNS and automatically restored
the old binary. The second passed eight HTTPS requests, four DNS queries and
bounded target failure in 5.355 seconds. Two follow-up full probes passed
another 16 HTTPS requests and eight DNS replies, with failed targets exiting
in 5.169 and 1.521 seconds. A third follow-up passed TCP but timed out on DNS.

An isolated old-release client on temporary SOCKS port `11084`, under the
same `nativeudp` user and against the same new server, provided a comparison:
12 independent DNS queries with three-second deadlines and no retransmission
yielded old client 10 replies/2 timeouts and new client 9 replies/3 timeouts.
The user confirms the test link has poor quality and random packet loss;
these observations are consistent with that condition. No packet capture
identified the loss location, and the sample does not establish statistical
equivalence. This was not an old-server A/B comparison. Controlled CI
DNS/HTTP3 and the release binary's complete local M4 suite passed. Temporary
comparison processes and configuration were cleaned up.

Immediate rollback files (restore atomically and restart only the Naive service):

- Router: `/root/native-udp-deploy/native-udp.pre-release-4-20260905T125944Z`, SHA256 `0bec3c3b2204a56611a1a990511d98df58fa2c5f946640e2b55c22b8ab80cab3`.
- Server: `/root/native-udp-deploy/caddy-naive-udp.pre-release-4-20260905T125728Z`, SHA256 `fe98dd3d5e7bef3544ec02337dfe7b647b283a00608064b2b523f91c66195a8c` (previous local CONNECT hotfix).

Server pre-restart metrics are retained in
`/root/native-udp-deploy/metrics.pre-release-4-20260905T125728Z.txt`.
Metrics remain process-local and reset on restart. Earlier rollback files
remain retained. SOCKS applications may still see EOF/reset for failed targets.

## Historical release 3 and local hotfix

Everything below describes the superseded September 4 release and the earlier
September 5 hotfix. Use the section above for current deployment facts.

The published release below remains the client baseline. On 2026-09-05 the
server alone received a locally built CONNECT hotfix from forwardproxy
`7307332b312f29ce5f5f1cb638e4a5b993e95442`, retaining the same Caddy and
quic-go pins. This hotfix is not part of the published `-3` bundle or product
lock; its actual provenance is in the current server manifest.

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

| Role | Host and service | Running binary SHA256 | Runtime configuration |
| --- | --- | --- | --- |
| Production client | `rtr.local` (`192.168.2.1`), `/etc/init.d/native-udp` | `0bec3c3b2204a56611a1a990511d98df58fa2c5f946640e2b55c22b8ab80cab3` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Validation client | `lllinya.com`, `native-udp-client.service` | `31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d` | SOCKS5 `127.0.0.1:1080`, `bbr1` |
| Production server | `triptrip999.qzz.io`, `native-udp-caddy.service` | `fe98dd3d5e7bef3544ec02337dfe7b647b283a00608064b2b523f91c66195a8c` | TCP+UDP `:8443`, `bbr-standard` |

At the September 4 release deployment the server was byte-identical and did
not need a restart. The September 5 CONNECT hotfix replaced it and restarted
only `native-udp-caddy.service` at 09:44:17 UTC. Other services were outside
the authorized change scope. The first hotfix replacement rolled back because
the health check rejected the expected unauthenticated `407`; the corrected
check passed on retry. The client services still run the September 4 release
binaries and retain their SOCKS5 listeners and sing-box configuration.

## CONNECT hotfix validation

The server now acknowledges TCP CONNECT after a successful target dial and
races ACL-approved numeric addresses with a 250 ms fallback delay. Failed
dials return `502` or `504`. Client fix `b652d34aa5` additionally disables the
padding-cache-triggered early CONNECT success in H2/H3.

On `lllinya.com`, temporary SOCKS listeners `11082` (H3) and `11083` (H2)
validated that client against the actual `8443` server. Each completed 96/96
normal TCP/TLS/HTTP connections at concurrency 8; maximum total times were
1.367 s and 1.518 s respectively. Four UDP DNS queries through H3 passed.
The controlled unreachable target produced bounded failures in about
0.95-5.47 s, versus the original approximately 10.01 s application timeout.
SOCKS applications can still see EOF/reset on failed targets; these results
do not claim elimination of all resets or all Internet connection failures.

The temporary client was the OpenWrt/musl build, SHA256
`44ac8d16f6d8ed7508f060e06303a5d1224a916c8627cb5b40aa9f00a3e51da2`,
run with its loader and `libgcc_s` in a private validation directory. The
local native Linux build needs glibc 2.42 while that host provides 2.35.
The production certificate verifier was retained. Neither permanent client
service was replaced; a matching published client release remains separate.
Both temporary client services and the client/server validation directories
were removed after testing. The original client and hotfixed server services
were rechecked as active with the hashes listed above; the server rollback
binary remains available.

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

Server rollback is `/root/native-udp-deploy/caddy-naive-udp.pre-connect-20260905`,
verified SHA256 `52a1ca4f5cb2829c97af1a32359914baae7b93ca6548d8bb71a6291cc9860e3d`.
Restore it atomically and restart only `native-udp-caddy.service`.

Immediate client rollback copies are retained at:

- `rtr.local`: `/root/native-udp-deploy/native-udp.pre-release-20260904`
- `lllinya.com`: `/root/native-udp-deploy/naive.pre-release-20260904`

Restore only the matching binary after verifying its hash, then restart its
Naive service. Do not change sing-box or its SOCKS5 destination during a
client rollback.
