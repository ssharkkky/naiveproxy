# M3 Execution Plan — Native UDP Client Data Path

Last updated: 2026-07-19 (Asia/Shanghai)

Documentation entry point: [`README.md`](README.md). Current verified state:
[`native-udp-status.md`](native-udp-status.md).

## 1. Mission and completion boundary

M3 composes the two already verified halves of the client:

```text
M2 SOCKS5 UDP ingress
        -> M3 production datagram backend
        -> M1 NaiveConnectUdpTunnel
        -> RFC 9298 CONNECT-UDP
        -> HTTP/3 DATAGRAM
        -> controlled UDP target
```

M3 is a composition and lifecycle milestone. It does not redesign SOCKS5,
QUIC, HTTP/3, or CONNECT-UDP.

M3 is complete when DNS and generic UDP echo pass through the full SOCKS5
client path against the controlled, compliant `naive_masque_server` endpoint,
using Chromium HTTP/3 DATAGRAM with no custom stream encapsulation. IPv4,
IPv6, domain targets, cached Basic authentication, multiple targets, resource
limits, failure isolation, lifecycle cancellation, all M1/M2 gates, and all 56
existing TCP regressions must pass. An independent `agy` review must return
`AUDIT_PASS` with no blocker, high, or medium finding.

The controlled QUICHE endpoint is an interoperability fixture only. Caddy and
`forwardproxy` production server work remains M4.

## 2. Inherited, frozen boundaries

M3 reuses these completed interfaces without reopening their protocols:

- M1 owns `NaiveQuicProxyStreamRequest`, QUIC request-stream acquisition,
  session-handle retention, preemptive cached proxy authentication, RFC 9298
  request construction, and `NaiveConnectUdpTunnel`.
- One `NaiveConnectUdpTunnel` represents one fixed `HostPortPair`. After
  `Start()` succeeds, its owned `DatagramClientSocket` reads and writes raw UDP
  payloads.
- M2 owns SOCKS5 command parsing, the pre-`NaiveConnection` command branch,
  real UDP relay binding and BND reply, RFC 1928 framing, authenticated client
  endpoint learning, TCP control lifetime, the 64-response queue, and
  association idle cleanup.
- UDP remains eligible only when every hop in the selected proxy chain is
  `quic://`. Ineligible or unavailable UDP backends are rejected during the
  SOCKS5 handshake with reply `0x01`.
- Existing `NaiveConnection`, TCP padding, HTTP/HTTPS/redir paths, and TCP
  configuration behavior are outside the M3 change surface.
- There is no UDP-over-stream fallback, retransmission, CONNECT-IP, UDP
  fragmentation, or UDP payload-padding protocol in v1.
- Interactive 407 recovery is not claimed. Cached/preemptive credentials are
  supported; an actual 407 is an explicit tunnel failure because retry needs a
  fresh HTTP/3 request stream.

## 3. Proposed production composition

```text
NaiveProxy
  |
  +-- PendingSocksHandshake
  |     - association id
  |     - transient NetworkAnonymizationKey
  |     - selected all-QUIC ProxyChain
  |
  +-- Socks5UdpAssociation
        |
        +-- NaiveConnectUdpDatagramBackend       one per association
              |
              +-- map<TargetKey, TargetEntry>
                    |
                    +-- NaiveConnectUdpTunnel     one per target
                          |
                          +-- QuicProxyDatagramClientSocket
```

The tentative class name is `NaiveConnectUdpDatagramBackend`. Naming may be
adjusted in G0, but the ownership boundary must not change.

### Backend creation context

The current zero-argument `Socks5UdpBackendFactory` cannot preserve the
transient `NetworkAnonymizationKey` allocated for a SOCKS association. G0 will
replace it with an immutable per-association creation context. At minimum that
context contains:

- association id;
- `HttpNetworkSession*`;
- the selected all-QUIC `ProxyChain`;
- the exact `NetworkAnonymizationKey` stored by `PendingSocksHandshake`;
- `NetLogWithSource` and traffic annotation;
- connect and target-idle timeout inputs.

`NaiveProxy::HandleSocksRequestRead()` creates the backend with that context
after the relay is bound and before writing a successful UDP ASSOCIATE reply.
The M2 runner continues to inject a fake backend that ignores transport fields;
the no-backend runner continues to prove exact `0x01` rejection. The production
binary installs only the real backend factory.

### Target identity

