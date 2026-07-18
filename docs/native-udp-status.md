# NaiveProxy Native UDP Project Status

Last updated: 2026-07-19 (Asia/Shanghai)

This is the execution ledger for the native UDP project. The development plan
defines scope and design; this file records what has actually been built and
verified. Update it at every completed G target and milestone.

## Overall milestone status

| Milestone | Status | Verified result | Next gate |
| --- | --- | --- | --- |
| M0 — baseline and guardrails | Complete | Stable Chromium 150 tag, development branch, Release build, TCP baseline | None |
| M1 — Chromium integration spike | Complete and independently audited | Real IPv4/IPv6 tunnel, auth echo, lifecycle and NetLog evidence; `agy` returned `AUDIT_PASS` | None |
| M2 — SOCKS5 UDP ingress | Complete, audited, and committed | Codec, handshake, real relay, fake backend, deterministic lifecycle and 56 TCP regressions pass; `agy` returned `AUDIT_PASS`; commit `fe817a87` | None |
| M3 — native UDP client data path | Planned; implementation not started | G0–G6 execution plan freezes composition, ownership, limits, verification, and audit gates | Execute M3-G0 backend contract/context seam |
| M4 — production server path | Not started | QUICHE endpoint is test-only and is not the Caddy/forwardproxy implementation | Client behavior frozen by M1 |
| M5 — end-to-end MVP | Not started | Local M2 ingress and M1 tunnel exist but are not composed | M2–M4 complete |
| M6 — hardening and release candidate | Not started | Verification matrix exists | MVP passes |

M1 is complete as an integration spike. M2 now supplies the local SOCKS5 UDP
ingress and a test-only echo backend; it deliberately does not connect that
ingress to the production M1 CONNECT-UDP tunnel. That adapter is M3. Its
executable plan is recorded in `docs/m3-execution-plan.md`.

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
  may be cancelled rather than invoked. The future association owner must
  destroy its tunnel when the session/control association closes.
- No SOCKS command parsing, `NaiveProxy::DoConnect()` branching, UDP relay,
  production server, or TCP data path was added in M1.

## M2 execution ledger — SOCKS5 UDP ingress

M2 is intentionally limited to a local SOCKS5 UDP ingress path with an
injected fake `DatagramBackend`. Production CONNECT-UDP backend integration
remains deferred to M3. The existing TCP data mover is unchanged.

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

## M3 planning ledger — native UDP client data path

Status: planned; ready to execute G0. No M3 production code has started.

The plan was derived from direct inspection of the M1 tunnel and M2 ingress
boundaries, then checked by three independent read-only reviews. The reviews
agreed on these blockers that must be resolved before real-server wiring:

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
- the full M3 path uses `naive_masque_server` as a controlled compliant
  endpoint; production Caddy/`forwardproxy` remains M4.

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

## Current verification commands

```bash
cd src
ninja -C out/Release naive naive_masque_server naive_masque_client \
  naive_masque_probe naive_connect_udp_runner naive_socks5_udp_test \
  naive_socks5_server_socket_state_test naive_socks5_udp_association_test \
  naive_socks5_udp_runner
../tests/masque_g1_smoke.sh
../tests/masque_g2_naive_tunnel.sh
../tests/masque_g3_basic_auth.sh
../tests/masque_g5_lifecycle.sh
../tests/socks5_udp_m2.sh
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
- exit code `0` from `tests/basic.sh`
- `ninja: no work to do` or a successful link

The final local verification on 2026-07-19 passed every named M1 and M2 target,
all M1 scripts, the complete M2 suite, all 56 existing TCP
HTTP/HTTPS/auth/chain cases, and `git diff --check`. The continuing-session
independent `agy` audit reran the same required matrix and returned
`AUDIT_PASS`; see `docs/m2-agy-audit.md`.

The final continuing-session `agy` audit independently reran the same suite
and returned `AUDIT_PASS` with no blocking, high, or medium findings. The audit
history and deferred low-priority observations are recorded in
`docs/m1-agy-audit.md`.

## Frozen boundaries

- TCP behavior must remain unchanged.
- UDP v1 is allowed only over `quic://` / HTTP/3.
- Native UDP means RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM, never a custom
  UDP-over-stream fallback.
- The QUICHE endpoint is an M1 interoperability fixture, not the production
  server architecture.
- M2's fake backend is test-only. M3 must adapt each validated target to the
  real M1 CONNECT-UDP tunnel and add production association/tunnel limits.
- UDP padding remains intentionally out of v1 until traffic-shape measurements
  justify a separate unreliable-datagram design.

## Working-tree state

M1 has been committed as `e11a7733` (`Complete native UDP M1 foundation`) on
`codex/native-udp-foundation`. M2 has been committed as `fe817a87` (`Complete
SOCKS5 UDP ingress M2`) after all local gates and the independent audit passed.
M3 is planned and ready for G0; implementation has not started. Generated
`.DS_Store` and `tmp/` entries remain unrelated and must not be included in
future feature commits.
