# NaiveProxy Native UDP Project Status

Last updated: 2026-09-03 (Asia/Shanghai)

Documentation entry point: [`README.md`](README.md). Active milestone plan:
[`m7-execution-plan.md`](m7-execution-plan.md).

This is the execution ledger for the native UDP project. The development plan
defines scope and design; this file records what has actually been built and
verified. Update it at every completed G target and milestone.

## Overall milestone status

| Milestone | Status | Verified result | Next gate |
| --- | --- | --- | --- |
| M0 — baseline and guardrails | Complete | Stable Chromium 150 tag, development branch, Release build, TCP baseline | None |
| M1 — Chromium integration spike | Complete and independently audited | Real IPv4/IPv6 tunnel, auth echo, lifecycle and NetLog evidence; `agy` returned `AUDIT_PASS` | None |
| M2 — SOCKS5 UDP ingress | Complete, audited, and committed | Codec, handshake, real relay, fake backend, deterministic lifecycle and 56 TCP regressions pass; `agy` returned `AUDIT_PASS`; commit `fe817a87` | None |
| M3 — native UDP client data path | Complete and independently audited | Full client path, controlled interoperability, recovery, all limits/lifecycle cases, complete regressions, three stress runs; `agy` returned `AUDIT_PASS` with zero blocker/high/medium | None |
| M4 — production server path | Complete and independently audited | Reproducible builds, full server/client regressions, independent RFC 9298 matrix, lifecycle, race, privacy, artifact checks, and `AUDIT_PASS`; final server commit `8f044e2`, Caddy `cce894a8` | None |
| M5 — end-to-end MVP | Complete and independently audited | Full product matrix, shipped default-verifier client, lifecycle/no-replay, complete regressions, three fresh-root repetitions, artifact closeout, and `AUDIT_PASS` | None |
| M6 — hardening and release candidate | **Complete**; G0-G6 closed; release candidate qualified | All G0-G6 gates closed; macOS arm64, Linux x64, Windows x64, and Android arm64 platform qualification verified; cross-platform wire gate `13df84bfd9` passes; independent release-candidate audit `AUDIT_PASS` (`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`); merged to `master` at `fcf3bb36f3` | None |
| M7 — BBR congestion control | **G4 complete; G5 intentionally deferred** | Correctly combined client/server BBR passed the fixed-loss reference parity gate; `M7_G4_PARITY_OK` recorded below. Full regression/audit G5 is outside the requested closeout scope. | None |

M1 is complete as an integration spike. M2 supplies the local SOCKS5 UDP
ingress and retains its test-only echo/no-backend modes. M3 G0–G6 compose
that ingress with the real M1 CONNECT-UDP tunnel in production while keeping
the M2 runner independent, and the independent final audit passed. Remaining
M6 work is sequenced in `docs/m6-execution-plan.md` and summarized in
`docs/native-udp-development-plan.md`.

### Overall progress estimate

- Milestone count: M0–M6 are complete, 7 of 7 milestones, or 100%.
- The M6 release candidate is qualified: independent audit `AUDIT_PASS` and
  marker `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closed M6; merged to `master`
  at `fcf3bb36f3`. Not yet a production release (see
  `native-udp-release-guide.md`).
- Chromium-driven native UDP client: M1-M3 are independently audited; the M5
  production-context ordering fix `333b7cb253` passed the complete owner matrix
  and is included in the completed M5-G6 audit boundary.
- Production Caddy/`forwardproxy` native UDP server: 100% complete and
  independently audited.
- End-to-end product MVP: 100% complete and independently audited. Release
  hardening is complete (M6 closed; release candidate qualified at `fcf3bb36f3`).

Current remaining planning range:

| Remaining milestone | Estimated effort |
| --- | ---: |
| (none) | 0 |
| **Total remaining** | **0** |

All M0-M6 milestones are complete. The native UDP release candidate is
qualified: the independent G6 audit returned `AUDIT_PASS` and the final marker
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closed M6; the full stack is merged to
`master` at `fcf3bb36f3`.

## M7 implementation evidence and G4 closeout (G5 deferred)

M7 work has started under [`m7-execution-plan.md`](m7-execution-plan.md).
The client G1 change is on `master` at `b26229ec06`; the server-side
quic-go fork is published on its default `master` at
`af5cf06bcc93b32ac19bad75f4669465ed6e8f11`. It adds selectable Hy2-derived
BBR profiles and preserves CUBIC as the zero-value default. The Caddy G3
integration is now merged to the fork default `master` at `e111f21c85`; the
forwardproxy server/build line is on its default `naive` branch at
`0db468de81` and directly pins those default-branch snapshots.

Verified on 2026-09-02:

```text
quic-go fork: go build ./..., go vet ./..., go test ./...                 PASS (in fork worktree)
quic-go fork (current `f84ad47`): go test ./...                          PASS (27 packages)
client `naive_quic_congestion_test` (Release binary via musl loader)       PASS; `M7_G1_CLIENT_BBR_OK`
Caddy fork:   go test ./..., go vet ./..., go build ./cmd/caddy           PASS
forwardproxy: focused M4 G0-G4 tests and go test ./...                    PASS
forwardproxy: go test -race ./...                                         PASS
combined Caddy+forwardproxy build with explicit quic-go replace            PASS
```

The combined server binary linked against the exact quic-go pseudo-version
`v0.59.1-0.20260901171950-f84ad47630af`. The G5 cross-process server script
was not run to completion because the fixed test Caddyfile attempted to bind
`:80`, already occupied on this host; no existing process was disturbed.

The independent scoped server validation found no protocol or CUBIC-path
regression. The former publication/pin blocker is closed by the published
Caddy commit and the two direct forwardproxy pins; a formal M7 scoped
`AUDIT_PASS` has not been run. The full M1-M6/56-case matrices and
cross-platform rows belong to G5, which is intentionally deferred after the
G4-only closeout.

### M7 runtime smoke (2026-09-02)

An isolated deployment under `/root/native-udp-m7-test` was run on the
authorized client/server hosts and then removed. It used separate ports
18443/18444 and separate SOCKS listeners 11080/11081; existing Naive 1080,
web listeners, Hysteria, Xray, frps, and Docker services were not changed.

- BBR and CUBIC HTTP/3 CONNECT-UDP probes each returned 32/32 paced UDP echo
  datagrams (1,200-byte payloads).
- Through each SOCKS listener, three authenticated downloads of the same
  10 MiB file completed with HTTP 200. BBR speeds were 3.66–3.94 MB/s and
  CUBIC speeds 3.68–4.07 MB/s on this clean path; this is a smoke result, not
  the required lossy-path G4 parity evidence.
- A burst test that sent hundreds of datagrams at once reached the intentional
  per-target queue bound (16); paced testing avoided that bound. No protocol
  failure was inferred from the burst result.
- Cleanup verified no `/root/native-udp-m7-test` processes or directories
  remained and removed the temporary UFW rules for ports 18443/18444.

### M7 G4 fixed-loss parity closeout (2026-09-02)

The final G4 run used `lllinya.com` as the client and `triptrip999.qzz.io` as
the server. It used the frozen `loss` profile (seed `202`, 5% independent
loss in each direction, no added delay) on isolated UDP relay port `18444`.
The temporary Caddy binary was built from Caddy `3bcce47f`, forwardproxy
`c329155`, and quic-go `f84ad47630af`; `go version -m` resolved the exact
quic-go pseudo-version and `caddy list-modules` contained
`http.handlers.forward_proxy`. The client was the G1 Release binary from
NaiveProxy `71dc1dfb13`, configured with `quic-congestion=bbr1` or `cubic`.
Production ports, services, and configurations were not changed.

For each profile, seven 20 MiB downloads and seven 20 MiB uploads were run
through one SOCKS5 listener. CUBIC's 90-second samples are explicitly
right-censored when the transfer did not finish; BBR samples all completed:

```text
TCP download, single connection (curl speed_download)
  BBR:   2,521,306 .. 3,074,087 B/s, median 2,748,313 B/s (2.75 MB/s)
  CUBIC:    55,946 ..    64,144 B/s, median    59,649 B/s (0.060 MB/s)
  ratio: 46.1x; BBR median exceeds the 500 KB/s and 5x G4 thresholds

TCP download, eight parallel connections (batch wall time)
  BBR:   55.722, 56.262, 57.605, 61.732, 62.715, 63.834, 66.272 s
         median aggregate 2,591 KB/s (all 8/8 streams completed)
  CUBIC: 30-second observation batches; median aggregate 59.7 KB/s
         (all streams were short reads, no batch completed the 20 MiB object)

TCP upload, single connection (curl speed_upload)
  BBR:   938,693 .. 1,127,725 B/s, median 1,095,133 B/s; 7/7 HTTP 200
  CUBIC: 106,312 .. 174,034 B/s, median 115,052 B/s; 0/7 completed in 90 s

UDP application probe, 20 paced 1,200-byte echo datagrams per round
  BBR:   20, 18, 17, 19, 17, 18, 16; median 17/20 (85%)
  CUBIC: 20, 16, 16, 17, 18, 19, 16; median 17/20 (85%)
```

The existing Hy2 service was also run through a second isolated shaper
(`18445 -> 8444`) as a comparator: three 20 MiB downloads completed at
2.19–2.40 MB/s, three uploads at 1.21–1.25 MB/s, and seven UDP rounds had a
median 18/20 replies. This is supporting parity evidence, not a Naive gate.

The main shaper recorded `1,608,906` forwarded and `84,510` dropped packets
(5.00% drop); the Hy2 shaper recorded `136,203` forwarded and `7,143`
dropped (4.99%). No paced-probe queue overflow occurred. Point-in-time BBR
snapshots were approximately 6% server CPU and 2% client CPU; CUBIC's
right-censored throughput and the identical shaper policy show no performance
regression, but these CPU readings are observational rather than a dedicated
CPU benchmark.

All temporary clients, Caddy, HTTP/UDP targets, shapers, files, and UFW rules
were removed. Post-cleanup checks found only the production client on `1080`,
production Caddy on `8443`, and Hy2 on `8444`; all three remained active.
Marker: `M7_G4_PARITY_OK`.

### M7 corrected BBR deployment diagnostic (2026-09-02)

The earlier lossy comparisons below did not exercise server-side BBR. Binary
provenance inspection found that the temporary client contained G1 and accepted
`quic-congestion=bbr1`, but the Caddy binary actually serving the comparison
contained `forward_proxy` with upstream `quic-go v0.59.0`. A separate Caddy
binary contained the M7 quic-go fork but did not contain the `forward_proxy`
module. Consequently, the alleged Naive BBR path still used server CUBIC.

The diagnosis was repeated with one combined binary built from exact local
worktrees: Caddy `3bcce47f`, forwardproxy `c329155`, and quic-go `f84ad47`.
`go version -m` and `caddy list-modules` verified all three replacements and
`http.handlers.forward_proxy` before deployment. On the authorized
`lllinya.com` client to `triptrip999.qzz.io` server path:

```text
5% bidirectional loss, seed 202, 20 MiB download
Naive BBR:   20 MiB in 8.38 s, approximately 2.50 MB/s
Naive CUBIC: 1.95 MiB in 35 s, approximately 55 KB/s (timed out)
Hy2 BBR:     20 MiB in 8.29 s, approximately 2.53 MB/s
```

Server qlog confirmed the mechanism. Correct BBR grew its congestion window
from 40 KiB to 1.22 MiB under 5% loss and ended near 1.17 MiB. CUBIC reached
only 79 KiB and ended near 10 KiB. A clean-path 20 MiB BBR run completed in
about 2.6 seconds and grew the server window to 6.4 MiB. By contrast, qlog from
the wrongly deployed server reduced 40,960 bytes to 28,672 bytes on its first
loss (the CUBIC 0.7 factor) and never exceeded 40 KiB.

This isolates the prior throughput gap to the server deployment artifact, not
the Naive HTTP/3 CONNECT or forwardproxy data path: with the correct BBR
binary, Naive and Hy2 were equal within about 1.1% in this diagnostic. This was
one download sample, not the complete G4 TCP upload/download, UDP, and repeated
median qualification matrix. All isolated listeners, clients, qlogs, shapers,
temporary files, and UFW rules were removed; production services were not
changed.

### M7 G4 fixed-loss comparison (2026-09-02, superseded)

Using `lllinya.com` as the client, `triptrip999.qzz.io` as the server, and a
userspace UDP shaper with fixed seed `202`, bidirectional 5% loss was applied
to isolated relay ports. Three 10 MiB authenticated downloads were attempted
through each outer-QUIC profile:

```text
BBR   32.5 KB/s, 34.4 KB/s, 54.6 KB/s   median 34.4 KB/s
CUBIC 31.1 KB/s, 31.9 KB/s, 48.9 KB/s   median 31.9 KB/s
```

This result is invalid as a BBR comparison because its server binary used
upstream quic-go CUBIC, as established by the corrected deployment diagnostic
above. The observed 7.8% difference must not be used as M7 performance
evidence.

The UDP application probe did not establish a usable echo association under
this loss profile, so no UDP parity claim is made. The isolated Caddy/client,
shaper processes, directories, and temporary firewall rules were removed after
the run. G4 remains **not qualified** and G5/final audit must not be marked
complete.

### M7 UDP/Hy2 retry (2026-09-02)

The prior failed UDP result was a test-topology/handshake failure: no
CONNECT-UDP association had been established. With Naive BBR and CUBIC pointed
directly at the production Caddy H3 listener (`triptrip999.qzz.io:8443`), paced
1,200-byte UDP echo probes returned 20/20 for both. The existing Hysteria
service on `:8444` also returned 20/20 through a temporary client on
`lllinya.com`.

Three Hy2 downloads of the 10 MiB test file completed at 3.21–3.62 MB/s. In
the same clean-path retry, one Naive BBR download completed at 2.88 MB/s and
one Naive CUBIC download at 3.19 MB/s. These results are similar in scale and
do not qualify the lossy G4 gate. All temporary clients, echo service,
directories, and firewall rules were removed; production listeners were not
changed.

### M7 5% loss retry with Hy2 (2026-09-02; superseded)

For an additional stress comparison, a fixed-seed userspace shaper applied
5% bidirectional random loss to isolated Naive BBR, Naive CUBIC, and the
existing Hy2 service. The client remained `lllinya.com` and the server
remained `triptrip999.qzz.io`.

- TCP 10 MiB downloads: Naive BBR samples were approximately 47–58 KB/s;
  Naive CUBIC samples approximately 48–55 KB/s. Hy2 completed at 1.61 and
  1.68 MB/s in two samples.
- UDP echo: Naive BBR and CUBIC each received 0/5 paced probes; Hy2 received
  3/5. This is a loss survivability result, not a throughput claim.

The Naive BBR throughput comparison in this run is invalid because server-side
BBR was not active. The UDP association observations remain topology evidence,
but do not compare BBR implementations. The earlier Naive UDP 0/20 result was
therefore not a protocol-path failure: at 5% loss the Naive CONNECT-UDP
control/association exchange can still
complete, while the already-established Hy2 client retained a usable UDP
forwarding session. UDP payloads themselves are not retransmitted by either
proxy; Hy2's advantage here is its established-session/loss handling rather
than a generic property of UDP. All temporary processes, directories, and
firewall rules were removed after this run.

### M7 true 50% loss retry (2026-09-02; BBR comparison superseded)

The comparison was repeated with explicit `--loss-percent 50` rather than the
5% named profile above. Shaper logs confirmed near 1:1 drop/forward counts on
all three paths. TCP 10 MiB attempts produced:

```text
Naive BBR:   48,898 bytes in 30 s, 1.6 KB/s (timed out)
Naive CUBIC: connection reset after 5.4 s, 0 bytes
Hy2:         1,457,922 bytes in 30 s, 48.6 KB/s (timed out)
```

The Naive row labeled BBR did not have server-side BBR active and is invalid as
a congestion-control comparison. The paced UDP probe (five 1,200-byte packets,
500 ms apart) received `0/5` for Naive BBR, `0/5` for Naive CUBIC, and `0/5`
for Hy2. At true 50% loss,
none of the protocols established a usable UDP association; this is a
survivability result, not a throughput comparison. All test processes,
directories, and temporary UFW rules were removed.

## M1 detailed status

### Foundation completed before G1

- Reused Chromium's RFC 9298 URL construction next to
  `QuicProxyDatagramClientSocket`; target hosts are escaped correctly.
- Added `NaiveQuicProxyStreamRequest` around
  `QuicSessionRequest -> session handle -> RequestStream()`.
- Added dormant `NaiveConnectUdpTunnel`; it acquires a QUIC request stream and
  passes it to `QuicProxyDatagramClientSocket::ConnectViaStream()`.
- Retained the QUIC session handle for the full datagram socket lifetime.
- Added cached preemptive proxy authentication through `HttpAuthController`.
  The first CONNECT-UDP can use credentials already loaded into
  `HttpAuthCache`. Interactive 407 restart is explicitly deferred because it
  requires a fresh QUIC stream and an upper-layer retry loop.
- Kept SOCKS ingress and the existing TCP `NaiveConnection` path untouched.
- Repeated Release builds and the complete existing TCP HTTP/HTTPS/auth/chain
  regression suite successfully after the changes.

### G1 — controlled HTTP/3 CONNECT-UDP endpoint: complete

Implemented and verified:

- `naive_masque_server`: controlled server using the exact QUICHE revision
  vendored by Chromium. It logs redacted Extended CONNECT metadata.
- `naive_masque_probe`: independent QUICHE client that calls
  `MasqueClientSession::SendPacket()` directly; it does not use Naive's tunnel.
- `masque_udp_echo.py`: local UDP echo fixture.
- `masque_g1_smoke.sh`: repeatable build/certificate/start/probe/cleanup test.
- A Chromium-local `quiche_tool_support` GN target exposing only CLI/test
  support; the missing `quic_trace` protobuf is deliberately excluded.

Verified evidence:

```text
READY masque=h3-connect-udp bind=[::]:19661 authority=[::1]:19661
CONNECTED proxy=https://[::1]:19661/... target=127.0.0.1:19001
DATAGRAM_ECHO_OK from=127.0.0.1:19001 bytes=19 payload=g1-connect-udp-echo
CONNECT_HEADERS method=CONNECT protocol=connect-udp scheme=https
  authority=[::1]:19661
  path=/.well-known/masque/udp/127.0.0.1/19001/
