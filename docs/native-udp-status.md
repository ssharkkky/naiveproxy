# NaiveProxy Native UDP Project Status

Last updated: 2026-07-18 (Asia/Shanghai)

This is the execution ledger for the native UDP project. The development plan
defines scope and design; this file records what has actually been built and
verified. Update it at every completed G target and milestone.

## Overall milestone status

| Milestone | Status | Verified result | Next gate |
| --- | --- | --- | --- |
| M0 — baseline and guardrails | Complete | Stable Chromium 150 tag, development branch, Release build, TCP baseline | None |
| M1 — Chromium integration spike | Complete and independently audited | Real IPv4/IPv6 tunnel, auth echo, lifecycle and NetLog evidence; `agy` returned `AUDIT_PASS` | Begin M2 SOCKS5 parsing/branching |
| M2 — SOCKS5 UDP ingress | Not started | Architecture and protocol decisions documented only | M1 exit |
| M3 — native UDP client data path | Not started | Some tunnel primitives were pulled forward into M1 | M2 fake-backend exit |
| M4 — production server path | Not started | QUICHE endpoint is test-only and is not the Caddy/forwardproxy implementation | Client behavior frozen by M1 |
| M5 — end-to-end MVP | Not started | No SOCKS5 UDP path exists yet | M2–M4 complete |
| M6 — hardening and release candidate | Not started | Verification matrix exists | MVP passes |

M1 is complete as an integration spike, not as a user-visible UDP feature.
Production ingress remains dormant; the next executable product work is M2.

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

## Current verification commands

```bash
cd src
ninja -C out/Release naive naive_masque_server naive_masque_client \
  naive_masque_probe naive_connect_udp_runner
../tests/masque_g1_smoke.sh
../tests/masque_g2_naive_tunnel.sh
../tests/masque_g3_basic_auth.sh
../tests/masque_g5_lifecycle.sh
../tests/basic.sh out/Release/naive
git diff --check
```

Expected markers:

- `G1_MASQUE_SMOKE_OK`
- `G2_NAIVE_TUNNEL_OK`
- `G3_BASIC_AUTH_OK`
- `G5_LIFECYCLE_OK`
- exit code `0` from `tests/basic.sh`
- `ninja: no work to do` or a successful link

Last full verification on 2026-07-18 passed all five build targets, G1, G2,
G3, G5, and all 56 existing TCP HTTP/HTTPS/auth/chain cases with exit code
zero. `git diff --check` also passed.

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
- SOCKS5 command branching, compliant UDP `BND.ADDR/BND.PORT`, non-QUIC reply
  `0x01`, fragments, association ownership and resource limits remain M2 work.
- UDP padding remains intentionally out of v1 until traffic-shape measurements
  justify a separate unreliable-datagram design.

## Working-tree state

The M1 work is currently uncommitted. Generated `.DS_Store` and `tmp/` entries
are unrelated and must not be included in a future feature commit.