`TargetKey` contains the SOCKS wire address type, canonical codec output for
host/address, and port. A domain and a numerically equivalent IP remain
different keys. Domains are sent to the proxy as domains and are not resolved
locally. Each entry retains the original validated SOCKS endpoint so return
datagrams preserve IPv4, IPv6, or domain framing.

### Target entry ownership

Each `TargetEntry` owns:

- a generation number and `connecting`, `open`, or `cooldown` state;
- one `NaiveConnectUdpTunnel`;
- pending outbound payloads and their retained `IOBuffer` storage;
- one continuously armed read buffer/pump;
- write-pump state;
- connect deadline, last activity, and retry-not-before timestamps;
- entry-scoped weak pointers, or callbacks carrying key plus generation.

Callbacks must never retain a raw map-entry pointer across an erase. Tunnel
retirement is posted out of connect/read/write callback stacks. Destroying the
association destroys the backend, entries, tunnels, sockets, and retained QUIC
session handles before the owning URL request context is destroyed.

## 4. G0 contract decisions

No production data-path work starts until these rules have deterministic tests
or compile-time assertions.

### Send completion and failure isolation

`Socks5UdpDatagramBackend::Send()` means admission into the backend's bounded
UDP work queue, not proof of remote delivery.

- Return `OK` after a datagram is accepted or intentionally dropped by a
  documented per-packet/per-target policy.
- Return `ERR_IO_PENDING` only if the admission decision itself is pending.
- Propagate a negative result to `Socks5UdpAssociation` only when the entire
  backend is unusable. M2 correctly terminates the association on such a fatal
  result.
- Oversize payload, target-cap pressure, queue pressure, connect failure, and
  one target's read/write failure are observable drops or target retirement;
  they do not terminate unrelated targets or the SOCKS association.
- Do not replay a datagram after an open tunnel reports a write/session error,
  because delivery state is ambiguous. After cooldown, only a new client
  datagram may create a fresh tunnel.

This keeps the existing backend interface narrow while explicitly preventing a
single remote target from closing the whole SOCKS control association.

### Read and write pumps

- Keep exactly one read armed on every open target tunnel. Chromium's current
  internal datagram queue is small, so reads are rearmed immediately.
- Cap synchronous pump work at 32 operations, then post a task to yield.
- Retain every `IOBuffer` until a synchronous or asynchronous callback
  completes. A positive write result must equal the payload length.
- Do not assume Chromium writes will always remain synchronous even though the
  current implementation normally is.
- Distinguish socket closure from a valid zero-length UDP datagram. G0/G1 must
  add the narrow tunnel/socket state query needed to resolve the current
  `Read() == 0` ambiguity; zero-length UDP must not be silently converted into
  EOF.
- Determine the safe CONNECT-UDP payload ceiling from the live QUIC stream and
  its quarter-stream-id/context-id overhead before calling the write path.
  Oversize payloads are dropped with `ERR_MSG_TOO_BIG` accounting instead of
  risking stream reset. Dynamic PMTU adaptation remains M6.

### Initial bounded-resource policy

G0 centralizes and tests the v1 constants before any real-server wiring. The
starting values are:

- 32 target entries per SOCKS association;
- 16 queued datagrams per target;
- 128 queued outbound datagrams and 256 KiB total queued payload per
  association;
- 32 synchronous pump operations before yielding;
- a 10-second target connect deadline;
- a 1-second failed-target cooldown tombstone;
- target idle eviction independent from M2's whole-association idle timeout;
- an explicit NaiveProxy-wide active UDP-association cap, frozen in G0 after
  checking the existing listener/concurrency semantics.

When a target cap is reached, first remove expired/cooldown-safe entries. If no
safe candidate exists, drop the new-target datagram; never evict a busy target.
Counters and rate-limited NetLog events record reason and count, not payload or
destination.

## 5. Sequential execution gates

Every gate is a small, independently revertible commit. Each gate must leave
the Release build, all earlier M1/M2 markers, all 56 TCP tests, and
`git diff --check` passing before the next gate begins.

### M3-G0 — contract, context, and test seam

Work:

1. Freeze the backend creation context, `TargetKey`, admission/error table,
   limits, timeout values, and no-replay rule in code comments and tests.
2. Pass the pending handshake's exact NAK into the backend factory without
   changing TCP construction.
3. Add a narrow fakeable target-tunnel adapter/factory. Production wraps the
   M1 tunnel; tests script connect/read/write sync, async, and failure outcomes.
   Do not virtualize Chromium session-pool internals.