RX bytes=19 hex=67312d636f6e6e6563742d7564702d6563686f
G1_MASQUE_SMOKE_OK
```

The long-running developer endpoint was also started on UDP `[::]:9661`, with
the echo target on `127.0.0.1:19000`. Its certificate is temporary test data
under `/tmp`; the repeatable smoke script generates its own certificate and
does not depend on that process remaining alive.

G1 does **not** prove Naive authentication or `NaiveConnectUdpTunnel` runtime
behavior. The independent probe intentionally logged
`proxy_authorization=absent`. Those are G2/G3 gates.

### G2 — real Naive tunnel runner: complete

Goal:

- Add a test-only executable that creates the same real `URLRequestContext` /
  `HttpNetworkSession` dependencies used by Naive.
- Configure a `quic://` proxy pointing at the controlled G1 endpoint.
- Invoke `NaiveConnectUdpTunnel` directly, without SOCKS5 ingress.
- Write one datagram and read its echo through the returned
  `DatagramClientSocket`.
- Emit deterministic state/result output suitable for automation.

Completed G2 substeps:

1. **G2-A:** isolated the smallest real session/context initialization boundary.
2. **G2-B:** compiled a runner through `NaiveConnectUdpTunnel::Start()`.
3. **G2-C:** added asynchronous write/read pumps and timeout handling.
4. **G2-D:** passed the controlled endpoint echo; regression verification is
   recorded below.

Verified evidence:

```text
CONNECT_UDP_URL_CONSTRUCTION_OK
SESSION_READY proxy=[quic://[::1]:19662]
CONNECT_UDP_OK
DATAGRAM_WRITE_OK bytes=20
DATAGRAM_ECHO_OK bytes=20 payload=g2-naive-tunnel-echo
CONNECT_HEADERS method=CONNECT protocol=connect-udp scheme=https
  authority=[::1]:19662
  path=/.well-known/masque/udp/%3A%3A1/19002/
  capsule_protocol=?1 proxy_authorization=absent
G2_NAIVE_TUNNEL_OK
```

The first real run failed with `QUIC_TLS_CERTIFICATE_UNKNOWN`. That exposed a
useful Chromium constraint: `QuicSessionPool` copies `QuicParams` during
`URLRequestContext::Build()`. The test-only self-signed origin must therefore
be installed before `Build()`, not mutated afterward. The runner now follows
that ordering without weakening production certificate verification.

G2 now runs both the QUIC proxy and UDP target on IPv6 loopback. It verifies
the bracketed IPv6 proxy authority and RFC 9298 target variable encoding
(`::1` becomes `%3A%3A1`, without authority-style brackets).

### G3 — cached Basic pre-authentication: complete

The controlled endpoint can require Basic proxy authentication without
logging credentials. A repeatable test verifies three independent client
processes:

- no credentials: rejected, `ERR_TUNNEL_CONNECTION_FAILED`;
- wrong cached credentials: header present but rejected with the same error;
- correct cached credentials: first CONNECT-UDP contains
  `Proxy-Authorization` and datagram echo succeeds.

```text
AUTH_DECISION rejected
AUTH_DECISION rejected
AUTH_DECISION accepted
DATAGRAM_ECHO_OK bytes=21 payload=g3-authenticated-echo
G3_BASIC_AUTH_OK
```

This proves cached/preemptive Basic authentication. It does not claim that a
single tunnel object can recover from a 407 challenge: the current object
returns the rejection and an upper layer would need a fresh request stream.

### G4 — authenticated bidirectional tunnel evidence: complete

G3's accepted case supplies G4's required end-to-end evidence through the
real Naive tunnel: authenticated Extended CONNECT headers, the RFC 9298 target
path, one successful write callback, an identical read callback, and the UDP
echo fixture's peer/byte log. Production SOCKS ingress remains dormant.

### G5 — lifecycle and failure behavior: complete

Completed:

- 407 rejection with absent and wrong credentials returns a deterministic
  error without a datagram write.
- Destroying `NaiveConnectUdpTunnel` while its connected socket has a pending
  `Read()` cancels the callback safely. A 200 ms grace window completes with
  `PENDING_READ_DESTRUCTION_OK`; no callback, UAF or crash occurs.
- Destroying the tunnel while the server deliberately leaves CONNECT pending
  produces `CONNECT_PENDING_DESTRUCTION_OK`; the server proves the request
  reached `CONNECT_ACTION ignored` before the test exits.
- Closing all Chromium QUIC sessions with a pending datagram read, then
  destroying the tunnel, produces `SESSION_SHUTDOWN_DESTRUCTION_OK` without a
  callback after destruction.
- The lifecycle test writes a real Chromium NetLog and asserts both the
  `QUIC_PROXY_DATAGRAM_CLIENT_SOCKET` source and `connect-udp` request evidence.

```text
PENDING_READ_DESTRUCTION_OK
NET_LOG_WRITTEN path=.../pending-read-netlog.json
SESSION_SHUTDOWN_ISSUED
SESSION_SHUTDOWN_DESTRUCTION_OK
CONNECT_PENDING
CONNECT_PENDING_DESTRUCTION_OK
CONNECT_ACTION ignored
G5_LIFECYCLE_OK
```

### Final M1 Chromium API boundary

- `NaiveQuicProxyStreamRequest` is the only Naive-owned adapter into
  `QuicSessionRequest`. It requests `SessionUsage::kProxy`, retains the QUIC
  session handle, requests one HTTP/3 stream, and exposes only the stream plus
  local/peer address and user-agent metadata.
- `NaiveConnectUdpTunnel` composes that stream with
  `QuicProxyDatagramClientSocket::ConnectViaStream()`. Declaration order makes
  the datagram socket die before the retained session handle.
- The CONNECT-UDP socket builds the RFC 9298 default template, optionally uses
  Naive's existing cached `HttpAuthController`, and continues to support the
  generic Chromium caller with no auth controller.
- A 407 is an explicit tunnel failure. Retrying requires a new stream and is
  deferred to an upper-layer association/retry owner; M1 does not imply
  interactive challenge recovery.
- Socket disconnection follows Chromium's `Socket` contract: pending callbacks
  may be cancelled rather than invoked. The M3 association owner destroys its
  tunnel when the session/control association closes.
- No SOCKS command parsing, `NaiveProxy::DoConnect()` branching, UDP relay,
  production server, or TCP data path was added in M1.

## M2 execution ledger — SOCKS5 UDP ingress

At M2 completion, the milestone was intentionally limited to a local SOCKS5
UDP ingress path with an injected fake `DatagramBackend`; production
CONNECT-UDP integration remained deferred. M3 later completed that production
composition. The existing TCP data mover is unchanged.

### M2 architecture

```text
NaiveProxy::DoConnect()
        |
        +-- HTTP / redir
        |      └── existing NaiveConnection path
        |
        └-- SOCKS5 two-stage handshake
                 |
                 +-- CONNECT
                 |      └── success response → existing NaiveConnection
                 |
                 └-- UDP ASSOCIATE
                        ├── non-quic:// → reply 0x01 and close
                        └── quic://
                               ├── bind local UDP relay
                               ├── return real BND.ADDR/BND.PORT
                               └── Socks5UdpAssociation
                                      └── M2 fake DatagramBackend
```

`NaiveProxy::DoConnect()` installs an independently owned pending SOCKS
handshake and immediately resumes accepting. Its request-completion callback
branches on the parsed command before any `NaiveConnection` is constructed.

### M2-G0 — interface freeze and test skeleton

Status: complete.

Completed:

- Added standalone SOCKS5 UDP test target:
  - `naive_socks5_udp_test`
- Added minimal test executable:
  - `tools/naive/naive_socks5_udp_test_bin.cc`
- Verified independent build path without touching NaiveProxy runtime.

Verified marker:

```text
M2_SOCKS5_UDP_TEST_SKELETON_OK
```

- Added the standalone codec target, deterministic SOCKS state-machine target,
  real-loopback integration runner, and independent Python RFC 1928 oracle.
- Defined `Socks5UdpAssociation` and its fake-backend boundary as M2 scope.
- Kept the real M1 `NaiveConnectUdpTunnel` out of the M2 ingress path.
- Avoided a dependency on the unavailable full Chromium `net_unittests` graph.

### M2-G1 — RFC 1928 UDP codec

Status: complete.

Implemented:

- Isolated span-based RFC 1928 codec and structured error model.
- Exact IPv4, IPv6, and domain parsing/serialization.
- Binary and empty payloads, port `0`/`65535`, and 255-byte domains.
- Table-driven fixed-wire, round-trip, boundary, malformed, truncation, RSV,
  FRAG, invalid address and invalid-build cases.
- Dedicated fragment error so the association can account for drops without
  logging destinations or payloads.

Verified marker:

```text
M2_G1_CODEC_OK
```

### M2-G2 — SOCKS5 two-stage handshake

Status: complete.

- Split request parsing from reply writing through `ReadRequest()` and
  `WriteReply()` while retaining legacy one-shot `Connect()` behavior.
- Exposed typed command/reply values and the parsed request endpoint.
- Serialize the caller-provided IPv4 or IPv6 bound endpoint.
- A deterministic scripted `StreamSocket` test covers all-sync, all-async,
  byte-fragmented command reads, partial reply writes, mixed phase modes, and
  cancellation during pending read/write.

Verified marker:

```text
M2_G2_DETERMINISTIC_STATE_MACHINE_OK
```

### M2-G3 — NaiveProxy command branching

Status: complete.

- Allocate the connection ID on entry to `DoConnect()`, before starting any
  independent SOCKS asynchronous operation.
- Keep pending handshakes in an ID-keyed owning map while the main accept loop
  continues.
- CONNECT writes the byte-identical legacy success response and transfers the
  same handshaken socket to the existing `NaiveConnection` path.
- BIND and unknown commands return `0x07`.
- UDP on a non-QUIC chain, or without an installed backend, writes `0x01` and
  closes; unsupported UDP is neither acknowledged with success nor left for
  silent data-path drops.

### M2-G4 — real UDP relay and BND endpoint

Status: complete.

- Freeze the TCP peer before sending success, then bind a UDP relay to the
  concrete local control address and address family with an ephemeral port.
- Return that socket's actual `BND.ADDR/BND.PORT`; bind or address lookup
  failure retains the default `0x01` response and closes.
- Real IPv4 and IPv6 clients send datagrams to the independently decoded reply
  endpoint and receive responses.

### M2-G5 — Socks5UdpAssociation and fake backend

Status: complete.

- Own the handshaken TCP control connection, bound UDP relay and injected
  backend as one association.
- Match the normalized TCP peer IP, enforce a requested source port, or learn
  a wildcard port only after the first valid RFC 1928 packet.
- Drop malformed, fragmented, wrong-port and wrong-IP sources without pinning
  or terminating a healthy association. Fragment warnings are rate-limited at
  powers of two.
- Keep one backend send in flight, serialize relay writes, and cap the response
  queue at 64 datagrams with observable drops.
- Post initial pumps, bound synchronous read loops to 32 operations before
  yielding, and guard synchronous backend callback reentrancy.
- Closing the TCP control channel terminates the relay; idle cleanup shares the
  existing proxy cleanup timer.
- The M2 runner injects a synchronous echo backend only in tests. The
  production binary has no fake backend and therefore cannot expose fake UDP.

### M2-G6 — cleanup and audit

Status: complete and independently audited.

Verification matrix already passing:

- IPv4, IPv6, and domain targets.
- Non-QUIC `0x01` response.
- Correct BND address behavior.
- `FRAG != 0`.
- Invalid and truncated datagrams.
- Spoofed UDP sources.
- TCP control close cleanup.
- Multiple concurrent associations.
- All 56 TCP regression tests.
- Deterministic pending-I/O cancellation and malformed-datagram lifecycle
  stress verification.

Independent audit result:

- One continuing `agy` session inspected the complete implementation and
  independently reran the M1/M2/TCP verification matrix.
- It found no blocker, high, or medium issue and returned `AUDIT_PASS`.
- Full evidence is recorded in `docs/m2-agy-audit.md`.

Verified integration markers:

```text
M2_G2_HANDSHAKE_OK
M2_G2_AUTHENTICATED_UDP_OK
M2_G3_BRANCHING_OK
M2_G4_RELAY_OK
M2_G4_G5_UDP_ASSOCIATION_OK
M2_G5_WRONG_SOURCE_IP_OK
M2_G5_SOURCE_AUTH_OK
M2_G5_ASSOCIATION_OK
M2_G5_CONCURRENCY_OK
M2_G5_LIFECYCLE_OK
M2_G3_NON_QUIC_REJECTION_OK
M2_G3_NO_BACKEND_REJECTION_OK
M2_SOCKS5_UDP_INGRESS_OK
```

## M3 execution ledger — native UDP client data path

Status: complete and independently audited. The real backend is installed in
production `naive`; M2 fake/no-backend behavior remains an independent
regression surface.

The plan was derived from direct inspection of the M1 tunnel and M2 ingress
boundaries, then checked by three independent read-only reviews. The reviews
identified these blockers, all of which M3 subsequently resolved:

- the zero-argument backend factory must receive the exact transient NAK from
  `PendingSocksHandshake`;
- one target needs one generation-safe tunnel owner and continuous read/write
  pumps;
- ordinary target, oversize, and queue failures must not close the whole SOCKS
  association;
- payload ceiling, zero-length datagram versus EOF, callback-stack retirement,
  and URL request context destruction order need explicit tests;
- target, packet, byte, active-association, connect, idle, and cooldown bounds
  must be frozen before full-path load testing;
- the full M3 path used `naive_masque_server` as a controlled compliant
  endpoint; production Caddy/`forwardproxy` was completed separately in M4.

Sequential gates:

```text
G0  backend context/NAK, contracts, constants, scripted tunnel seam
G1  cancellation-safe single-target backend
G2  target routing, bounds, cooldown, and failure isolation
G3  real M1 adapter and production composition
G4  controlled IPv4/IPv6/domain/DNS/auth/multi-target interoperability
G5  lifecycle, recovery, resource pressure, and observability
G6  full regression plus independent agy AUDIT_PASS
```

Final aggregate marker: `M3_NATIVE_UDP_CLIENT_OK`.

Planning estimate: 12–20 person-days for an audited M3, approximately 2–4
elapsed weeks for one engineer plus an agent. The first single-target real path
is not considered milestone completion.

### M3-G0 — backend contract, context, and test seam

Status: complete.

Completed:

- Replaced the zero-argument backend factory with an immutable per-association
  context carrying the association id, non-owning session pointer, exact
  transient NAK, selected proxy chain, NetLog source, traffic annotation,
  10-second connect timeout, and target idle timeout.
