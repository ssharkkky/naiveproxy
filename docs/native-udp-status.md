# NaiveProxy Native UDP Project Status

Last updated: 2026-07-20 (Asia/Shanghai)

Documentation entry point: [`README.md`](README.md). Active milestone plan:
[`m6-execution-plan.md`](m6-execution-plan.md).

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
| M6 — hardening and release candidate | In progress; G0/G2 complete; G1/G3/G4/G5 open | G0 contract, G1b1/G1c measurements, three-run G2 impairment matrix, client/forwardproxy G4 evidence, G5a record contract; Caddy race fix owner regressions passed but post-fix G4/G3 are required | Re-run G4 on Caddy `dd9a89c1`, then G3 qualification |

M1 is complete as an integration spike. M2 supplies the local SOCKS5 UDP
ingress and retains its test-only echo/no-backend modes. M3 G0–G6 compose
that ingress with the real M1 CONNECT-UDP tunnel in production while keeping
the M2 runner independent, and the independent final audit passed. Remaining
M6 work is sequenced in `docs/m6-execution-plan.md` and summarized in
`docs/native-udp-development-plan.md`.

### Overall progress estimate

- Milestone count: M0–M5 are complete, 6 of 7 milestones, or 86%.
- Weighted engineering estimate: approximately 93–95% complete. This weights
  the remaining shipped-client policy, qualification, platform, and release
  evidence more heavily than a simple milestone count; it is not a release
  claim.
- Chromium-driven native UDP client: M1-M3 are independently audited; the M5
  production-context ordering fix `333b7cb253` passed the complete owner matrix
  and is included in the completed M5-G6 audit boundary.
- Production Caddy/`forwardproxy` native UDP server: 100% complete and
  independently audited.
- End-to-end product MVP: 100% complete and independently audited. Release
  hardening is not started.

Current remaining planning range:

| Remaining milestone | Estimated effort |
| --- | ---: |
| M6 — hardening and release candidate | 10–20 person-days |
| **Total remaining** | **10–20 person-days** |

These are engineering estimates, not elapsed-calendar guarantees. The product
is not production-ready: M6 still lacks shipped-client G1b2/G1d evidence,
cross-platform G5 qualification, and G6 release/audit closeout.

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

Status: in progress. The active plan is
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
cd /Users/stoneshi/Documents/naive\ proxy
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

### M6-G1 — inner payload ceiling and PMTU behavior: in progress

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

G1 remains open. G1b2 must repeat the ceiling measurement through shipped
`naive` and its default verifier in an explicitly authorized trust window;
G1d must then freeze the 1200-byte release policy and run the complete focused
and inherited regressions three times.

Commit `80abaad450` prepares the G1b2 runner without changing trust or claiming
success. It uses the shipped `src/out/Release/naive`, proves the untrusted
negative path first, requires an explicit user-domain CA confirmation, measures
IPv4/IPv6/domain through `payload_probe.py`, requires the previously measured
1314-byte result, scans logs for target/credential leakage, removes the trust
entry, and verifies the certificate is untrusted again. Contract/static checks
pass; the runtime gate remains `not run` until a fresh trust authorization is
given.

The non-mutating negative-only path passed at `182267a1ca`: shipped `naive`
used its default verifier, the untrusted temporary root could not establish a
CONNECT-UDP association, and the run ended with
`M6_G1B2_UNTRUSTED_CERT_REJECTED_OK` and `M6_G1B2_NEGATIVE_ONLY_OK`. This does
not satisfy the positive ceiling or trust-cleanup half of G1b2.

Commits `fc23ce4144` and `a32e95d27a` prepare the G1d closeout runner. After one
G1b2 trust window it requires three complete repetitions of live ceiling,
lowered/restored PMTU, the cumulative M3 client suite, all 56 TCP cases, uncached forwardproxy
tests, and focused Caddy HTTP tests before emitting
`M6_G1_PAYLOAD_PMTU_OK`. The corrected `src/` working-directory invocation of
the TCP matrix passed one non-mutating preflight through all 56 cases. The full
three-run closeout remains unexecuted.

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

### M6-G3 — resource pressure and soak: qualification retry running

Commit `325c77b95a` added the bounded association/churn/resource harness. The
smoke evidence already passed with 256 active client associations, rejection
of the 257th, reuse of 64 released slots, 101 waves, 1616 product datagrams,
and recovery of runner/Caddy file descriptors.

The first unshortened qualification body's pressure work also passed: 5634
waves, 90144 product datagrams, the same 256/257th/64 admission-reuse boundary,
runner RSS 13376 -> 18496 KiB and FD 10 -> 17, and Caddy RSS 41968 -> 41152
KiB and FD 12 -> 13. The wrapper then failed without
`M6_G3_STRESS_SOAK_OK` because its long-running shell had started before the
G4 allowlist change and reached an inconsistent post-probe branch. That run is
not counted as a pass. Commit `8ab48dbee1` replaces the non-portable BSD `sed`
allowlist expressions; a one-second stress smoke completed through privacy and
harness markers, and a fresh 3600-second qualification root is now running.

The G3 harness covers association admission, churn, post-close resource
recovery, and bounded RSS/FD deltas. M5-G4 remains the inherited evidence for
server restart, idle expiry, control close, outer-session shutdown, and no
replay; G2's loss profile remains the inherited outer-QUIC impairment evidence.

### M6-G4 — fuzz, sanitizer, race, and lifecycle hardening: reopened for Caddy fix

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
reproduction passed. The final G4 runner and G3 soak must still be rerun on
this new runtime. The previous `M6_G4_SANITIZER_FUZZ_OK` is retained as
pre-fix evidence only.

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

## Canonical M5 verification commands

```bash
cd /Users/stoneshi/Documents/naive\ proxy
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
cd /Users/stoneshi/Documents/naive-forwardproxy-m4
GO_BIN=/Users/stoneshi/.local/naive-m4/go1.25.12/bin/go \
XCADDY_BIN=/Users/stoneshi/.local/naive-m4/bin/xcaddy \
CADDY_SOURCE_DIR=/Users/stoneshi/Documents/caddy-naive-udp-m4 \
  ./scripts/build-naive-caddy.sh /tmp/naive-m4-caddy
PATH=/Users/stoneshi/.local/naive-m4/go1.25.12/bin:$PATH \
  ./scripts/test-m4.sh
GO_BIN=/Users/stoneshi/.local/naive-m4/go1.25.12/bin/go \
CADDY_BIN=/tmp/naive-m4-caddy \
  ./scripts/test-m4-g5-server.sh
PATH=/Users/stoneshi/.local/naive-m4/go1.25.12/bin:$PATH \
  go test -race ./...

cd /Users/stoneshi/Documents/caddy-naive-udp-m4
PATH=/Users/stoneshi/.local/naive-m4/go1.25.12/bin:$PATH \
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