4. Establish a deterministic backend test target and M3 script skeleton.
5. Preserve the production/fake/no-backend three-state behavior and verify the
   no-backend path still returns `0x01`.

Exit:

- context and same-NAK propagation have focused coverage;
- an empty backend skeleton compiles behind the new seam;
- M1/M2/TCP regressions remain green;
- marker: `M3_G0_BACKEND_CONTRACT_OK`.

Estimated effort: 1–2 person-days.

### M3-G1 — single-target backend state machine

Work:

1. Lazily create one target tunnel on the first admitted datagram.
2. Queue within limits while CONNECT-UDP is pending, then run serialized write
   and continuous read pumps after success.
3. Wrap each received payload in the target entry's original SOCKS endpoint.
4. Normalize socket byte-count results into the backend admission contract.
5. Implement safe payload-ceiling and zero-length-datagram behavior.
6. Cover synchronous reentrancy and asynchronous buffer ownership.

Deterministic tests include sync/async connect, read, and write; same-target
reuse; byte equality; pending destruction; short write; EOF; zero-length UDP;
oversize; and callback-triggered association destruction.

Exit marker: `M3_G1_SINGLE_TARGET_OK`.

Estimated effort: 2–3 person-days.

### M3-G2 — target routing, limits, and failure isolation

Work:

1. Add the target-keyed map and generation-safe callback routing.
2. Interleave IPv4, IPv6, and domain targets; reuse one tunnel for the same
   key and create distinct tunnels for distinct wire endpoints.
3. Enforce per-target, per-association, and active-association limits.
4. Add connect deadline, target idle eviction, cooldown tombstones, and lazy
   fresh-tunnel creation on a later datagram.
5. Isolate target connect/read/write/session failures. Drop ambiguous queued
   data without retransmission; keep other targets and the SOCKS association
   alive.

Exit:

- no cross-route response is possible;
- one target failure does not affect another target;
- all capacity and yield behavior is deterministic and observable;
- markers: `M3_G2_MULTI_TARGET_LIMITS_OK` and
  `M3_G2_FAILURE_ISOLATION_OK`.

Estimated effort: 2–4 person-days.

### M3-G3 — real M1 adapter and production composition

Work:

1. Implement the production adapter that creates `NaiveConnectUdpTunnel` with
   the backend context's session, chain, NAK, NetLog, annotation, and target.
2. Install the real factory in `naive_proxy_bin.cc`; retain the M2 fake runner
   and its explicit no-backend mode.
3. Keep both the SOCKS handshake eligibility check and a defensive all-QUIC
   backend invariant.
4. Verify the tunnel receives the same transient NAK used by the association.
   Separately verify that Chromium's proxy-wide `HttpAuthCache` supplies the
   cached credentials on this path; proxy credentials are not NAK-partitioned.
5. Audit declaration/destruction order so every proxy/backend/tunnel is
   destroyed before its URL request context/session.
6. Add a test-only full-path runner using `MockCertVerifier` for the local
   short-lived certificate. Production certificate verification is never
   weakened.

Exit:

- the production `naive` target links with the real backend;
- direct, H2, mixed-chain, and no-backend UDP still return exact `0x01`;
- TCP construction and behavior are unchanged;
- marker: `M3_G3_PRODUCTION_WIRING_OK`.

Estimated effort: 2–3 person-days.

### M3-G4 — controlled full-path interoperability

Run the real path against `naive_masque_server` and local UDP fixtures:

```text
SOCKS5 UDP client
  -> real local relay and RFC 1928 codec
  -> real M3 target backend
  -> real M1 Chromium tunnel
  -> controlled CONNECT-UDP endpoint
  -> UDP target and back
```

Coverage:

- IPv4, IPv6, and domain echo;
- a deterministic DNS UDP fixture;
- multiple targets in one association;
- multiple concurrent associations;
- correct cached Basic credentials with a transient NAK;
- server and NetLog evidence for Extended CONNECT plus H3 DATAGRAM;
- no UoT or custom reliable-stream framing.

Exit markers:

```text
M3_G4_IPV4_OK
M3_G4_IPV6_OK
M3_G4_DOMAIN_OK
M3_G4_DNS_OK
M3_G4_AUTH_OK
M3_G4_MULTI_TARGET_OK
```

Estimated effort: 2–3 person-days.

### M3-G5 — lifecycle, recovery, and observability

Coverage:

- control close during pending connect/read/write;
- backend destruction and URL request context shutdown;
- missing/wrong/correct cached proxy credentials and explicit 407 failure;
- server/session shutdown, cooldown, fresh-tunnel creation on a later packet,
  and proof that old payloads are not replayed;