- Passed `PendingSocksHandshake::network_anonymization_key` directly into that
  context before the successful UDP ASSOCIATE reply. The M2 runner now rejects
  empty/non-transient keys and validates the remaining production-style
  context inputs.
- Froze target identity as SOCKS address type plus host plus port, so domains
  and numerically equivalent IP literals remain separate routes.
- Froze admission/no-replay comments and v1 limits: 32 targets, 16 queued
  datagrams per target, 128 queued datagrams and 256 KiB per association, 32
  synchronous pump operations, 10-second connect timeout, 1-second cooldown,
  and 256 active UDP associations per NaiveProxy.
- Added the narrow `NaiveConnectUdpTargetTunnel` seam for scripted and future
  production M1 adapters, including explicit open-state, zero-length-read, and
  safe-payload-limit queries.
- Added the standalone `naive_connect_udp_backend_test` and the cumulative
  `tests/socks5_udp_m3.sh` entry point.

Verified markers:

```text
M3_G0_BACKEND_CONTRACT_OK
M3_G0_TEST_SKELETON_OK
```

### M3-G1 — cancellation-safe single-target backend

Status: complete.

Completed:

- Lazily creates one fixed-target tunnel on the first admitted datagram and
  queues payloads while CONNECT-UDP is pending.
- Serializes writes, retains every `IOBuffer` through pending callbacks, checks
  exact byte counts, and clears ambiguous queued data without replay after a
  short write or transport failure.
- Keeps one read armed on an open tunnel, immediately rearms after delivery,
  and yields after 32 synchronous completions.
- Uses the live tunnel payload ceiling before every write; oversize payloads
  are observable policy drops rather than association-fatal errors.
- Distinguishes an empty UDP datagram from EOF through the scripted tunnel
  contract, preserves the original SOCKS endpoint on responses, and supports
  callback-triggered backend destruction without rearming or UAF.
- Defers target destruction out of connect/read/write callback stacks and
  orders tunnel destruction before retained pending-I/O buffers.

Deterministic coverage includes synchronous and asynchronous connect/read/
write, same-target reuse, byte equality, pending destruction, short write,
EOF, synchronous and asynchronous zero-length datagrams, live oversize drop,
and receive-callback destruction.

Verified marker:

```text
M3_G1_SINGLE_TARGET_OK
```

### M3-G2 — target routing, limits, and failure isolation

Status: complete.

Completed:

- Routes by address type plus host plus port with one generation-tagged tunnel
  owner per target. Interleaved responses retain their original endpoint and
  cannot cross routes.
- Enforces 32 live/cooldown targets, 16 queued datagrams per target, 128 queued
  datagrams and 256 KiB per association. A busy target is never evicted to
  admit a new one.
- Enforces a default 256 active UDP-association cap per `NaiveProxy`, counting
  both established associations and successful-reply-pending reservations.
  The listener's existing `concurrency` value governs session prewarming, not
  connection count, so a separate conservative hard cap is required. A
  deterministic cap/release test uses an injected lower limit and verifies
  exact SOCKS reply `0x01` at capacity.
- Adds connect deadlines, independent target idle eviction, and cooldown
  tombstones. Cooldown blocks per-packet reconnect storms; a later new packet
  after expiry creates a fresh tunnel.
- Converts connect/read/write/session failure into target-scoped retirement.
  Other targets and the SOCKS association remain active. Queued data from an
  ambiguous failure is cleared and never replayed into the fresh tunnel.
- Resets live idle deadlines on admitted open-target traffic and successful
  reads/writes, caps synchronous read work at 32 operations, and resumes via a
  posted task.

Deterministic tests cover IPv4/domain-distinct routing primitives, interleaved
targets, same-target reuse, target/packet/byte/association caps, connect
timeout, idle eviction, cooldown suppression and expiry, fresh-tunnel creation,
no replay, failure isolation, and synchronous pump yield.

Verified markers:

```text
M3_G2_MULTI_TARGET_LIMITS_OK
M3_G2_FAILURE_ISOLATION_OK
M3_G2_ACTIVE_ASSOCIATION_LIMIT_OK
```

### M3-G3 — real M1 adapter and production composition

Status: complete.

Completed:

- Added a production target adapter that owns `NaiveConnectUdpTunnel` and
  forwards the backend context's session, complete proxy chain, exact NAK,
  NetLog source, traffic annotation, and fixed target without substituting a
  second QUIC implementation.
- Added defensive production-factory checks for a live session, non-empty
  transient NAK, positive timeouts, a valid non-direct chain, and every proxy
  hop being `quic://`. The SOCKS handshake keeps its independent eligibility
  check.
- Installed the real factory in `naive_proxy_bin.cc`. The production binary
  still uses the default certificate verifier; only the separate controlled
  runner installs `MockCertVerifier` before building its context.
- Added live Chromium queries for stream state, empty-datagram evidence, and
  safe payload ceiling. The ceiling uses QUICHE's HTTP/3 datagram size after
  quarter-stream-id overhead, then removes the RFC 9298 Context ID byte.
- Treats a closed live stream or zero live ceiling as a target failure rather
  than misclassifying it as an oversize policy drop.
- Corrected declaration order so every proxy/backend/tunnel is destroyed
  before its URL request context/session and resolver.
- Added `naive_socks5_udp_m3_runner`, which shares the exact production
  factory and proves graceful proxy-before-context destruction.
- Verified exact SOCKS `0x01` plus EOF for direct, HTTPS/H2, valid mixed, and
  no-backend configurations. Verified real IPv4 echo and cached Basic auth via
  SOCKS5, the M3 backend, M1 tunnel, RFC 9298 CONNECT-UDP, and H3 DATAGRAM.

Verified markers:

```text
M3_G3_DIRECT_REJECTION_OK
M3_G3_H2_REJECTION_OK
M3_G3_MIXED_CHAIN_REJECTION_OK
M3_G3_NO_BACKEND_REJECTION_OK
M3_G3_IPV4_ECHO_OK
M3_G3_AUTH_ECHO_OK
M3_G3_PRODUCTION_WIRING_OK
```

The G3 gate also reran all M1 scripts, the complete M2 suite, the cumulative
M3 entry point, all 56 existing TCP cases, and `git diff --check`.

### M3-G4 — controlled full-path interoperability

Status: complete.

Completed:

- Ran the exact production backend/factory through a real SOCKS5 UDP relay,
  Chromium CONNECT-UDP/H3 DATAGRAM, the controlled QUICHE endpoint, and local
  deterministic UDP fixtures.
- Verified IPv4 and IPv6 literals, a domain target without local resolution,
  a deterministic DNS query/response, multiple targets in one association,
  and four concurrent associations.
- Verified the cached Basic credential path separately from the transient NAK
  contract and required the controlled server's accepted-auth evidence.
- Replaced fixed fixture ports with dynamically reserved loopback ports and
  widened runner deadlines to remove slow-host and parallel-test flakiness.
- Closed the post-G3 privacy audit finding: QPDCS now records write byte counts
  without payload buffers, CONNECT-UDP request lines are redacted, and the
  common HTTP/3 header logger redacts the RFC 9298 target path even at NetLog
  `kEverything` capture level.
- Closed the corresponding transport-lifecycle code gap: underlying QUIC
  stream closure now completes a pending QPDCS datagram read and permits the
  M3 owner to retire that target promptly. G5 adds explicit reconnect evidence.

Verified markers:

```text
M3_G4_IPV4_OK
M3_G4_IPV6_OK
M3_G4_DOMAIN_OK
M3_G4_DNS_OK
M3_G4_AUTH_OK
M3_G4_MULTI_TARGET_OK
M3_G4_CONCURRENT_ASSOCIATIONS_OK
M3_G4_NETLOG_REDACTION_OK
```

### M3-G5 — lifecycle, recovery, limits, and observability

Status: complete.

Completed:

- Added a real full-path session-shutdown case. Chromium closes the active
  QUIC session while QPDCS has a pending read; the close callback retires only
  that target, the one-second cooldown expires, and a later packet creates a
  second CONNECT-UDP tunnel. The UDP fixture receives the pre- and post-close
  payload exactly once each.
- Added a real target-idle case using a test-runner-only timeout override. The
  first target is evicted independently of the SOCKS association and a later
  packet creates a fresh tunnel.
- Verified absent and wrong cached Basic credentials each produce two explicit
  server-side 407 rejections while the SOCKS association remains open. The
  accepted cached credential case remains the independent G4 positive path.
- Verified control close with CONNECT pending, active backend destruction with
  a pending target read, and deterministic backend destruction with pending
  connect/read/write callbacks. Production QPDCS writes are currently
  synchronous, so the pending target-write branch is exercised through the
  narrow scripted tunnel contract rather than claimed as a real network state.
- Verified a 200 ms controlled connect timeout, cooldown, retry on a later
  packet, idle eviction, zero-length datagram echo, and four oversized drops
  followed by a healthy small datagram on the same target.
- Revalidated the 32-target, 16-packet-per-target, 128-packet, 256 KiB,
  256-active-association, 32-operation pump, and 64-response-queue limits. The
  response-pressure test proves exactly 64 queued responses are sent and two
  excess responses are dropped without terminating the association.
- Added `NAIVE_CONNECT_UDP_BACKEND_COUNTER`. It logs only association id,
  non-sensitive reason, and cumulative count at powers of two. A real
  `kEverything` NetLog proves oversize events occur at counts 1, 2, and 4 and
  contain neither UDP destination nor payload.
- Kept all timeout shortening and self-signed certificate handling inside the
  controlled test runner; production defaults and verification are unchanged.

Verified markers:

```text
M3_G5_DETERMINISTIC_LIFECYCLE_OK
M3_G5_SESSION_RECONNECT_OK
M3_G5_IDLE_RECONNECT_OK
M3_G5_ZERO_OVERSIZE_OK
M3_G5_BACKEND_DESTRUCTION_OK
M3_G5_PENDING_CONNECT_CLOSE_OK
M3_G5_CONNECT_TIMEOUT_OK
M3_G5_AUTH_MISSING_OK
M3_G5_AUTH_WRONG_OK
M3_G5_AUTH_FAILURES_OK
M3_G5_RECONNECT_OK
M3_G5_LIFECYCLE_OK
M3_G5_LIMITS_OK
```

### M3-G6 — complete regression, stress, and independent audit

Status: complete.

Verified:

- Rebuilt the complete named Release target set and reran all M1 scripts, the
  full M2 entry point, the full M3 entry point, all 56 TCP HTTP/HTTPS/auth/chain
  cases, and `git diff --check`.
- Repeated `tests/socks5_udp_m3.sh` three consecutive times against fresh
  controlled endpoints. Every run produced `M3_G5_RECONNECT_OK`,
  `M3_G5_LIFECYCLE_OK`, `M3_G5_LIMITS_OK`, and
  `M3_G0_TEST_SKELETON_OK` with exit code zero.
- Inspected `c6ec957f..578e3992`: no `NaiveConnection` TCP data-path change,
  second QUIC stack, UoT/private framing, Caddy/M4 implementation, production
  certificate bypass, generated artifact, or accidental tracked test output
  entered M3.
- Started a fresh Gemini 3.1 Pro High `agy -p` review with permissions prompts
  disabled and a 30-minute window. The reviewer read the actual diff rather
  than relying on this ledger, independently reran the complete required
  matrix, and returned `AUDIT_PASS` with zero blocker, high, or medium
  findings.
- The sole low observation was that the frozen 32-target v1 cap may be small
  for aggressive multi-target clients; the reviewer confirmed bounded graceful
  drops make it acceptable for v1.

Final marker: `M3_NATIVE_UDP_CLIENT_OK`.

Durable report: [`m3-agy-audit.md`](m3-agy-audit.md).

## M4 production server path

Status: complete and independently audited. Detailed gates and contracts are in
[`m4-execution-plan.md`](m4-execution-plan.md).

Read-only source inspection recorded these exact reference snapshots:

- Naive `forwardproxy` branch `naive`, commit
  `d62c80d3dd2c706b6b87579844d2397bddd18317`;
- Caddy `v2.11.2`, commit
  `ffb6ab0644f24c5ee6542aca6bd59b7a1b0a8f91`;
- quic-go `v0.59.0`, commit
  `7659dd8e0fa06b41290ad29af323d93d673c6b36`.

The historical source facts that defined the runtime M4 gates were:

- baseline `forwardproxy` rejected H2/H3 CONNECT whenever `:scheme` or `:path`
  was present, so RFC 9298 needed an explicit Extended CONNECT branch before
  legacy TCP CONNECT;
- authentication, probe resistance, and normal-site routing already executed
  before CONNECT dispatch and must remain common to TCP and CONNECT-UDP;
- the baseline authorization/dial helper accepted only TCP, so target
  authorization/resolution had to be factored from transport dialing rather
  than bypassed for UDP;
- the declared `forwardproxy` dependencies were Caddy v2.8.4/quic-go v0.44.0,
  but its release workflow used floating `xcaddy@latest`, while the inspected
  Caddy v2.11.2 used quic-go v0.59.0 and Go 1.25; G0 had to freeze one exact
  reproducible tuple;
- Caddy v2.11.2 constructed `http3.Server` without `EnableDatagrams: true`;
- quic-go v0.59.0 exposed `http3.HTTPStreamer` and stream-level
  `SendDatagram`/`ReceiveDatagram`, but real Caddy middleware visibility and
  writer unwrapping still had to be runtime-proven in M4-G1;
- quic-go owns HTTP/3 quarter-stream-id framing, while the server handler must
  decode/prepend RFC 9298 Context ID `0` around the UDP payload.

The completed sequence was G0 build/contract freeze, G1 Caddy capability spike, G2 strict
protocol/policy layer, G3 bounded UDP association, G4 production integration,
G5 independent server interoperability, and G6 reproducible closeout plus
independent `agy` audit. All gates are verified and the final milestone marker
is recorded below.

### M4-G0 — reproducible server baseline: complete

- Production server fork: `https://github.com/ssharkkky/forwardproxy`, branch
  `codex/native-udp-server`; local gate commit `bf092e6`.
- Caddy patch fork: `https://github.com/ssharkkky/caddy`, branch
  `codex/enable-h3-datagrams`, still at the clean v2.11.2 base for G0.
- Locked Go `1.25.12` archive SHA-256
  `fa2c88bbcf64bd3b2aef355f026cfec6d3a4a01c132f999c8f8c964eb767164f`,
  xcaddy `v0.4.5`, Caddy `v2.11.2`, quic-go `v0.59.0`, and the exact base
  commits recorded above.
- Replaced the floating `xcaddy@latest` workflow and old Caddy v2.8.4 module
  graph with the pinned tuple.
- Modernized the pre-existing test topology to distinct `*.localhost` names;
  this preserves Host/SNI separation while avoiding non-portable macOS
  `127.x.y.z` loopback aliases. Existing TCP, auth, ACL, upstream, PAC, and
  probe-resistance tests all pass on Caddy v2.11.2.
- Froze the v1 protocol/result/resource baseline and explicit no-private-queue,
  no-replay rule. The standalone script emits `M4_G0_SERVER_BASELINE_OK`.
- Two clean pinned builds were byte-identical with SHA-256
  `5b2d40b134e9b340e8fa9a9384c44d2b871bb43915fd485734be9003016b611d`.

G0 did not claim a Caddy H3 Datagram patch or production CONNECT-UDP handler.
M4-G1 subsequently proved that runtime boundary before protocol/relay work.

### M4-G1 — real Caddy H3 Datagram capability: complete

- Caddy commit `2002a520` enables `http3.Server.EnableDatagrams`; the first
  real handshake correctly failed with H3 SETTINGS error because Caddy's
  shared QUIC listener had already been created without the matching QUIC
  transport parameter.
- Caddy commit `2ff83e69` also enables Datagrams on that shared QUIC listener.
  This closes both required RFC 9297 negotiation layers rather than faking a
  server SETTINGS value.
- Forwardproxy commit `121f097` pins the patched Caddy pseudo-version/build
  replacement and adds a G1-only capability fixture plus the production-safe
  response-writer unwrapping seam.
- A real `quic-go` client traversed the complete Caddy route/middleware chain,
  observed `EnableExtendedConnect` and `EnableDatagrams`, proved incoming
  `r.Proto == connect-udp`, unwrapped through standard `Unwrap()` to
  `http3.HTTPStreamer`, and round-tripped both a binary payload and a valid
  zero-length H3 Datagram.
- Marker `M4_G1_CADDY_H3_DATAGRAM_OK`, the complete legacy forwardproxy suite,
  focused Caddy package tests, and the patched production Caddy build passed.
