# Current Native UDP Deployment

Public record: deployment endpoints and operator-specific paths are
anonymized. Hosts under `example.invalid` and documentation-range addresses
are placeholders, not connection instructions. Real inventory belongs in a
private operations record outside this repository.

Last verified: 2026-09-08 (Asia/Shanghai)

This page is the authority for currently deployed binaries. Milestone records
in `native-udp-status.md` are historical verification evidence, not deployment
provenance.

The September 7 [privacy cleanup](native-udp-status.md#documentation-privacy-cleanup-2026-09-07)
rewrote documentation history and release tags. Source SHAs below and in the
manifests remain the original build provenance; the cleanup record maps the
release inputs to sanitized revisions. Published binaries were not rebuilt.

## Current release and deployment: September 8 release 6 (incremental DNS)

Experimental release
[`v150.0.7871.63-6-native-udp-fastopen-dns`](https://github.com/ssharkkky/naiveproxy/releases/tag/v150.0.7871.63-6-native-udp-fastopen-dns)
is deployed on the server and router. The validation client retains release 5
and was not replaced in this deployment. This section supersedes older
deployment descriptions below.

- Product lock SHA256: `df54c246c590368ebc5559a0ecd6a508274b2c5248fad6f492f5146cd1c929d2`.
- Source: NaiveProxy `4de6443f5ab3842bfead7f65b544207d83d290e3`, forwardproxy
  `cad30c35a736bd856789b3c7318a571d5c6d26ae`, Caddy
  `0ea5700f64254ba24e39d57b1febece2fa34927e`, quic-go
  `c308178d8c77061d5e261ce9df37f2bcc0ab22bf`.
- Product combination [34210375332](https://github.com/ssharkkky/naiveproxy/actions/runs/34210375332)
  passed with `PRODUCT_COMBINATION_OK`, 56 TCP cases, CONNECT/Fast Open, M1-M5,
  server normal/race, and default certificate-verifier checks.
- Server release [34215280766](https://github.com/ssharkkky/naiveproxy/actions/runs/34215280766)
  passed. Client release [34215280733](https://github.com/ssharkkky/naiveproxy/actions/runs/34215280733)
  passed its OpenWrt x86_64 job before router deployment; other platform jobs
  were still running at deployment time.

At deployment closeout, 49/50 client platform/toolchain jobs had succeeded;
only macOS x64 was still building, with no completed failures. The release
remains experimental; this record does not claim the remaining job passed.

| Role | Running binary SHA256 | Deployment |
| --- | --- | --- |
| Server `endpoint-3.example.invalid`, `native-udp-caddy.service` | `3a5b1aa0e467415d93f3c8a13ffb71fcff47e65452a0a178db01be75e4c00daa` | Release 6, 2026-09-08 10:30:53 UTC |
| Router `192.0.2.2`, `/etc/init.d/native-udp` | `c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145` | Release 6, 2026-09-08 10:44:37 UTC |
| Validation client `endpoint-1.example.invalid` | `cdcff06ca5ecaabf839e298b9c1f298482af763c9c7e1f8c8828b83c218e49df` | Retained release 5; not changed in this deployment |

The server now admits ACL-approved A/AAAA candidates as each family completes.
The normal 250 ms stagger, failure-accelerated 100 ms minimum interval, 5 s
per-address timeout, and cancellation policy remain. The client runtime is
unchanged: the new OpenWrt archive contains the same Naive binary as release 5.
The exact new archive was verified and installed, with only the Naive service
restarted and its configuration hash unchanged. No sing-box operations were
performed. Server configuration validation, process SHA verification, and the
expected unauthenticated CONNECT `407` check passed; `NRestarts=0` after the
manual restart does not imply that no restart occurred.

Release-6 provenance recheck: the artifact binary SHA256 matches the deployed
process; `go version -m` shows the `_product/forwardproxy` replacement and
`caddy list-modules` includes `http.handlers.forward_proxy`. Pinned forwardproxy
`cad30c35` contains the 512-total/128-per-client constants from `25b4cd60`,
and the workflow passes that checkout to `build-m7-caddy.sh`. Historical peak
samples do not establish a cap; active/peak values are process-local metrics
and require a live pressure run.

After the server replacement, the existing client passed 96/96 TCP and 4/4 UDP
DNS requests; a failed target completed in 5.205 s. After the client replacement,
three batches each passed 96/96 TCP and 4/4 UDP DNS; failed-target samples completed
in 5.381 s, 5.180 s, and 5.177 s. One successful request in the first batch took 6.350 s.
Before either replacement, UDP samples included 2/4 and 3/4 results as well as
a 4/4 batch; all are retained in the ledger rather than omitted. These bounded
samples do not establish a statistical latency improvement or zero packet loss.

Read-only checks at 11:24:24/27 UTC confirmed the router still running and the
server active/running with `NRestarts=0`; both process hashes matched and both
rollback files were present. This was about 40 minutes after client replacement
and 54 minutes after server replacement. Temporary router installation files
and the deployment probe were removed; rollback binaries were retained.

Immediate rollback, restoring only the named binary and restarting its service:

- Server: `/var/lib/proxy-private/caddy-naive-udp.pre-release-6-20260908T103051Z`,
  SHA256 `d8d886126fee26a2777248b9081566cb79618d407258a690af8ec3c48749d230`.
- Router: `/usr/bin/native-udp.pre-release-6-20260908T104434Z`,
  SHA256 `c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145`.
- Pre-restart server metrics:
  `/var/lib/proxy-private/metrics.pre-release-6-20260908T103051Z.txt`.

Exact job/artifact IDs and archive hashes are recorded in the current
[client](../release/manifests/current-client.json) and
[server](../release/manifests/current-server.json) manifests. Historical
deployment hashes below describe their named releases only.

## Historical release 5 (Fast Open)

The current matching experimental release is
[`v150.0.7871.63-5-native-udp-fastopen`](https://github.com/ssharkkky/naiveproxy/releases/tag/v150.0.7871.63-5-native-udp-fastopen).
This section and the current manifests supersede the historical release-4 and
release-3 records below. The [product lock](../release/product.lock.json)
SHA256 is
`7eed62b94831b2fef231f37662b1d5047108e3ff771715afb01be187dd567d62`.

| Repository | Locked source commit |
| --- | --- |
| NaiveProxy | `4de6443f5ab3842bfead7f65b544207d83d290e3` |
| forwardproxy | `d50ef3ff5c92164ff88e8e7f0a1ff2d342b7ecab` |
| Caddy | `0ea5700f64254ba24e39d57b1febece2fa34927e` |
| quic-go | `c308178d8c77061d5e261ce9df37f2bcc0ab22bf` |

The release includes the CONNECT correctness fixes and the re-enabled learned-
padding Fast Open client behavior. The release tag is a prerelease on
`master`; its client source is locked at `4de6443f5a`. The forwardproxy, Caddy,
and quic-go pins are unchanged from release 4.

- Client release run [34159149117](https://github.com/ssharkkky/naiveproxy/actions/runs/34159149117): all 50 platform/toolchain jobs succeeded.
- Server release run [34159149063](https://github.com/ssharkkky/naiveproxy/actions/runs/34159149063): pinned Linux amd64 package succeeded.
- Product combination [34155408450](https://github.com/ssharkkky/naiveproxy/actions/runs/34155408450): `PRODUCT_COMBINATION_OK`, CONNECT/Fast Open regressions, 56 TCP cases, M1-M5, server normal/race, and certificate-verifier checks passed.

The release is an experimental prerelease. Its Linux x64 client archive,
OpenWrt x86_64 client archive, and Linux amd64 server archive were downloaded
and their SHA256 values matched the published release assets. See
[client](../release/manifests/current-client.json) and
[server](../release/manifests/current-server.json) deployment manifests.
M7 G5 and its independent audit remain deferred.

| Role | Running binary SHA256 | Configuration |
| --- | --- | --- |
| Router `192.0.2.2`, `/etc/init.d/native-udp` | `c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Server `endpoint-3.example.invalid`, `native-udp-caddy.service` | `d8d886126fee26a2777248b9081566cb79618d407258a690af8ec3c48749d230` | TCP/UDP `:8443`, `bbr-standard` |
| Validation client `endpoint-1.example.invalid`, `native-udp-client.service` | `cdcff06ca5ecaabf839e298b9c1f298482af763c9c7e1f8c8828b83c218e49df` | SOCKS5 `127.0.0.1:1080`, `bbr1` |

The exact release binaries were installed without rebuilding. The validation
client and router run the release-5 client artifacts; the server binary already
matched the release-5 server artifact. The server remains active with
`NRestarts=0`. No sing-box process, binary, or configuration was changed.

The current server is active/running with `NRestarts=0`; a read-only check on
2026-09-08 confirmed the release-5 binary SHA above. The validation client is
active/running with `NRestarts=0`; its process executable has the release-5
Linux x64 SHA above.

The release-5 validation record contains 27/27 successful Fast Open enabled
requests and 29/29 disabled requests against the same server. Both runs stayed
at `NRestarts=0`; the enabled candidate was restored after comparison.
The router was then verified read-only with the release-5 OpenWrt binary SHA.

The A/B and matrix evidence, including its limits, is recorded in
`native-udp-status.md`. The sample is controlled smoke evidence rather than a
statistical performance claim.

A subsequent September 8 recheck confirmed all three process/binary hashes
against the same release. Each client's existing SOCKS5 listener passed 8/8
TCP requests and 4/4 UDP DNS queries. Unreachable CONNECT samples completed
in 5.153 s on the production client and 5.143 s on the validation client.
The server and validation service were active with `NRestarts=0`; the router's
Naive service was running. This recheck required no binary replacement or
service restart. See the W4 status ledger for markers and evidence limits.

Immediate rollback files (restore atomically and restart only the affected Naive service):

- Router: `/var/lib/proxy-private/native-udp.pre-release-4-20260905T125944Z`, SHA256 `0bec3c3b2204a56611a1a990511d98df58fa2c5f946640e2b55c22b8ab80cab3`.
- Server: `/var/lib/proxy-private/caddy-naive-udp.pre-release-4-20260905T125728Z`, SHA256 `fe98dd3d5e7bef3544ec02337dfe7b647b283a00608064b2b523f91c66195a8c` (previous local CONNECT hotfix).
- Validation client: `naive.disabled-20260908`, SHA256 `31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d`.

Server pre-restart metrics are retained in
`/var/lib/proxy-private/metrics.pre-release-4-20260905T125728Z.txt`.
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
| Production client | `endpoint-2.example.invalid` (`192.0.2.2`), `/etc/init.d/native-udp` | `0bec3c3b2204a56611a1a990511d98df58fa2c5f946640e2b55c22b8ab80cab3` | SOCKS5 `127.0.0.1:1080`, `bbr1`, user `nativeudp` |
| Validation client | `endpoint-1.example.invalid`, `native-udp-client.service` | `31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d` | SOCKS5 `127.0.0.1:1080`, `bbr1` |
| Production server | `endpoint-3.example.invalid`, `native-udp-caddy.service` | `fe98dd3d5e7bef3544ec02337dfe7b647b283a00608064b2b523f91c66195a8c` | TCP+UDP `:8443`, `bbr-standard` |

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

On `endpoint-1.example.invalid`, temporary SOCKS listeners `11082` (H3) and `11083` (H2)
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
- The `endpoint-1.example.invalid` Linux release client completed SOCKS5 `UDP ASSOCIATE` and
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

Server rollback is `/var/lib/proxy-private/caddy-naive-udp.pre-connect-20260905`,
verified SHA256 `52a1ca4f5cb2829c97af1a32359914baae7b93ca6548d8bb71a6291cc9860e3d`.
Restore it atomically and restart only `native-udp-caddy.service`.

Immediate client rollback copies are retained at:

- `endpoint-2.example.invalid`: `/var/lib/proxy-private/native-udp.pre-release-20260904`
- `endpoint-1.example.invalid`: `/var/lib/proxy-private/naive.pre-release-20260904`

Restore only the matching binary after verifying its hash, then restart its
Naive service. Do not change sing-box or its SOCKS5 destination during a
client rollback.