- target connect timeout and idle eviction;
- target, packet, byte, active-association, and response-queue pressure;
- oversized and zero-length datagrams;
- repeated synchronous-pump yield and callback reentrancy;
- redacted, rate-limited counters/NetLog evidence.

Exit markers: `M3_G5_LIFECYCLE_OK`, `M3_G5_RECONNECT_OK`, and
`M3_G5_LIMITS_OK`.

Estimated effort: 2–3 person-days.

### M3-G6 — complete regression and independent audit

1. Run the complete M1 test matrix.
2. Run the complete M2 test matrix.
3. Run the single M3 integration entry point, planned as
   `tests/socks5_udp_m3.sh`.
4. Run all 56 TCP HTTP/HTTPS/auth/chain cases.
5. Run repeated lifecycle stress; use sanitizer coverage where practical.
6. Run `git diff --check` and inspect the final diff for accidental generated
   artifacts or TCP changes.
7. Keep one `agy` review session alive long enough to inspect the design,
   independently rerun the required matrix, and audit callback ownership,
   same-NAK propagation, proxy auth-cache behavior, limits, no-replay
   semantics, and H3 Datagram proof.

Exit:

- aggregate marker: `M3_NATIVE_UDP_CLIENT_OK`;
- `agy` result: `AUDIT_PASS` with no blocker, high, or medium finding;
- M3 status and evidence ledger updated before the implementation commit.

Estimated effort: 1–2 person-days.

## 6. Source and test change map

Expected production change surface:

- `net/tools/naive/socks5_udp_datagram_backend.h`: factory context and frozen
  admission semantics;
- new backend/target owner files under `net/tools/naive/`;
- `naive_connect_udp_tunnel.{h,cc}` only for narrow state/payload-limit access
  that cannot be obtained safely through `DatagramClientSocket`;
- `naive_proxy.{h,cc}`: pass association context, enforce the active
  association cap, and preserve the existing pre-`NaiveConnection` branch;
- `naive_proxy_bin.cc`: install the production backend and preserve context
  destruction order;
- `BUILD.gn`: focused backend tests and a controlled full-path runner.

Expected test change surface:

- scripted target-tunnel adapter tests;
- real `Socks5UdpAssociation` plus production backend tests;
- full SOCKS-to-controlled-MASQUE integration script;
- deterministic UDP echo and DNS fixtures;
- M1/M2/TCP regression aggregation and audit evidence.

Files explicitly outside M3 include `NaiveConnection`, TCP padding, Caddy,
`forwardproxy`, and any second QUIC implementation.

## 7. Risk register and stop conditions

| Risk | Required control | Stop condition |
| --- | --- | --- |
| Association NAK is lost or replaced | Context factory and focused NAK propagation test | Do not wire production backend until the same NAK is proven |
| Callback erases its own target/tunnel | Weak pointer plus key/generation and posted retirement | Redesign before accepting a callback-held raw entry pointer |
| Oversize write resets H3 stream | Preflight safe live payload ceiling | Do not ship a guessed constant as the only check |
| One target closes the SOCKS association | Freeze fatal versus target/drop error table | Do not proceed while ordinary target failures reach `Finish()` |
| Unbounded connect/queue storm | Target, packet, byte, association caps and cooldown | Do not run real-server stress without all caps tested |
| Datagram replay after ambiguous failure | Clear old queue and require a later new packet | Do not add transparent reliable retry |
| H3 Datagram is not actually negotiated | Controlled server settings plus NetLog/server evidence | A payload echo alone is insufficient evidence |
| Test certificate weakens production | Separate test runner with `MockCertVerifier` | Never add insecure production certificate configuration |
| Session outlives URL request context incorrectly | Explicit ownership/destruction test | Fix ordering before production wiring |

## 8. Effort and execution policy

The previous 6–10 person-day estimate assumed less lifecycle and resource
hardening. The gate estimates total 12–20 person-days, which is also the
planning budget for an audited implementation, or roughly 2–4 elapsed weeks
for one engineer plus an agent with Chromium builds and network tests in the
loop. A first real single-target path may appear earlier, but it is not M3
completion.

Work proceeds in high-confidence, green-to-green commits. If a gate exposes an
API boundary that requires modifying `QuicSessionPool`, replacing Chromium
QUIC, altering `NaiveConnection`, or adding a custom UDP protocol, stop and
revise the design rather than expanding M3 silently.