- Patched Caddy build SHA-256 at this gate:
  `9b8f5b62c80313264fb028e4b8f05fcb1a8c2434c60f1957089af0d0d6845269`.

G1 contains no target parser, ACL bypass, UDP socket, or production relay.
Those start only after G2 freezes strict protocol and policy behavior.

### M4-G2 — strict RFC 9298 protocol and policy: complete

- Forwardproxy commit `f9b40f6` adds an explicit H3 `connect-udp` branch
  before legacy TCP CONNECT and rejects other Extended CONNECT protocols with
  the frozen unsupported status.
- The strict default URI-template parser accepts IPv4, percent-encoded IPv6,
  ASCII/IDNA domains, and ports 1–65535. It rejects queries, fragments,
  missing/extra segments, encoded slash/backslash, double encoding, zones,
  userinfo, bracketed variables, bad labels, and invalid ports.
- The RFC 9298 application codec accepts only canonical QUIC-varint Context ID
  `0`, preserves a valid empty payload, and rejects truncated, noncanonical,
  or unsupported contexts.
- Target resolution/authorization is factored from TCP dialing. TCP and future
  UDP use the same domain rules, per-resolved-IP ACL evaluation, allowed-port
  list, deduplication, and context-aware DNS lookup; legacy TCP status/error
  mapping and the complete old suite remain green.
- UDP-facing policy errors are generic and do not contain the target. The
  frozen `400/403/501/502` protocol/policy mapping and unsupported upstream
  mode have deterministic tests.
- Marker `M4_G2_PROTOCOL_POLICY_OK` and the cumulative G0–G2 plus full legacy
  server suite pass.

G2 deliberately returned `501` after a valid authorized request reached the
association boundary. M4-G3 replaced that final stub with the bounded UDP
association; no packet was silently accepted before the data path existed.

### M4-G3 — bounded production UDP association: complete

- Forwardproxy commit `1b6d04b` replaces the valid-request `501` stub with one
  connected UDP socket per fixed-target CONNECT-UDP stream. Resolution and
  policy approval select a concrete IP before the HTTP `200`; the target is
  not re-resolved after authorization.
- The two pumps decode/prepend canonical Context ID `0`, preserve valid empty
  UDP payloads, retain one in-flight datagram per direction with no
  forwardproxy-owned packet queue, yield every 32 datagrams, and never retry
  an ambiguous write.
- A live quic-go `DatagramTooLargeError` is an observable drop rather than a
  stream reset. Other UDP/H3 failures close only that association.
- Per-handler 256 and per-client 32 active-association caps return the frozen
  pre-success `503`. Double-safe release, request/stream cancellation,
  two-minute production idle expiry, connected-socket closure, and pump join
  prevent resource leaks.
- Deterministic tests verify bidirectional byte equality, malformed-context
  drop, no replay, idle shutdown, cap/release behavior, and cancellation. Race
  coverage passed for association, admission, and real production-path tests.
- A real independent H3 client traversed the actual forwardproxy Handler to a
  local IPv4 UDP echo target and back for both non-empty and zero-length
  payloads. Marker: `M4_G3_UDP_ASSOCIATION_OK`.
- The complete legacy forwardproxy TCP/auth/ACL/upstream/PAC/probe-resistance
  suite remains green.

### M4-G4 — production auth/policy/privacy integration: complete

- Forwardproxy commit `15c07ab` preserves the legacy TCP branch and routes
  valid CONNECT-UDP only after shared authentication and probe-resistance
  processing. It also corrects the H3-specific authority interaction so a
  missing or wrong credential returns `407` when probe resistance is disabled,
  while enabled probe resistance still matches the ordinary hidden-site path.
- A real H3 matrix verifies IPv4, IPv6, domain resolution, missing/wrong/correct
  Basic credentials, malformed `400`, ACL/allowed-port `403`, unsupported
  upstream `501`, and probe-resistance passthrough against a reference Caddy
  route.
- CONNECT-UDP keeps its original URI available to the downstream camouflage
  route but redacts it before returning to outer Caddy logging. Handler counters
  log only association id, generic reason, count, and byte count at powers of
  two; tests search for target, path, payload, and credential leakage.
- `scripts/test-m4.sh`, the complete legacy forwardproxy suite, and
  `go test -race ./...` pass. The race build emitted only the previously noted
  harmless macOS `LC_DYSYMTAB` linker warning.
- Marker: `M4_G4_FORWARDPROXY_INTEGRATION_OK`.

### M4-G5 — pinned production-server interoperability: complete

- Forwardproxy commit `7243519` adds an independent quic-go RFC 9298 command
  that does not import NaiveProxy or forwardproxy test helpers, plus a real
  Caddy binary orchestration script and production-style Caddyfile.
- The locked build script now puts the verified Go 1.25.12 binary first in the
  `xcaddy` child PATH and inspects the final binary's embedded Go version. This
  closed a discovered gap where the earlier script verified 1.25.12 but could
  let `xcaddy` select a different system Go.
- Caddy commit `cce894a8` replaces its debug-level reflected raw module config
  with non-sensitive topology counts. The prior raw reflection exposed the
  recoverable double-Base64 form of Basic credentials; the final standalone
  log scan rejects both encoded forms as well as the target path/domain and
  payload sentinels.
- The standalone matrix verifies IPv4, IPv6, domain, deterministic DNS,
  zero-length, a 1024-byte safe payload, local oversize rejection followed by
  a healthy datagram, eight simultaneous streams, cancellation recovery, and
  negotiated Extended CONNECT plus H3 Datagram APIs.
- Thirty-two live associations are admitted from one client; the 33rd receives
  `503`, and released capacity is reusable. QUIC keepalive isolates and proves
  the unchanged two-minute production association idle expiry, after which a
  fresh stream on the same connection succeeds.
- A finite one-second Caddy grace period proves active H3 association
  cancellation during process shutdown, followed by restart smoke success.
  Three further complete matrix runs, the full legacy suite, race tests, and
  Caddy HTTP package tests remain green.
- Markers include `M4_G5_H3_DATAGRAM_EVIDENCE_OK`,
  `M4_G5_IDLE_EXPIRY_OK`, `M4_G5_RESOURCE_LIMIT_OK`,
  `M4_G5_SHUTDOWN_RESTART_OK`, `M4_G5_SERVER_LOG_PRIVACY_OK`, and final
  `M4_G5_SERVER_INTEROP_OK`.

### M4-G6 — release closeout and independent audit: complete

- Two separate locked builds are byte-identical. Both have SHA-256
  `d31fa3c8b0897b12ee799305a5aba10e23434fa0153398e44d82bb8ba4d82ba4`,
  and embedded build metadata reports Go 1.25.12.
- The standalone `M4_G5_SERVER_INTEROP_OK` matrix passed again using one of
  those exact G6 binaries.
- Uncached `go test -count=1 ./...` and `go test -race -count=1 ./...` pass in
  forwardproxy. The race link emits only the previously recorded macOS
  `LC_DYSYMTAB` warning. Uncached `go test -count=1 ./...` passes across the
  complete patched Caddy repository.
- The full M1 script set, M2, M3, aggregate `M3_NATIVE_UDP_CLIENT_OK`, and all
  56 TCP cases pass from the unchanged client checkout.
- Diff and artifact inventory confirms that M4 changed only documentation in
  this repository, 22 intended files in forwardproxy, and three intended Caddy
  files. No M4-generated binary, log, capture, certificate, credential, or
  destination-bearing output is tracked. Caddy's pre-existing tracked test
  key/certificate fixtures are outside the M4 diff.
- A separate user-run Antigravity (`agy`) session then inspected the committed
  NaiveProxy documentation range `fa7a1c2dfa..9ec8fff82c`, forwardproxy range
  `d62c80d..7243519`, and Caddy range `ffb6ab06..cce894a8`. It returned
  `AUDIT_PASS` with zero blocker, high, or medium findings.
- The audit's one low finding identified `.github/workflows/build.yml` still
  selecting Caddy `2ff83e69` instead of privacy-fixed `cce894a8`. The local
  build path and `go.mod` were already correct. Forwardproxy commit `8f044e2`
  closes the finding by updating that single CI ref; runtime source is
  unchanged.
- The durable audit record is [`m4-agy-audit.md`](m4-agy-audit.md).

Final marker: `M4_NATIVE_UDP_SERVER_OK`.

M4 is complete. M5 now owns the full SOCKS5-to-Naive-client-to-production-
Caddy product matrix, including product-level reconnect claims.

## M5 execution baseline — end-to-end MVP

Status: complete and independently audited. The
active plan is
[`m5-execution-plan.md`](m5-execution-plan.md).

M5 inherits these frozen inputs:

- NaiveProxy's audited M3 production data path, closeout `2bb83aec`;
- forwardproxy `8f044e2`, whose runtime implementation was audited at
  `7243519` and whose final commit closes the audit's CI-only pin finding;
- Caddy `cce894a8` and the locked M4 Go/xcaddy/quic-go build tuple.

The M5 sequence is:

```text
G0  dynamic topology, reversible trust fixture, independent H3 probe contract
G1  first M3-client-to-production-M4-server IPv4 echo
G2  IPv4/IPv6/domain/DNS/multi-target/concurrency/HTTP3 application matrix
G3  authentication, policy, malformed input, failure isolation, privacy
G4  control close, idle, server restart, QUIC reconnect, no replay
G5  shipped naive + default certificate verifier, wire evidence, no-padding baseline
G6  full M1-M5/server regressions, artifacts, independent AUDIT_PASS
```

Planning inspection found two tasks not fully represented in the earlier
5-8-day estimate: a safe trusted-certificate fixture for the shipped `naive`
binary and an independent SOCKS5-UDP-backed HTTP/3 application client. The M5
planning range is therefore 6–10 person-days.

### M5-G0 — topology, trust, harness, and evidence contract: complete

- Added `tests/socks5_udp_m5.sh` as the cumulative M5 entry point. It verifies
  the exact M3/M4/Caddy revisions, unchanged client runtime source, clean
  server worktrees, binary metadata, and isolated temporary Go caches.
- Added a dynamic loopback topology allocator and deterministic tests. The
  proxy uses one dynamically selected port available to both TCP and UDP;
  echo, DNS, and HTTP/3 fixtures use distinct dynamic UDP ports.
- Froze the macOS production trust strategy around Chromium's user-domain
  `TrustStoreMac`. The default contract check is read-only. A controlled
  `--exercise` run returned `before=1 during=0 after=1`, proving a temporary
  trust root is removed completely; the later G5 production run must use that
  path rather than a certificate-bypass flag.
- Added a dedicated Go 1.25/quic-go 0.59 module for the independent HTTP/3
  probe. G0 freezes the SOCKS5 UDP `net.PacketConn`, retained TCP control
  lifetime, and IPv4/IPv6/domain target identity; G2 adds runtime transport.
- Froze revisions, marker names, privacy sentinels, capture exclusions, and
  size/timing-only traffic fields in `tests/m5/contract.json`.
- `M5_G0_PRODUCT_CONTRACT_OK`, all M1 scripts, M2/M3 aggregates,
  `M3_NATIVE_UDP_CLIENT_OK`, all 56 TCP cases, `M4_G5_SERVER_INTEROP_OK`,
  forwardproxy normal/race tests, and focused Caddy HTTP tests passed.

No cross-repository product echo is claimed in G0. The deterministic M3 runner
may be used for the broad matrix because it shares the production
backend/factory, but final M5 evidence also requires `out/Release/naive` with
`CertVerifier::CreateDefault()`. A production certificate-bypass switch is
forbidden.

### M5-G1 — first audited-client-to-production-server echo: complete

- Added a dynamic production Caddyfile in forwardproxy commit `d922441`; this
  changes no audited runtime source and retains forwardproxy `8f044e2` as the
  M4 runtime base and Caddy `cce894a8` as the exact server dependency.
- Added `tests/m5/g1_cross_repo_echo.sh`. It owns a fresh temporary root,
  starts the pinned Caddy binary with Basic authentication and H3 Datagrams,
  starts the M3 runner with the real production factory, negotiates SOCKS5
  UDP, and sends one IPv4 datagram to a dynamic loopback echo target.
- The response was byte-identical. Runner evidence proved production-factory
  eligibility and destruction order; NetLog proved a redacted
  `QUIC_PROXY_DATAGRAM_CLIENT_SOCKET` send and redacted CONNECT-UDP request;
  server access/lifecycle logs proved an authenticated target-redacted `200`.
- Privacy scans found no payload, target path, plain credential, password, or
  Base64 credential in client/server evidence. Cleanup left no G1 process or
  temporary root.
- The focused G1 path passed once, then passed three consecutive fresh-root
  repetitions. The cumulative `tests/socks5_udp_m5.sh` now emits both the G0
  contract and `M5_G1_CROSS_REPO_ECHO_OK`.

### M5-G2 — addressing, DNS, multiplexing, and HTTP/3 application: complete

- Added an independent RFC 1928 `net.PacketConn` in the M5 Go module. It
  performs no-auth or RFC 1929 negotiation, sends UDP ASSOCIATE, consumes the
  real BND relay endpoint, retains the control channel, filters unexpected
  relay sources, and preserves IPv4/IPv6/domain identity in both directions.
- Added codec/malformed and live fake-SOCKS tests for empty/binary payloads,
  no-auth, username/password, target identity, and control-owned cleanup.
- Added a dual-stack controlled quic-go HTTP/3 origin and runtime mode to the
  independent probe. The probe uses the SOCKS PacketConn as quic-go's packet
  transport, sends a real HTTP/3 GET through a domain-form target, verifies
  the exact H3 response, and closes the inner QUIC connection. Its inner
  self-signed certificate is verified through an explicit temporary root pool;
  Naive's outer proxy certificate behavior remains unchanged.
- The product UDP matrix verifies IPv4, IPv6, domain response framing,
  deterministic DNS, binary and zero-length datagrams, a 1200-byte safe
  payload, four 4096-byte oversize drops followed by healthy traffic, three
  interleaved targets, and four isolated concurrent SOCKS associations.
- All required markers passed in three fresh-root G2 runs. The cumulative M5
  entry point is green, `go test`, `go test -race`, `go vet`, formatting,
  cleanup, privacy scans, and all three repository `diff --check` boundaries
  pass.

Verified markers:

```text
M5_G2_IPV4_IPV6_DOMAIN_OK
M5_G2_DNS_OK
M5_G2_ZERO_OVERSIZE_OK
M5_G2_MULTI_TARGET_OK
M5_G2_CONCURRENT_ASSOCIATIONS_OK
M5_G2_HTTP3_APPLICATION_OK
M5_G2_PRODUCT_MATRIX_OK
```

### M5-G3 — authentication, policy, malformed input, and isolation: complete

- forwardproxy test-only policy fixtures end at `88ac298`; audited runtime
  source remains based at `8f044e2` and Caddy remains `cce894a8`;
- Correct, missing, and wrong upstream Basic credentials were exercised
  through production Caddy. Local SOCKS success, missing method, and wrong
  password were verified independently.
- Product policy variants returned `403` for port/ACL denial, `502` for DNS
  failure, `501` for unsupported upstream mode, and `503` for the 33rd active
  association. Permitted traffic and a replacement association remained
  healthy.
- Malformed/truncated input, nonzero `FRAG`, invalid address type, bad RSV,
  spoofed source port/IP, and a malformed burst were dropped; a later valid
  datagram on the same association passed.
- Direct, HTTPS/H2, mixed, and unavailable native-UDP backends returned SOCKS
  `0x01`. Default NetLog and server logs contained no target, MASQUE path,
  payload, password, Basic encoding, or double encoding.
- All four required G3 markers passed in focused and cumulative runs. M1-M3,
  all 56 TCP cases, forwardproxy normal/race, Caddy HTTP, and every repository
  `diff --check` remain green; the production M4 idle/restart/privacy suite
  passed immediately before this test-only gate.

### M5-G4 — lifecycle, restart, reconnect, idle, and no replay: complete

- `tests/m5/g4_lifecycle_matrix.sh` verifies idle/open/pending SOCKS control
  closure, two production-Caddy restart cycles, a forced outer QUIC session
  close with two independent targets, and the real 30-second production client
  target-idle boundary;
- unique payload counts prove pre-failure delivery exactly once, deliberately
  ambiguous datagrams zero times, and recovery only from a later fresh
  datagram. A healthy unrelated target remains isolated across session close;
- the real two-minute production server idle is composed into G5's one
  privileged trust window. The 125-second probe observed server
  `idle_expired`, created fresh state, and emitted
  `M5_G4_SERVER_IDLE_RECONNECT_OK`;
- the focused non-privileged matrix passed during development, passed again
  after removing its duplicate trust path, and leaves no process or temporary
  root;
- markers: `M5_G4_CONTROL_CLOSE_OK`, `M5_G4_SERVER_RESTART_OK` (twice),
  `M5_G4_QUIC_RECONNECT_OK`, `M5_G4_CLIENT_IDLE_RECONNECT_OK`,
  `M5_G4_IDLE_RECONNECT_OK`, and `M5_G4_NO_REPLAY_OK`.

### M5-G5 — shipped binary, trust, wire evidence, and baseline: complete

- The untrusted shipped-`naive` negative path failed before any CONNECT-UDP
  `200`. The trusted positive used a temporary user-domain root in the current
  user's login keychain and `CertVerifier::CreateDefault()`; cleanup removed
  its trust/certificate and verified the same server certificate untrusted
  again.
- The first positive attempt exposed that production `naive_proxy_bin.cc`
  configured forced QUIC origins after `URLRequestContextBuilder::Build()`.
  Chromium had already copied `QuicParams`, so a locally trusted root was
  accepted by the default verifier but rejected by the QUIC proof verifier.
  Commit `333b7cb253` moves only the existing QUIC configuration before Build.
- After the fix, shipped `out/Release/naive` passed authenticated IPv4 echo,
  deterministic DNS, the independent SOCKS5-UDP HTTP/3 application request,
  ordinary TCP SOCKS, and the 125-second production server-idle/reconnect case.
- Production NetLog contains the QUIC proxy datagram source and redacted
  CONNECT-UDP evidence; server logs contain redacted association lifecycle.
  Size/timing evidence contains only the four frozen encrypted-shape fields
  across echo, DNS, and HTTP/3 windows. Native UDP v1 adds no padding layer.
- The complete M1-M3 target/script matrix, `M3_NATIVE_UDP_CLIENT_OK`, all 56
  TCP cases, and `git diff --check` pass after `333b7cb253`.
- markers: `M5_G5_UNTRUSTED_CERT_REJECTED_OK`,
  `M5_G5_DEFAULT_CERT_VERIFIER_OK`, `M5_G5_PRODUCTION_BINARY_OK`,
  `M5_G5_H3_DATAGRAM_EVIDENCE_OK`, and
  `M5_G5_NO_PADDING_BASELINE_OK`.

### M5-G6 — regressions, artifact closeout, and independent audit: complete

- Rebuilt every named M1-M3 Release target and reran all M1 scripts, M2, M3,
  `M3_NATIVE_UDP_CLIENT_OK`, and all 56 TCP cases.
- Rebuilt Caddy from the pinned Go 1.25.12/xcaddy 0.4.5, Caddy `cce894a8`,
  forwardproxy `8f044e2` runtime plus test-only M5 fixtures, and quic-go 0.59.0
  inputs. That binary passed cumulative M5 G0-G5.
- G1-G4 then passed three consecutive additional fresh-root repetitions.
  Each produced `M5_G6_FRESH_ROOT_OK`; every run included control close, two
  Caddy restarts, outer-QUIC reconnect, client idle, and no replay.
- Forwardproxy cumulative/legacy, standalone `M4_G5_SERVER_INTEROP_OK`,
  uncached normal/race, and focused Caddy HTTP tests pass. The only race build
  diagnostic is the already recorded harmless macOS `LC_DYSYMTAB` warning.
- The first all-in-one closeout invocation stopped after those completed
  client/product repetitions because the new runner launched the standalone
  server script outside its Go module. No product test failed. The cwd-only
  harness fix is `d1aee3663f`; the corrected server half was rebuilt and run
  independently to `M5_G6_SERVER_REGRESSIONS_OK`.
- Three-repository diff/status checks, M5 diff extension/path scan, exact
  process-name scan, and login-keychain certificate scan pass. Only unrelated
  `.DS_Store` and `src/tmp/` remain untracked.
- local marker: `M5_G6_LOCAL_REGRESSIONS_OK`.

- A continuing read-only Gemini 3.1 Pro High `agy -p` review inspected
  NaiveProxy `cd9a676df9..eaf172d971`, forwardproxy runtime `8f044e2` plus
  M5-only fixtures through `2b2a8ea`, and exact Caddy `cce894a8`.
- It confirmed the RFC 9298/H3 DATAGRAM-only path, unchanged TCP data path,
  pre-Build QUIC configuration, default verifier, trust cleanup, independent
  H3 application probe, lifecycle/no replay, privacy baseline, G6 evidence,
  and dependency pins.
- In the same review session it independently reran
  `tests/m5/g1_cross_repo_echo.sh`; the command exited `0` with all five G1
  byte/auth/H3/privacy/aggregate markers and left no review-created change.
- Findings: zero blocker, high, medium, or low. Verdict: `AUDIT_PASS` with
  `Zero blocker, high, or medium findings.`
- Durable report: [`m5-agy-audit.md`](m5-agy-audit.md).

Final marker: `M5_NATIVE_UDP_MVP_OK`. M5 is complete; M6 now owns network
impairment, PMTU, soak, fuzz/sanitizer expansion, multi-platform qualification,
and release readiness.

## M6 execution baseline — hardening and release candidate

Status: complete (all G0-G6 gates closed; release candidate qualified; independent audit `AUDIT_PASS`). The plan was
[`m6-execution-plan.md`](m6-execution-plan.md).

M6 inherits the independently audited M5 boundary and keeps production client,
server, and Caddy source frozen unless a later hardening gate exposes a
reproducible defect. Its sequence is:

```text
G0  release contract, audited inputs, environment and platform evidence states
G1  safe inner payload ceiling and PMTU behavior
G2  deterministic loss/reordering/delay/jitter/bandwidth profiles
G3  resource pressure, reclamation and long-running soak
G4  fuzz, sanitizer, race and randomized lifecycle hardening
G5  macOS/Linux/Windows/Android qualification
G6  full release checklist, clean builds and independent AUDIT_PASS
```

### M6-G0 — release contract and environment readiness: complete

- Commit `80d37395a6` adds the G0-G6 plan, machine-readable contract, five
  contract tests, and a read-only environment/baseline runner.
- The contract freezes exact M5/client/server/Caddy/toolchain inputs, the
  RFC 9298/H3 DATAGRAM-only protocol, no replay/no padding decisions,
  fail-closed blocker policy, duration tiers, and forbidden artifacts.
- Platform claims remain separate: macOS arm64 is verified as G0 environment-
  ready; Linux x64, Windows x64, and Android arm64 are explicitly `not run`.
  They remain required for G5 and are not implied by the current-host pass.
- G2's default impairment mechanism is a future non-privileged seeded
  user-space UDP shaper. Available `pf`/`dnctl` facilities are optional and no
  administrator permission or trust mutation was used by G0.
- The exact Chromium-matched Clang revision, Python 3, Ninja, Go 1.25.12,
  xcaddy 0.4.5, four existing Release binaries, all three Git boundaries, M5
  audit verdict, and source-drift constraints passed.
- An incremental build of `naive`, `naive_socks5_udp_test`,
  `naive_connect_udp_backend_test`, and `naive_socks5_udp_m3_runner` reported
  `ninja: no work to do.`
- No production source, sibling repository, certificate trust, or generated
  private artifact changed. Only the pre-existing excluded `.DS_Store` and
  `src/tmp/` entries remain untracked.

Verified commands:

```bash
cd /path/to/naiveproxy
python3 tests/m6/contract_test.py
./tests/m6/g0_contract.sh
ninja -C src/out/Release naive naive_socks5_udp_test \
  naive_connect_udp_backend_test naive_socks5_udp_m3_runner
git diff --check
```

Verified markers:

```text
M6_G0_RELEASE_CONTRACT_OK
M6_G0_PLATFORM_CONTRACT_OK
M6_G0_M5_BASELINE_OK
M6_G0_TOOLCHAIN_OK
M6_G0_CONTRACT_OK
```

Next: M6-G1 must measure the live payload ceiling and PMTU-change behavior
before choosing the release policy. The existing 1200-byte success and
4096-byte oversize probes are inherited evidence, not the final G1 ceiling.

### M6-G1 — inner payload ceiling and PMTU behavior: complete

Commit `1870779147` is the first narrow test-only step. It extends the existing
production-backend deterministic target and proves that the backend:

- admits a payload exactly at the tunnel's current ceiling without truncation;
- drops ceiling-plus-one before calling tunnel `Write()`;
- re-queries the live ceiling before a later write after it shrinks;
- resumes exact delivery after the ceiling is restored; and
- counts two admitted/sent and two oversize-dropped datagrams without making
  the association fatal.

Verified command and marker:

```bash
ninja -C src/out/Release naive_connect_udp_backend_test
src/out/Release/naive_connect_udp_backend_test
# M6_G1_LIVE_CEILING_UNIT_OK
./tests/socks5_udp_m3.sh
# M3_NATIVE_UDP_CLIENT_OK
```

This is G1a evidence only. It does not establish the release payload value or
claim a real PMTU change; G1b-G1d remain open.

Hardening observation: the first cumulative M3 invocation stopped in its
pre-existing idle-reconnect segment before the final marker. With no source or
configuration change, an immediate full rerun exited `0` through
`M3_NATIVE_UDP_CLIENT_OK`. The successful rerun is regression evidence, but the
single transient is retained as a G3/G4 flake/soak investigation input rather
than being silently counted as a repeated qualification pass.

#### M6-G1b1/G1c — live product ceiling and PMTU fixture: complete

Commit `9c72a7da08` adds a black-box SOCKS5 UDP payload probe, a test-only
production Caddy fixture with trust installation disabled, and a user-space
outer-QUIC UDP shaper. It changes no production client/server source and does
not modify system trust.

Verified behavior:

- Three independent production-backend/production-Caddy roots measured the
  same live inner ceiling for IPv4, IPv6, and domain targets: 1314 bytes.
- Each root repeated exact-ceiling delivery three times, rejected 1315 bytes
  three times, then delivered later healthy traffic on the same association.
- The PMTU fixture interprets IPv6 minimum PMTU 1280 correctly as an outer UDP
  payload ceiling of 1232 bytes after 40-byte IPv6 and 8-byte UDP headers.
- Under that ceiling, the 1200-byte candidate inner payload produced a
  1225-byte outer QUIC packet and passed. The 1314-byte inner payload produced
  a 1345-byte outer packet and was dropped by the shaper.
- Restoring the outer ceiling delivered a fresh 1314-byte datagram; the
  ambiguous dropped datagram was not replayed. A separate IPv6 target and
  SOCKS association stayed healthy during the lower-PMTU interval.
- Three fresh PMTU roots passed with size-only shaper evidence. Default NetLog
  and production server logs contained no target path or credential. An early
  development run using `--net-log-everything` correctly exposed configured
  credentials to its local diagnostic artifact and was rejected by the
  privacy gate; the final harness deliberately uses default NetLog and the
  temporary diagnostic root was deleted.

Verified commands:

```bash
./tests/m6/g1_live_ceiling.sh
M6_G1_PROBE_MODE=pmtu ./tests/m6/g1_live_ceiling.sh
python3 tests/m6/contract_test.py
git diff --check
```

Verified markers:

```text
M6_G1_LIVE_PRODUCT_CEILING_OK bytes=1314
M6_G1_LIVE_CEILING_PRIVACY_OK
M6_G1B_LIVE_CEILING_OK
M6_G1_PMTU_SAFE_PAYLOAD_OK bytes=1200
M6_G1_PMTU_ISOLATION_OK
M6_G1_PMTU_NO_REPLAY_OK
M6_G1C_PMTU_RECOVERY_OK
M6_G1C_PMTU_OK
```

G1b2 completed through shipped `src/out/Release/naive` with the default
verifier inside one explicitly authorized user-domain trust window. The initial
`./tests/m6/g1_finalize.sh` invocation emitted the untrusted negative marker,
measured 1314 bytes for IPv4/IPv6/domain, emitted the shipped/default-verifier
markers, and removed the temporary trust before a test-harness TCP cwd bug
stopped its later regression phase. No product or trust failure occurred.

The non-mutating negative-only path passed at `182267a1ca`: shipped `naive`
used its default verifier, the untrusted temporary root could not establish a
CONNECT-UDP association, and the run ended with
`M6_G1B2_UNTRUSTED_CERT_REJECTED_OK` and `M6_G1B2_NEGATIVE_ONLY_OK`. This does
not satisfy the positive ceiling or trust-cleanup half of G1b2.

The closeout runner was corrected to invoke the TCP matrix from `src/` with a
relative script path. With the shipped phase already completed and trust
cleanup verified, `M6_G1_SKIP_SHIPPED=1 ./tests/m6/g1_finalize.sh` ran three
complete repetitions of live ceiling, lowered/restored PMTU, the cumulative M3
client suite, all 56 TCP cases, uncached forwardproxy tests, and focused Caddy
HTTP tests. Each repetition emitted `M6_G1D_REGRESSION_RUN_OK`; the command
ended with `M6_G1_PAYLOAD_PMTU_OK`. The separate payload policy remains a
candidate until G5 platform records are resolved.

### M6-G2 — deterministic network impairment: complete

Commit `028d3984d4` extends the test-only UDP shaper with named, seeded loss,
reordering, delay/jitter, and bandwidth scheduling; adds aggregate-only shaper
logs; and composes the production M3 backend, pinned M4 Caddy/forwardproxy,
generic UDP echo, DNS, and the independent quic-go HTTP/3 application probe.
No production runtime source changed.

Frozen profiles:

| Profile | Seed | Contract |
| --- | ---: | --- |
| delay | 101 | 20ms delay, 5ms jitter |
| loss | 202 | 5% seeded outer-packet loss |
| reorder | 303 | 20% seeded reordering with 30ms extra delay |
| bandwidth | 404 | 256 Kbit/s per-direction serialization |
| combined | 505 | 2% loss, 10% reordering, 10ms delay/5ms jitter, 512 Kbit/s |

Each profile verifies:

- 20 unique application datagrams, zero duplicate/corrupt/cross-target
  responses, and a protocol-aware delivery floor for unreliable profiles;
- DNS success within a bounded retry budget;
- an independent HTTP/3 request/response over a separate CONNECT-UDP target;
- SOCKS control close stops the closed association under impairment;
- removing the profile restores fresh traffic on the original association;
- no ambiguous datagram replay and healthy H3 target isolation;
- profile-specific shaper actions and default-NetLog/server privacy.

The committed three-run matrix passed 15 independent product roots. Delay and
bandwidth delivered 20/20 application datagrams in every run. Loss delivered
17-18/20, reorder 15-17/20, and combined 17-19/20; all had zero duplicates,
all DNS/H3 operations completed, and all removal/recovery checks passed. The
variation is expected for unreliable DATAGRAM delivery because the seeded
shaper sees the real runtime's packet sequence; the deterministic contract is
the profile/seed and safety outcome, not an invented lossless UDP count.

Verified commands and final markers:

```bash
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tests/m6 \
  python3 tests/m6/test_udp_shaper.py
./tests/m6/g2_network_matrix.sh
# M6_G2_FRESH_ROOT_MATRIX_OK run=1
# M6_G2_FRESH_ROOT_MATRIX_OK run=2
# M6_G2_FRESH_ROOT_MATRIX_OK run=3
# M6_G2_NETWORK_IMPAIRMENT_OK
```

G2 artifacts are temporary and deleted on success/failure/signal. Committed
profile evidence is limited to profile, seed, direction, size, action, reason,
elapsed time, and aggregate counts; it excludes endpoints, paths, credentials,
and packet bytes.

### M6-G3 — resource pressure and soak: complete

Commit `325c77b95a` added the bounded association/churn/resource harness. The
smoke evidence already passed with 256 active client associations, rejection
of the 257th, reuse of 64 released slots, 101 waves, 1616 product datagrams,
and recovery of runner/Caddy file descriptors.

The first unshortened qualification body's pressure work on the pre-fix runtime
also passed: 5634
waves, 90144 product datagrams, the same 256/257th/64 admission-reuse boundary,
runner RSS 13376 -> 18496 KiB and FD 10 -> 17, and Caddy RSS 41968 -> 41152
KiB and FD 12 -> 13. The wrapper then failed without
`M6_G3_STRESS_SOAK_OK` because its long-running shell had started before the
G4 allowlist change and reached an inconsistent post-probe branch. That run is
not counted as a pass and was invalidated by the subsequent Caddy race fix.
Commit `8ab48dbee1` replaces the non-portable BSD `sed` allowlist expressions;
a one-second stress smoke completed through privacy and harness markers.

A later post-fix 3600-second body on Caddy `dd9a89c1` completed 5391 waves and
86256 product datagrams. Its runner RSS was 13376 -> 19776 KiB with a
18864-KiB sampled peak; runner FDs were 10 -> 17 with a 526-FD admission-test
peak. Caddy RSS was 42592 -> 42768 KiB with a 42480-KiB sampled peak, and
Caddy FDs were 12 -> 13. Admission, churn, resource recovery, privacy, and
harness markers passed, but the wrapper emitted only the explicit-duration
smoke marker rather than `M6_G3_STRESS_SOAK_OK`. It therefore remains useful
diagnostic evidence and is not counted as qualification. Commit `5257e2757f`
records the pre-run explicit-duration decision directly; its one-second
override regression passed.

The clean command below then completed on Caddy `dd9a89c1` and forwardproxy
lock `e9663e4`:

```bash
env -u M6_G3_DURATION_SECONDS M6_G3_TIER=qualification \
  ./tests/m6/g3_stress_soak.sh
# M6_G3_ASSOCIATION_CAP_REUSE_OK active=256 rejected=1 reused=64
# M6_G3_RESOURCE_PEAK process=runner rss_kib=19904 fd=526
# M6_G3_RESOURCE_PEAK process=caddy rss_kib=44240 fd=13
# M6_G3_RESOURCE_SAMPLE process=runner rss_before_kib=13392 rss_after_kib=18832 fd_before=10 fd_after=17
# M6_G3_RESOURCE_SAMPLE process=caddy rss_before_kib=42672 rss_after_kib=40144 fd_before=12 fd_after=13
# M6_G3_CHURN_OK waves=5787 datagrams=92592
# M6_G3_RESOURCE_RECOVERY_OK
# M6_G3_STRESS_HARNESS_OK
# M6_G3_TIER_OK tier=qualification duration_seconds=3600
# M6_G3_STRESS_SOAK_OK
```

The final root had no crash, hang, stale process, capacity leak, privacy leak,
or replay. Runner RSS remained bounded and runner FDs returned from the
526-FD admission-test peak to 17; Caddy RSS decreased across the soak and its
FD count ended at 13.

The G3 harness covers association admission, churn, post-close resource
recovery, and bounded RSS/FD deltas. M5-G4 remains the inherited evidence for
server restart, idle expiry, control close, outer-session shutdown, and no
replay; G2's loss profile remains the inherited outer-QUIC impairment evidence.

### M6-G4 — fuzz, sanitizer, race, and lifecycle hardening: complete post-fix

Commit `5893f97f6e` adds a deterministic client codec-fuzz executable, the
seeded asynchronous lifecycle schedule, a fail-closed ASan/UBSan configuration,
and the frozen-budget cross-repository runner. It also extends the machine
contract with the exact seeds, iteration counts, lifecycle count, and Go fuzz
duration. The runner does not accept a shortened Go fuzz duration as a final
pass; diagnostic short runs must be invoked outside the gate runner.

Verified command and evidence:

```bash
./tests/m6/g4_sanitizer_fuzz.sh
# M6_G4_CODEC_FUZZ_OK seed=20260720 iterations=1000000 valid=44027
# M6_G4_CODEC_FUZZ_OK seed=9298 iterations=1000000 valid=43374
# M6_G4_CODEC_FUZZ_OK seed=1928 iterations=1000000 valid=43459
# M6_G4_RELEASE_CODEC_FUZZ_OK
# M6_G4_SEEDED_LIFECYCLE_OK iterations=2000
# M6_G4_ASAN_UBSAN_OK
# M6_G4_GO_RACE_FUZZ_OK
# M6_G4_SANITIZER_FUZZ_OK
```

The first full attempt exposed a race in pinned Caddy's automatic
HTTPS/certmagic startup and stopped without a pass marker. Although a second
run passed, the finding was real: `caddytls.TLS.CaddyModule()` used a value
receiver and copied mutable app state while `Start()`/`keepStorageClean()` was
writing it. Caddy commit `dd9a89c11194dcb806d845233995ef040f096464` changes the
module registration and receiver to pointer semantics; forwardproxy build-lock
commit `e9663e4` pins it. Caddy module ordinary/race tests, forwardproxy local-
Caddy full race three times, M4 owner integration, G5 server smoke, and build
reproduction passed. The final G4 runner was rerun on this new runtime and
passed. The previous marker is retained as pre-fix evidence only; the post-fix
marker is the release evidence.

Post-fix command and final evidence:

```bash
./tests/m6/g4_sanitizer_fuzz.sh
# M6_G4_RELEASE_CODEC_FUZZ_OK
# M6_G4_SEEDED_LIFECYCLE_OK iterations=2000
# M6_G4_ASAN_UBSAN_OK
# M6_G4_GO_RACE_FUZZ_OK
# M6_G4_SANITIZER_FUZZ_OK
```

The post-fix run used Caddy `dd9a89c1` and forwardproxy lock `e9663e4`; runner
commit `c58cc49b19` creates a temporary Go modfile replacing Caddy with that
worktree, so the race/fuzz evidence cannot silently use the old module cache.
No race was reported. The macOS linker warning is unchanged and non-fatal.

### M6-G5a — platform evidence contract: complete

Commits `9869f1d6d1` and `09af3795c7` add the fail-closed platform record and
its contract runner, and split G5 into
separately attributable macOS arm64, Linux x64, Windows x64, Android arm64,
and final interoperability sub-gates. All four records remain `not run` until
their exact OS/architecture, three repository revisions, commands, and markers
are populated. Contract validation passes, but this is record readiness only;
it is not platform qualification evidence.

```bash
./tests/m6/g5_platform_contract.sh
# M6_G5_PLATFORM_STATE id=macos-arm64 state=not run
# M6_G5_PLATFORM_STATE id=linux-x64 state=not run
# M6_G5_PLATFORM_STATE id=windows-x64 state=not run
# M6_G5_PLATFORM_STATE id=android-arm64 state=not run
# M6_G5_PLATFORM_CONTRACT_OK
```

### M6-G5b — macOS arm64 qualification: complete

`tests/m6/g5_macos_qualification.sh` composes an exact-revision macOS arm64
Release build, the shipped-client product smoke, and focused G2-G4 gates. Its
positive path is fail-closed unless
`M6_G5_TEMPORARY_TRUST_AUTHORIZED=1` is set for that invocation. The M5
shipped-product fixture retains its historical default revisions while
accepting explicit M6 revisions from this wrapper.

The non-mutating negative preflight passed against NaiveProxy `faf6da23fc`,
forwardproxy `e9663e4`, and Caddy `dd9a89c1`:

```bash
M6_G5_NEGATIVE_ONLY=1 ./tests/m6/g5_macos_qualification.sh
# M6_G5B_MACOS_BUILD_OK
# M5_G5_UNTRUSTED_CERT_REJECTED_OK
# M5_G5_NEGATIVE_ONLY_OK
# M6_G5B_MACOS_NEGATIVE_ONLY_OK
```

This proved build readiness and default-verifier rejection without modifying
trust. The later explicitly authorized positive command used one temporary
user-domain trust window and removed it before the non-privileged focused
gates:

```bash
M6_G5_TEMPORARY_TRUST_AUTHORIZED=1 \
  ./tests/m6/g5_macos_qualification.sh
# M6_G5B_MACOS_BUILD_OK
# M6_G5B_MACOS_PRODUCT_OK
# M6_G2_NETWORK_IMPAIRMENT_OK
# M6_G3_STRESS_SMOKE_MATRIX_OK
# M6_G4_SANITIZER_FUZZ_OK
# M6_G5B_MACOS_ARM64_OK
```

The original run reported untrusted/trusted default-verifier behavior, UDP
echo, DNS, an independent HTTP/3 application, ordinary TCP SOCKS, the real
125-second server idle/reconnect boundary, H3 Datagram wire evidence, and the
no-padding baseline. All five focused impairment profiles passed; the 60-second
stress root completed 101 waves/1616 datagrams and reclaimed capacity; the
full frozen G4 codec/ASan/UBSan/race/Go-fuzz budget passed again. The temporary
CA was removed and later gates required no trust mutation. The TCP claim is
superseded by the corrected forced-SOCKS evidence below; the remaining markers
retain their historical value but do not close the current platform row.

The first machine record bound `verified` macOS arm64 evidence to macOS 26.5.2
build 25F84, NaiveProxy `350fc4e694c3dece134e5aa110ed24f307733e16`,
forwardproxy `e9663e4bd7222fd3ec3bd516c71e23fd5d482188`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. This did not qualify Linux,
Windows, or Android, and it no longer satisfies the corrected G5 contract.

The loopback TCP parity command used `curl --proxy` while `NO_PROXY` included
loopback, so the successful macOS response could be direct. Adding
`--noproxy ''` reproduced the real defect on macOS and Linux: a successful
CONNECT-UDP response cached proxy padding as `None`; the later fast-open TCP
CONNECT sent unpadded bytes while forwardproxy expected Variant1. The narrow
owner fix `baa7f2dd0845aa4cb55e39b4cc67c9b6a59b6285` advertises the negotiated
padding capability on CONNECT-UDP responses without padding UDP DATAGRAM
payloads. A controlled M3 production-path runner then completed a real SOCKS
TCP request and logged `negotiated padding type: Variant1`.

The corrected requalification completed on macOS 26.5.2 build 25F84, arm64,
at NaiveProxy `d402f9261c6ff3fe92bbd699e57051bccef2d61e`, forwardproxy
`f14924cdedc93c28a2b92c8120538ea5beee28fb`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. The negative-only preflight and
the explicitly authorized full run both passed. The full run verified shipped
UDP echo, DNS, independent HTTP/3 application, forced-SOCKS TCP, default
verifier rejection/acceptance/cleanup, idle/reconnect, H3 Datagram evidence,
all five impairment profiles, the 60-second 101-wave/1616-datagram stress
root, and the complete ASan/UBSan/race/fuzz budget. Final marker:

```text
M6_G5B_MACOS_ARM64_OK
```

The machine-readable platform record now marks `macos-arm64` verified; Linux,
Windows, and Android remain `not run`.

### M6-G5e — Android arm64 qualification: complete

GitHub Actions run `29743425559` built the Android arm64 Naive binary and
plugin APK at NaiveProxy `58a7ac9821d9b01d5ab95f154e0eeff33fb4ea84` on
Ubuntu 22.04.5, then verified the packaged AArch64 ELF and exported SagerNet
plugin provider boundary:

```text
M6_G5E_ANDROID_ARM64_ELF_OK
M6_G5E_ANDROID_PLUGIN_PACKAGE_OK
M6_G5E_ANDROID_ARM64_BUILD_READY
```

Runtime qualification then ran on a physical Android arm64 device (Android 16,
`Android-build-redacted`, arm64-v8a, ADB) against the LAN M5
Caddy/forwardproxy server (Caddy `dd9a89c1`, forwardproxy `964281a9`) with
the shipped NDK Release `naive` built at NaiveProxy
`474a1e4b0aeb9c64e6d0083eaddd205c887bf608` (31 commits ahead of the GitHub
build; UDP-hardening and Windows G5d work). A temporary per-process CA
(`SSL_CERT_FILE`) authorized the direct-`naive` runs, and a temporary
system-trust-store installation of the same test CA authorized the host-app
runs; both trust inputs were removed before closeout.

Verified on-device: shipped UDP echo, the 1314-byte live-ceiling and
1200-byte safe payloads, DNS, zero/oversize limits, control close, the
125-second server-idle reconnect (server `idle_expired` plus a fresh
association in the Caddy log), an independent quic-go HTTP/3 application
probe through CONNECT-UDP, forced-SOCKS TCP parity expecting
`m5-production-tcp-ok`, and QUIC proxy-datagram NetLog evidence. The
untrusted A/B (test CA removed from the system trust store, no
`SSL_CERT_FILE`) rejected the self-signed proxy certificate with a QUIC
handshake failure, no CONNECT-UDP success reached the server, and the echo
probe timed out. The NekoBox (`moe.nb4a`) host app ran the SagerNet naive
plugin node with `tun0` up, a real-web CONNECT returning HTTP 200 through
the plugin, and traffic continuing across `am freeze`/`am unfreeze`.

```text
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_DEFAULT_CERT_VERIFIER_OK
M5_G5_PRODUCTION_ECHO_OK
G5E_PAYLOAD_1314_OK
G5E_PAYLOAD_1200_OK
M3_G5_ZERO_OVERSIZE_OK
M3_G4_DNS_OK
M5_G2_HTTP3_APPLICATION_OK
M5_G5_PRODUCTION_TCP_OK
M5_G4_CONTROL_CLOSE_OK
M5_G4_SERVER_IDLE_RECONNECT_OK
M5_G5_H3_DATAGRAM_EVIDENCE_OK
M6_G5E_HOST_APP_NEKOBBOX_OK
M6_G5E_LIFECYCLE_FREEZE_OK
M6_G5E_ANDROID_ARM64_OK
```

The machine-readable `android-arm64` record is now `verified`, closing the
final open G5 platform row. With macOS, Linux, Windows, and Android all
verified and the G5f wire gate passing, the G5 exit is satisfied and
`M6_G5_PLATFORM_QUALIFICATION_OK` is recorded.

### M6-G5c — Linux x64 qualification: complete

The shipped-product fixture now selects the platform default verifier without
a bypass: macOS uses its temporary user-domain root, native Linux reads a
temporary per-process `SSL_CERT_FILE`, and Windows uses the temporary user Root
store. Every positive path removes its trust input and starts shipped `naive`
again to require `M5_G5_TRUST_CLEANUP_OK` from an untrusted connection.

`tests/m6/g5_linux_qualification.sh` requires native `Linux x86_64`, exact
NaiveProxy/forwardproxy/Caddy revisions, Go 1.25.12, and a reproducibly built
server. It composes shipped-client echo, DNS, independent H3, TCP, trust,
server idle, focused impairment, lifecycle pressure, and server regressions.
The pinned `.github/workflows/m6-platform-qualification.yml` job runs on an
actual GitHub-hosted Ubuntu x64 runner and retains only aggregate marker and
revision evidence. Local macOS checks validate its shell/static contract but
are not counted as Linux runtime evidence.

The first native-x64 run, GitHub Actions `29727010346`, completed the full
Naive Release/native-UDP build and pinned Caddy/forwardproxy build, then
stopped before product traffic because Caddy's automatic HTTPS redirect tried
to bind privileged Linux port 80. This is fixture portability rather than a
runtime defect. Forwardproxy fixture commit `444667f` adds
`auto_https disable_redirects`; the HTTPS/H3 listener and production module
stack are unchanged. That failed run is build evidence only and is not counted
as G5c runtime qualification.

The second native-x64 run, GitHub Actions `29729141865`, built NaiveProxy and
the pinned production server successfully, then passed shipped UDP echo, DNS,
and the independent HTTP/3 application probe. Its forced TCP parity request
timed out, exposing the cross-protocol padding-cache defect described above;
it did not emit `M6_G5C_LINUX_X64_OK` and is not qualification evidence. The
next runner pins forwardproxy qualification head `f14924cd` (runtime fix
`baa7f2dd`) and forces SOCKS independently of `NO_PROXY`.

### M6 forwardproxy qualification fixture correction — verified

The failed hostname-site parity result was independently reduced to a Caddy
route-matching issue, not an HTTP/3 response-header serialization defect.
Ordinary CONNECT uses the target as its `:authority`, so a site address such
as `https://m5-proxy.localhost:<port>` prevents the request from reaching the
forwardproxy handler. CONNECT-UDP uses the proxy authority and therefore hid
the fixture error. Forwardproxy qualification commit `f14924cd` changes
`tests/m5/Caddyfile-trusted` to an explicit-certificate `https://:<port>` TLS
listener without a Host matcher and adds
`scripts/test-m6-hostless-forward-proxy.sh`.

Verified with the rebuilt Caddy binary and the independent quic-go client:

```text
M6_H3_TCP_PADDING_INTEROP_OK
M4_G5_BINARY_SMOKE_OK
M6_HOSTLESS_TCP_UDP_INTEROP_OK
```

The same qualification head passed owner `go test ./...`, `go test -race ./...`,
the complete `scripts/test-m4.sh`, and the complete
`scripts/test-m4-g5-server.sh` matrix, including idle expiry, restart,
resource, and log-privacy markers. The original platform qualification scripts
and M5 shipped-product default pin used
`f14924cdedc93c28a2b92c8120538ea5beee28fb`. Current/future runs advance to
test-only qualification head `964281a9797efd9a4c953f6273c73e397e777864`;
the runtime padding fix remains separately identified as `baa7f2dd`.

The corrected native run, GitHub Actions `29754432052`, job `88393013948`,
completed on Ubuntu 22.04.5 LTS x86_64 at NaiveProxy
`f7e206a308404d8324e609bc0463f3b7dc7734e6`, forwardproxy
`f14924cdedc93c28a2b92c8120538ea5beee28fb`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. It reproduced the Release client
and pinned server, then passed shipped UDP echo, DNS, independent HTTP/3,
forced-SOCKS TCP, default-verifier rejection/acceptance/cleanup, server idle,
all five impairment profiles, the 60-second pressure matrix, and server
regressions. The redacted artifact ended with:

```text
M6_G5C_LINUX_PRODUCT_OK
M6_G2_NETWORK_IMPAIRMENT_OK
M6_G3_STRESS_SMOKE_MATRIX_OK
M6_G5C_LINUX_SERVER_OK
M6_G5C_LINUX_X64_OK
```

The machine-readable platform record now marks `linux-x64` verified. Windows
and Android remain `not run`.

### M6-G5d — Windows x64 qualification: complete

GitHub Actions run `30013662603` at NaiveProxy `d958cc8017` reproduced the
Windows Release client and pinned Caddy/forwardproxy server, then stalled in
the temporary user-root installation phase. Its original watchdog terminated
only the MSYS shell while a native child survived. Commit `4b2d50833d` split
the trust phases, added process-tree termination, and raised the outer job
budget; the next run proved that process-tree cleanup exits promptly.

Run `30060226705` at `4b2d50833d` again reproduced both binaries and passed the
real shipped-client untrusted-certificate rejection. It then stopped at
`temporary-trust-store-check`. Retained redacted evidence showed
`certutil -user -addstore` reporting success, followed by an exact SHA-1 store
query returning `NTE_NOT_FOUND`; MSYS process-state polling then remained
blocked until the 20-minute product watchdog. No positive product marker or
G5d pass marker was emitted, so neither run is Windows runtime evidence.

Commit `a620d7da7d` removes `certutil` and its MSYS PID polling from the Windows
fixture. Installation, exact-thumbprint presence, and removal now use
synchronous .NET `X509Store("Root", CurrentUser)` operations. The real shipped
`naive.exe` still owns the meaningful trust proof: untrusted failure, positive
traffic after temporary trust, and failure again after exact cleanup. Local
non-mutating verification passed:

```text
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_NEGATIVE_ONLY_OK
M6_G5_PLATFORM_CONTRACT_OK
```

Shell syntax and `git diff --check` also passed. At that point the
`windows-x64` machine record remained `not run`, and `a620d7da7d` was only a
candidate pending a native rerun.

The native rerun `30064158390` at NaiveProxy `2bc4e1e7b8` superseded that
candidate. It again reproduced the Windows Release client and pinned server,
and the shipped-client negative phase passed. The .NET operation then hung in
`temporary-trust-install`; the product watchdog fired after 20 minutes, and
the protected Root-store operation prevented prompt process-tree teardown
until the 180-minute job limit cancelled the run. This proves the unstable
boundary is unattended mutation of `CurrentUser\Root`, not the choice between
`certutil` and .NET APIs. The run emitted no positive product or G5d marker.

Chromium's Windows `TrustStoreWin` source provides a narrower supported local
trust path: it reads `LocalMachine\TrustedPeople` and treats self-signed
server certificates there as trusted leaves, while intentionally excluding
`CurrentUser\TrustedPeople`. Commit `d5875a05d3` follows that boundary. The
Windows proxy fixture now uses a one-day self-signed server leaf, installs and
removes only its exact thumbprint in machine `TrustedPeople`, and retains the
real shipped `naive.exe` negative/positive/cleanup-negative proof. It never
modifies a Root store and does not change `CertVerifier::CreateDefault()`.

The workflow now runs a three-minute native TrustedPeople install/check/remove
preflight before the approximately 50-minute Chromium build. Local Shell,
workflow-YAML, 19-test contract, `git diff --check`, and non-mutating shipped-
client negative checks pass. `windows-x64` remains `not run` until the new
native workflow passes through `M6_G5D_WINDOWS_X64_OK`.

Run `30084029306`, job `89451910893`, at NaiveProxy `f32990a75a`,
forwardproxy `f14924cd`, and Caddy `dd9a89c1` validated the new trust boundary:
the three-second TrustedPeople preflight passed, the Windows Release client
and pinned server rebuilt, the shipped default verifier rejected the
untrusted leaf, and trusted UDP echo plus DNS emitted
`M5_G5_PRODUCTION_ECHO_OK` and `M3_G4_DNS_OK`. The independent HTTP/3
application probe then reported `timeout: no recent network activity` while
its SOCKS target was the custom domain `m5-h3.localhost`; no H3, TCP,
lifecycle, cleanup-negative, product, or G5d final marker was emitted.

Candidate test-fixture commit `a2e06cc21a` removes that Windows-specific
resolver dependency. Windows now sends the exact `localhost` name as an RFC
1928 domain target while retaining `m5-h3.localhost` as the inner HTTP/3 TLS
identity and explicit fixture CA. Other platforms keep the audited target
unchanged. The H3 probe output is captured separately so failures emit only
the existing redacted client/server lifecycle logs rather than a raw target
URL. Local verification passed:

```text
19 M6 contract tests
M5 Go tests
Shell syntax checks
WORKFLOW_YAML_OK
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_NEGATIVE_ONLY_OK
git diff --check
```

This is a candidate harness correction, not Windows runtime evidence. G5d
remains fail-closed until a fresh native run emits every required marker and
ends with `M6_G5D_WINDOWS_X64_OK`.

Run `30097890690`, job `89496376267`, at NaiveProxy `35136124ed` proved the
H3 resolver correction. In addition to the earlier trust, build, rejection,
UDP echo, and DNS evidence, it emitted:

```text
M5_G2_HTTP3_CONNECTION_CLOSED
M5_G2_HTTP3_APPLICATION_OK
M5_G5_PRODUCTION_TCP_OK
```

The next control-close assertion received Python `ConnectionResetError` with
Windows error `10054` (`WSAECONNRESET`) after sending to the already closed
local UDP relay. The assertion already accepted POSIX timeout and an explicit
`ConnectionRefusedError`; no application datagram was delivered. Candidate
test-oracle commit `3c3db3885c` accepts only `winerror == 10054`, only when the
caller has explicitly allowed a refused closed relay. Other reset errors and
unexpected packets still fail. Five behavior tests cover timeout, allowed and
disallowed 10054, another reset, and unexpected data. The test is also part of
the three-minute Windows preflight so future regressions fail before the
Chromium build.

Local candidate verification passed:

```text
5 Windows UDP error-semantics tests
19 M6 contract tests
WORKFLOW_YAML_OK
M2_SOCKS5_UDP_INGRESS_OK
Shell syntax checks
git diff --check
```

No production source changed in this gate. G5d is **complete**: the fresh native
Windows run passed control close, idle/reconnect, trust cleanup, and server
tests, emitting `M6_G5D_WINDOWS_X64_OK` (GitHub Actions full run `30167583501`).

The `3c3db3885c` preflight test was later found insufficient: it injected a
synthetic `ConnectionResetError` subclass with a hand-written `winerror`
attribute, so the exact numeric guard could pass without exercising a real
Winsock exception. Run `30102201100` at `9951aedfae` entered the full product
gate but did not produce a G5d marker; it was cancelled after the failed
product path remained in cleanup. It is not qualification evidence.

Commit `5bcdd97107` replaces that synthetic boundary. An explicitly opted-in
closed-relay assertion now accepts the `ConnectionResetError` type without
depending on runtime-specific numeric attributes; all other connection errors
and unexpected packets still fail. The Windows-only sixth test sends a real
UDP packet to an actual closed loopback port, captures the native exception,
and passes that object through the production test oracle. A new
`windows-x64-preflight` workflow input runs this probe and the TrustedPeople
lifecycle without building Chromium or Caddy.

GitHub Actions run `30107431553`, job `89528169413`, passed on native Windows
Server 2022 at `5bcdd97107` in 29 seconds:

```text
Ran 6 tests in 0.018s
OK
M6_G5D_WINDOWS_UDP_ERROR_SEMANTICS_OK
M5_WINDOWS_TRUSTED_LEAF_INSTALL_OK
M5_WINDOWS_TRUSTED_LEAF_CHECK_OK
M5_WINDOWS_TRUSTED_LEAF_REMOVE_OK
M6_G5D_WINDOWS_TRUST_PREFLIGHT_OK
M6_G5D_WINDOWS_PREFLIGHT_ONLY_OK
```

Local verification also passed the 19 M6 contract tests, workflow YAML and
Shell checks, Python compilation, `git diff --check`, and the complete
`M2_SOCKS5_UDP_INGRESS_OK` regression. No production source changed. This
preflight closes the specific synthetic-test gap but does not qualify G5d;
the full Windows product marker remains required.

Full Windows run `30107604684`, job `89528773330`, then passed the build,
trusted-leaf preflight, shipped product echo/DNS/H3/TCP path, control-close
oracle, and trust cleanup. It reached the final forwardproxy owner suite and
failed because legacy test fixtures assumed arbitrary `*.localhost` names
resolve on Windows; probe-resistance error handling also dereferenced a nil
response after the setup failure. This was a later, independent test-fixture
boundary, not a recurrence of the Winsock fix or a product-path failure.

Forwardproxy test-only commits `9b40eeb5cede209143bba47fce3b05060d7e1bce`
and `964281a9797efd9a4c953f6273c73e397e777864` now:

- dial local proxy sockets through `127.0.0.1` while preserving logical
  Host/SNI identities;
- use explicit loopback target identities that do not require wildcard DNS;
- wait for successful TLS handshakes instead of sleeping a fixed 500 ms; and
- fail probe-resistance setup errors directly rather than dereferencing nil.

Local `go test -count=1 ./...` and `go test -race -count=1 ./...` pass at the
new head. Native Windows fast run `30167351024`, job `89702525017`, passed the
complete owner suite in 2m17s and emitted:

```text
ok github.com/caddyserver/forwardproxy 3.740s
M6_G5D_WINDOWS_FORWARDPROXY_TESTS_OK
```

No production source changed. Current/future qualification scripts pin
`964281a9797efd9a4c953f6273c73e397e777864`; historical macOS/Linux records
retain the exact `f14924cd` revision they actually verified.

Full native Windows run `30167583501`, job `89703137849`, completed in 59m43s
on Microsoft Windows Server 2022 `10.0.20348.5386` x86_64 at NaiveProxy
`3ed7cbc3defa48010d82cfab57ae1870873eaef5`, forwardproxy
`964281a9797efd9a4c953f6273c73e397e777864`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. The Release client build took
52m35s, the pinned server build 1m42s, and the complete runtime gate 3m46s.
The redacted evidence includes default-verifier rejection, temporary
TrustedPeople acceptance and removal, UDP echo, DNS, independent HTTP/3,
forced-SOCKS TCP, control close, server/client idle reconnect, H3 DATAGRAM,
no-padding baseline, and the final forwardproxy server suite. Final markers:

```text
M6_G5D_WINDOWS_PRODUCT_OK
M6_G5D_WINDOWS_SHIPPED_CLIENT_OK
M6_G5D_WINDOWS_SERVER_OK
M6_G5D_WINDOWS_X64_OK
```

The machine-readable `windows-x64` row is now `verified`. Android arm64 real-device qualification (G5e) is also complete (`cfb42328ac`), so all
four G5 platform rows are verified.

### M6-G5f — cross-platform wire interoperability: complete

Commit `13df84bfd9` adds a pinned cross-platform gate. The macOS arm64
Chromium/Naive production backend connects over real QUIC/H3 to the exact
forwardproxy/Caddy server built as a Linux arm64 ELF and executed in a Lima
2.1.1 Alpine 3.23.3 VM. The VM image digest, Go/xcaddy/Caddy/forwardproxy
inputs, guest architecture, and client source ancestry are all checked before
traffic starts. This is wire-interoperability evidence, not a substitute for
the native Linux x64 or Android arm64 platform rows.

Verified markers:

```text
M6_G5F_UDP_OK
M5_G2_HTTP3_APPLICATION_OK
M6_G5F_LINUX_ARM64_SERVER_OK
M6_G5F_HTTP3_APPLICATION_OK
M6_G5F_TCP_OK
M6_G5F_PRIVACY_OK
M6_G5F_MACOS_CLIENT_LINUX_SERVER_OK
```

The gate uses only the test runner's local certificate verifier; default
certificate-verifier evidence remains owned by each native G5 platform row.
Temporary keys, logs, cross-built binaries, and guest processes are removed by
the runner. The final `M6_G5_PLATFORM_QUALIFICATION_OK` marker was withheld
until the Android arm64 record was verified; G5e closed that row, so the
marker is now recorded in the G5e section.

### M6-G6 — release-candidate closeout and independent audit: complete

Date: 2026-08-29 (Asia/Shanghai)

The release matrix runner `tests/m6/g6_release_matrix.sh` reproduces the
release from clean inputs in one pass against the exact release pins: client
runtime `474a1e4b0aeb9c64e6d0083eaddd205c887bf608` (HEAD `73a0afe80d` is
docs-only; `git diff 474a1e4b0a..HEAD -- src/net` is empty), forwardproxy
`964281a9` (audited runtime base `8f044e27`, build lock `e9663e4`), Caddy
`dd9a89c1` (rebuilt per run into the matrix tmp root by
`forwardproxy/scripts/build-naive-caddy.sh` with Go `1.25.12` / xcaddy
`v0.4.5`; `go version -m` on the binary is checked for `go1.25.12`), Go
`1.25.12`, quic-go `0.59.0`, gn `2407 (3357c4f51b1a)` at the DEPS pin.

Gate order: release pin checks -> 11 Release ninja targets (no-op against
the frozen tree) -> M1-M3 matrix -> 56-case Naive TCP owner matrix -> Caddy
RC rebuild + toolchain pin check -> pre-seeded isolated Go module cache
for the shipped-binary gate -> `tests/m5/g5_production_binary.sh` on the RC
pins -> three clean-root M5 product repetitions -> M6 G2 network-impairment
matrix (5 profiles x 3 clean-root runs) -> M6 G3 3600 s qualification soak
-> M6 G4 sanitizer/fuzz -> forwardproxy owner/legacy/privacy/race
regressions -> Caddy `modules/caddyhttp` regression -> hygiene (3x `git
diff --check`, forbidden-generated-artifact scans over the three M6 diff
ranges, untracked allow-list).

Harness fixes landed during G6 (test-only; no gate weakened):

- `tests/m6/g1_live_ceiling.sh` and `tests/m6/g1_shipped_ceiling.sh`:
  `expected_client` advanced from the G2-era pin `17c717793c` to the frozen
  RC `474a1e4b0a`. The five M6 runtime commits (`a5d33cb3f9`,
  `d6f95e9f61`, `cc2208ec87`, `2ec711012e`, `474a1e4b0a`) changed
  `src/net` after the G2-era pin, so the stale pin rejected the RC tree.
- The G6 runner pre-seeds a fresh per-run isolated Go module cache
  (`M6_GO_CACHE_ROOT`) before the G2/G3 stages build the h3-origin /
  socks-h3-probe fixtures, the same fix pattern as the G5 shipped-binary
  gate, protecting probes from a half-extracted shared module directory.
- The G6 runner hygiene stage filters `git status --porcelain` to untracked
  (`??`) lines before the allow-list comparison; modified tracked files (the
  G6 change itself) were previously counted as unexpected.

Matrix execution: the first G6 matrix run (`matrix-run6`, log retained under
`/tmp/g6/`) passed every test stage through server regressions on the
pre-fix runner; its final hygiene stage exposed the untracked-filter harness
bug above, not a product finding. The final run (`matrix-run7`) completed
every stage green on the fixed runner, exiting `0` with:

```text
M6_G6_PRODUCT_REPETITION_OK run=1
M6_G6_PRODUCT_REPETITION_OK run=2
M6_G6_PRODUCT_REPETITION_OK run=3
M6_G2_NETWORK_IMPAIRMENT_OK
M6_G3_STRESS_SOAK_OK
M6_G4_SANITIZER_FUZZ_OK
M6_G6_LOCAL_RELEASE_CHECKLIST_OK
```

plus the M1-M3 marker set (`G1_MASQUE_SMOKE_OK`, `G2_NAIVE_TUNNEL_OK`,
`G3_BASIC_AUTH_OK`, `G5_LIFECYCLE_OK`, `M2_SOCKS5_UDP_INGRESS_OK`,
`M3_NATIVE_UDP_CLIENT_OK`), 56/56 Naive TCP `TEST PASS` cases, the full
shipped-binary marker set including `M5_G5_PRODUCTION_BINARY_OK` and
`M5_G5_UNTRUSTED_CERT_REJECTED_OK`, all three clean-root repetitions, the
forwardproxy `M4_G0`-`M4_G5` server marker sets plus
`M6_H3_TCP_PADDING_INTEROP_OK`, `go test ./...` and `go test -race ./...`
ok, and Caddy `go test ./modules/caddyhttp` ok. The machine contract test
suite (`python3 tests/m6/contract_test.py`) passes 19/19.

G3 qualification soak (`M6_G3_TIER_OK tier=qualification
duration_seconds=3600`): 97,056 datagrams over 6,066 churn waves;
association cap 256 with 1 rejection and 64 reuses; resource peaks runner
RSS 20,064 KiB / fd 526 and caddy RSS 43,648 KiB / fd 12; resource
recovery verified; zero duplicate datagrams under every G2 profile
(`M6_G2_NO_REPLAY_OK` per profile).

G4 evidence: three seeded release codec fuzz runs of 1,000,000 iterations
(seeds `20260720`, `9298`, `1928`; valid corpus 44,027 / 43,374 / 43,459
entries) with `M6_G4_RELEASE_CODEC_FUZZ_OK`; 2,000-iteration seeded
lifecycle; ASan/UBSan build and run `M6_G4_ASAN_UBSAN_OK`; Go server fuzz
budget runs PASS; race suites clean; `M6_G4_SANITIZER_FUZZ_OK`.

Exact-pin artifact inventory (SHA-256, macOS arm64 Release tree; the four
contract-required release targets are marked `*`):

| Artifact | SHA-256 |
| --- | --- |
| `naive` `*` | `fb37dfc7f4132e751fb0057dcf6be366e5c79c90c1afa16c579609f2193d7973` |
| `naive_connect_udp_backend_test` `*` | `5715bb9c907ec33cded198ff23c5600af18c6eefd2ea9cfb99b049e0c9fab9ee` |
| `naive_connect_udp_runner` | `9a53ef5b3a6037efd98fd71061458a9d5ddcae09ebd4cc524b14081901a0e3b9` |
| `naive_socks5_udp_test` `*` | `9ce9cdd2c54c8fa647568012acb99b8a6879b3faa83a75cd74401c5007834f65` |
| `naive_socks5_udp_fuzz_test` | `c76fd3fbf9e6aa0938b16f0006939521669021994ff4daeeefabd5b6a7030cac` |
| `naive_socks5_udp_m3_runner` `*` | `50e0223df5df68459ec3f7fdc178352790fb4e337d7a19669f4ae4cba3471a72` |
| `naive_socks5_udp_runner` | `035f9e175a3079d5cff9190e63a645c6ae7cb50c4d43e55d451b21834eb2d7a2` |
| `naive_socks5_udp_association_test` | `d5e07760e6d07af8520072598d264a5c737867336be8fe266377d7f4a6d2d46a` |
| `naive_socks5_server_socket_state_test` | `f12407d3f1a81e67885554fedab857f0971543435cbea10d67eb5985b86e8e96` |
| `naive_masque_client` | `78dfd4d9d33d6d172e93408be24f6fecb1830c2147152043d3ad0909051ee186` |
| `naive_masque_probe` | `8f7ae94edd4e8dc2b28c35e37208b0178763fc523ec56e7280d7733d57f1ee58` |
| `naive_masque_server` (test fixture) | `772983e729b9c3d8f9b57090c1a7f8ea51c503905f413b5f5bee0cf8d88e7de0` |
| Caddy RC binary (per-run rebuild) | `28b638d12e612f82aaf12fbdef7302030ec5e67f73908944fa4055a9c6328bb6` |

The Android arm64 artifacts on the NAS were re-verified at the same SHAs as
the G5e record: `naive`
`e205d1b76de73416a752117ff26ead42cee1330ea9e3ace3d2090509b81c791b`, plugin
APK `aee57c2b26a76ae2881f0335d0075055e105d97b14136ff818fc745ec6f28e7a`.

Post-run cleanup verified: no test processes (caddy/masque/naive/echo/
shaper/probe), no test listener ports (8443, 8500-8503, 19661-19664), no
matrix tmp roots, and no G5 test CA remaining in the login keychain.

Maintainer approval (G6 item 4): the project owner approved the Chromium
API boundary (sole adapter `NaiveQuicProxyStreamRequest` over
`NaiveConnectUdpTunnel` + `ConnectViaStream()`, preemptive auth via
`HttpAuthController`, 407 as fresh-stream failure, pre-`Build()` QUIC
origins per `333b7cb253`, M6 hardening confined to Naive's UDP backend, TCP
path and `CertVerifier::CreateDefault()` untouched) and the frozen payload
policy (1200 B baseline, 1314 B host ceiling, drop-oversize without
truncation, no replay, no padding) on 2026-08-29.

Independent audit (G6 item 5): a non-interactive `agy` session (Gemini
3.1 Pro High, `agy --model gemini-3.1-pro-high -p`, permission prompts
disabled, 45-minute print timeout) performed a defensive release-quality,
read-only review over the exact M6 ranges: client
`eaf172d971..73a0afe80d` (M6 runtime commits `a5d33cb3f9`,
`d6f95e9f61`, `cc2208ec87`, `2ec711012e`, `474a1e4b0a`, plus platform
qualification and documentation commits), forwardproxy
`8f044e27..964281a9` (including build lock `e9663e4`), Caddy
`cce894a8..dd9a89c1` (scoped post-fix audit of the TLS module race fix
`dd9a89c1`), and the G6 harness change set (new G6 runner, M5
environment overrides, M6 ceiling pin advances). The reviewer
operated read-only, inspected the Git ranges, critical sources, and the
recorded evidence (matrix logs, markers, pins), and re-verified that the
M5 scripts' defaults remain the audited pins (no gate weakened).

Findings: blocker 0, high 0, medium 0, low 2.

- LOW (client): commit `17c717793c` adds a
  `notify_proxy_delegate_of_response` constructor parameter at
  `quic_session_pool.cc:1925`, a technical `QuicSessionPool` change that
  is purely an API adaptation: the parameter defaults to `true`, so all
  pre-existing streams keep today's behavior, and it is passed `false`
  only for UDP datagram streams so UDP responses cannot pollute the TCP
  padding capability cache. No security risk. Owner acknowledged with
  mitigation on 2026-08-29: TCP data path and padding behavior stay
  covered by the 56-case TCP owner matrix, `M6_H3_TCP_PADDING_INTEROP_OK`,
  and `M5_G5_NO_PADDING_BASELINE_OK`, all green in the G6 matrix.
- LOW (client, informational): the M6 UDP association hardening
  (transient relay error tolerance `d6f95e9f61`, zombie-target eviction
  `a5d33cb3f9`) introduces no vulnerability or replay vector; ambiguous
  datagrams are dropped without replay.

Verdict:

```text
AUDIT_PASS
Zero blocker, high, or medium findings.
```

M6-G6 exit: `M6_G6_LOCAL_RELEASE_CHECKLIST_OK` plus independent
`AUDIT_PASS` is recorded, and the final marker
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closes M6.

## Canonical M5 verification commands

```bash
cd /path/to/naiveproxy
./tests/m5/g4_lifecycle_matrix.sh
./tests/m5/g5_production_binary.sh
./tests/socks5_udp_m5.sh
./tests/m5/g6_finalize.sh
```

`g4_lifecycle_matrix.sh` is non-privileged. The G5 and cumulative commands
install one short-lived user-domain root after an explicit macOS confirmation;
their traps remove the trust/certificate and verify the endpoint is untrusted
again. `M5_G5_STOP_AFTER_NEGATIVE=1` runs only the non-mutating negative path.

## Canonical server verification commands

```bash
cd /path/to/naive-forwardproxy-m4
GO_BIN=/path/to/naive-m4/go1.25.12/bin/go \
XCADDY_BIN=/path/to/naive-m4/bin/xcaddy \
CADDY_SOURCE_DIR=/path/to/caddy-naive-udp-m4 \
  ./scripts/build-naive-caddy.sh /tmp/naive-m4-caddy
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  ./scripts/test-m4.sh
GO_BIN=/path/to/naive-m4/go1.25.12/bin/go \
CADDY_BIN=/tmp/naive-m4-caddy \
  ./scripts/test-m4-g5-server.sh
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  go test -race ./...

cd /path/to/caddy-naive-udp-m4
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  go test ./modules/caddyhttp
```

## Canonical client verification commands

```bash
cd src
ninja -C out/Release naive naive_masque_server naive_masque_client \
  naive_masque_probe naive_connect_udp_runner naive_socks5_udp_test \
  naive_socks5_server_socket_state_test naive_socks5_udp_association_test \
  naive_socks5_udp_runner naive_connect_udp_backend_test \
  naive_socks5_udp_m3_runner
../tests/masque_g1_smoke.sh
../tests/masque_g2_naive_tunnel.sh
../tests/masque_g3_basic_auth.sh
../tests/masque_g5_lifecycle.sh
../tests/socks5_udp_m2.sh
../tests/socks5_udp_m3.sh
../tests/basic.sh out/Release/naive
git diff --check
```

Expected markers:

- `G1_MASQUE_SMOKE_OK`
- `G2_NAIVE_TUNNEL_OK`
- `G3_BASIC_AUTH_OK`
- `G5_LIFECYCLE_OK`
- `M2_G1_CODEC_OK`
- `M2_G2_DETERMINISTIC_STATE_MACHINE_OK`
- `M2_G4_G5_UDP_ASSOCIATION_OK`
- `M2_G2_AUTHENTICATED_UDP_OK`
- `M2_G3_NO_BACKEND_REJECTION_OK`
- `M2_SOCKS5_UDP_INGRESS_OK`
- `M3_G0_BACKEND_CONTRACT_OK`
- `M3_G0_TEST_SKELETON_OK`
- `M3_G1_SINGLE_TARGET_OK`
- `M3_G2_MULTI_TARGET_LIMITS_OK`
- `M3_G2_FAILURE_ISOLATION_OK`
- `M3_G2_ACTIVE_ASSOCIATION_LIMIT_OK`
- `M3_G3_DIRECT_REJECTION_OK`
- `M3_G3_H2_REJECTION_OK`
- `M3_G3_MIXED_CHAIN_REJECTION_OK`
- `M3_G3_NO_BACKEND_REJECTION_OK`
- `M3_G3_IPV4_ECHO_OK`
- `M3_G3_AUTH_ECHO_OK`
- `M3_G3_PRODUCTION_WIRING_OK`
- `M3_G4_IPV4_OK`
- `M3_G4_IPV6_OK`
- `M3_G4_DOMAIN_OK`
- `M3_G4_DNS_OK`
- `M3_G4_AUTH_OK`
- `M3_G4_MULTI_TARGET_OK`
- `M3_G4_CONCURRENT_ASSOCIATIONS_OK`
- `M3_G4_NETLOG_REDACTION_OK`
- `M3_G5_DETERMINISTIC_LIFECYCLE_OK`
- `M3_G5_SESSION_RECONNECT_OK`
- `M3_G5_IDLE_RECONNECT_OK`
- `M3_G5_ZERO_OVERSIZE_OK`
- `M3_G5_BACKEND_DESTRUCTION_OK`
- `M3_G5_PENDING_CONNECT_CLOSE_OK`
- `M3_G5_CONNECT_TIMEOUT_OK`
- `M3_G5_AUTH_MISSING_OK`
- `M3_G5_AUTH_WRONG_OK`
- `M3_G5_AUTH_FAILURES_OK`
- `M3_G5_RECONNECT_OK`
- `M3_G5_LIFECYCLE_OK`
- `M3_G5_LIMITS_OK`
- `M3_NATIVE_UDP_CLIENT_OK`
- exit code `0` from `tests/basic.sh`
- `ninja: no work to do` or a successful link

The final M3 local verification on 2026-07-19 passed every named M1/M2/M3
target and script, all 56 existing TCP HTTP/HTTPS/auth/chain cases, three
consecutive M3 lifecycle stress runs, and `git diff --check`. The independent
M3 `agy` audit reran the required matrix and returned `AUDIT_PASS`; see
`docs/m3-agy-audit.md`.

Historical M1 and M2 independent audits also returned `AUDIT_PASS` with no
blocking, high, or medium findings. Their audit histories and deferred
low-priority observations remain in `docs/m1-agy-audit.md` and
`docs/m2-agy-audit.md`.

## Frozen boundaries

- TCP behavior must remain unchanged.
- UDP v1 is allowed only over `quic://` / HTTP/3.
- Native UDP means RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM, never a custom
  UDP-over-stream fallback.
- The QUICHE endpoint is an M1 interoperability fixture, not the production
  server architecture.
- M2's fake backend is test-only. Production uses the audited M3 adapter and
  target backend to reach the real M1 CONNECT-UDP tunnel.
- M5 deterministic runs may use the M3 runner's test-only certificate verifier,
  but milestone completion requires a separate shipped-`naive` smoke through
  the default certificate verifier. Do not add a production bypass.
- M5 application evidence requires an independent SOCKS5-UDP-backed HTTP/3
  client; generic UDP echo does not satisfy that gate by itself.
- UDP padding remains intentionally out of v1 until traffic-shape measurements
  justify a separate unreliable-datagram design.

## Working-tree state

M1 has been committed as `e11a7733` (`Complete native UDP M1 foundation`) on
`codex/native-udp-foundation`. M2 has been committed as `fe817a87` (`Complete
SOCKS5 UDP ingress M2`) after all local gates and the independent audit passed.
The reviewed M3 execution plan is committed as `8720c912` (`Plan native UDP M3
execution`). G0, G1, and G2 are committed as `83904eb8`, `4541f756`, and
`1bd5789e`; G3 production composition is committed as `c2352710`. G4
interoperability and the post-G3 privacy/lifecycle audit fixes are committed as
`4927d06a`. G5 lifecycle/recovery/limits work is complete and verified in
`578e3992`. G6 regressions, stress verification, and independent audit
closeout is commit `2bb83aec`. M4-G0–G5 are committed in the separate server
fork through `7243519`; the post-audit CI-pin closure is `8f044e2`. Caddy's
three audited patches end at `cce894a8`. M4's local verification and audit
evidence are recorded in `4ec0f8bb9a`. The M5 G0–G6 execution plan now exists;
M5-G0 is commit `014cdc4761`; M5-G1 is main commit `67caa8f131` plus the
forwardproxy fixture commits `d922441` and `88ac298`. M5-G2 is commit
`eeccb7cb30`; M5-G3 is `fbdd8af531`. The shipped-client QUIC context ordering
fix is `333b7cb253`; M5-G4/G5 harness and evidence are `c73b5a486f`; the G6
runner is `4a395a7f4e` with module-cwd correction `d1aee3663f`; the independent
audit covers the local closeout `eaf172d971` and returns `AUDIT_PASS`.
Generated `.DS_Store` and `src/tmp/` entries remain unrelated and must not be
included in future feature commits.
M6-G6 closes the branch with harness commit `2430df7acc` (release matrix
plus harness pin fixes) and closeout record `e96cfbd0cc` (status ledger,
release guide, payload policy), both pushed to origin.

## Post-M7 CONNECT-UDP admission tuning — verified

On 2026-09-03 CST, the production forwardproxy admission caps were raised to
accommodate the router's single sing-box UDP fan-out: handler-wide active
associations from 256 to 512, and per-source-public-IP active associations
from 32 to 128. The narrow forwardproxy change is commit `25b4cd6`; its full
`go test ./...` suite passed with Go 1.25.12 in the isolated build tree.

The deployed Caddy binary was rebuilt with the locked M7 inputs (Caddy
`3bcce47`, quic-go `f84ad47630af`, Go 1.25.12, xcaddy 0.4.5) and installed as
`/root/native-udp-deploy/caddy-naive-udp`, SHA256
`fe6de99fee5d3502644cc7c4d0944318e4b78bedbf78ff47158261e1ccef9ad9`.
`native-udp-caddy.service` restarted successfully with `NRestarts=0` and
8443/8444 listeners intact. The previous binary remains at
`/root/native-udp-deploy/caddy-naive-udp.pre-limit-20260902-2345` for rollback.
