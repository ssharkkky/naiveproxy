# NaiveProxy Native UDP Development Plan

Documentation entry point: [`README.md`](README.md). Current verified state:
[`native-udp-status.md`](native-udp-status.md). Active milestone plan:
[`m3-execution-plan.md`](m3-execution-plan.md).

## 1. Goal

Add native UDP proxying alongside the existing TCP proxy path while retaining
Chromium's network stack as the client transport implementation.

The first production target is:

```text
SOCKS5 UDP ASSOCIATE
        -> per-target UDP association
        -> RFC 9298 CONNECT-UDP
        -> HTTP/3 DATAGRAM
        -> QUIC upstream
```

The existing TCP path remains:

```text
SOCKS5 CONNECT -> NaiveConnection -> HTTP CONNECT over H2/H3
```

## 2. Frozen scope for v1

### Included

- Keep all existing TCP behavior and configuration compatible.
- Add SOCKS5 `UDP ASSOCIATE` on a SOCKS listener.
- Support UDP only when the selected upstream is `quic://`.
- Reject UDP ASSOCIATE during the SOCKS5 handshake with reply `0x01`
  (general SOCKS server failure) when the selected upstream is not `quic://`.
- Use Chromium QUIC, HTTP/3, and CONNECT-UDP implementations.
- Use one CONNECT-UDP association per destination host and port.
- Support IPv4, IPv6, and domain-name destinations.
- Tie UDP relay lifetime to the SOCKS TCP control connection.
- Add idle expiration and bounded resource limits for associations.
- Reject or drop SOCKS5 UDP fragments where `FRAG != 0` in v1.
- Preserve proxy authentication and existing `NaiveProxyDelegate` behavior.
- Add end-to-end tests for DNS, generic UDP echo, and HTTP/3 traffic.

### Excluded

- Custom UDP-over-stream or UDP-over-TCP encapsulation.
- UDP over `https://` / HTTP/2.
- TUN, CONNECT-IP, VPN mode, WebRTC-specific multi-peer extensions, or
  CONNECT-UDP-LISTEN.
- SOCKS5 UDP fragmentation/reassembly.
- Refactoring `NaiveConnection` or changing the existing TCP padding format.
- FluxFabric traffic classification or behavior modeling.
- UDP payload padding in v1. HTTP/3 DATAGRAM does not provide the Naive TCP
  payload-padding protocol; traffic-shape measurement is required before a
  separate datagram-padding design is considered.

## 3. Repository baseline

- Development fork: `ssharkkky/naiveproxy` (`origin`).
- Reference implementation: `klzgrad/naiveproxy` (`upstream`).
- Baseline tag: `v150.0.7871.63-1`.
- Development branch: `codex/native-udp-foundation`.
- Chromium version: `150.0.7871.63`.

Development must follow stable NaiveProxy tags. Do not merge or continuously
track `master`, because upstream replaces its root commit for new Chromium
releases.

## 4. M0 source findings (historical baseline)

These findings drove M1 and M2. Completion and any later corrections are
recorded in the status ledger and active milestone plan.

1. `NaiveProxy::DoConnect()` accepts a TCP socket, wraps SOCKS listeners in
   `Socks5ServerSocket`, and always constructs `NaiveConnection`. UDP needs a
   command-aware interception point before `NaiveConnection` construction; it
   must not be forced into `NaiveConnection`.
2. `Socks5ServerSocket` parses `UDP ASSOCIATE` today but returns
   `kReplyCommandNotSupported`. It is a `StreamSocket`, so converting it into a
   combined stream/datagram abstraction would create unnecessary TCP risk.
3. The vendored Chromium 150 tree already contains
   `QuicProxyDatagramClientSocket`. It generates `connect-udp`, registers an
   HTTP/3 Datagram visitor, and exposes datagram `Read()` / `Write()` behavior.
4. The available Chromium integration is not yet a public, high-level
   "open arbitrary UDP tunnel" API for NaiveProxy. Its current construction is
   embedded in `QuicSessionPool::CreateSessionOnProxyStream()` for creating an
   inner QUIC session through MASQUE. The existing
   `HttpProxyConnectJob::DoQuicProxyCreateSession()` ->
   `DoQuicProxyCreateStream()` flow demonstrates that `QuicSessionRequest` and
   `RequestStream()` are the preferred acquisition path; M1 should adapt that
   path rather than duplicate `QuicSessionPool` internals.
5. Server support is a separate workstream. The Naive fork of
   `forwardproxy`, Caddy's HTTP/3 Datagram enablement, and UDP egress lifecycle
   must be verified and changed outside this repository.
6. The default RFC 9298 request URL was constructed inline in
   `CreateSessionOnProxyStream()`. Its intended escaped-host variable was both
   unused and based on the proxy host rather than the target host. The first M1
   step moves URL construction beside `QuicProxyDatagramClientSocket`, encodes
   the target host, and leaves stream/session acquisition as the next spike.

## 5. Proposed client architecture

```text
TCP accept loop
    |
    +-- HTTP/redir --------------------------> existing NaiveConnection
    |
    +-- SOCKS5 request reader
            |
            +-- CONNECT
            |     -> write existing success response
            |     -> existing NaiveConnection
            |
            +-- UDP ASSOCIATE
                  |
                  +-- upstream is not quic://
                  |     -> reply 0x01 and close
                  |
                  +-- upstream is quic://
                        -> bind local UDP relay first
                        -> write success response containing the relay's
                           actual BND.ADDR and BND.PORT
                        -> complete SOCKS handshake
                        -> branch in NaiveProxy before NaiveConnection
                        -> Socks5UdpAssociation
                              - TCP control-channel lifetime
                              - local UDP relay socket
                              - RFC 1928 packet parsing
                              - destination-to-tunnel map
                              |
                              +-- NaiveConnectUdpTunnel (one per target)
                                    - acquire H3 request stream
                                    - Chromium CONNECT-UDP socket
                                    - datagram read/write pumps
                                    - idle/error/MTU accounting
```

The SOCKS5 handshake therefore needs a two-phase boundary: parse and expose the
command before writing the command response, then report handshake completion
to `NaiveProxy`. `NaiveProxy` performs the final command switch after the
handshake and before constructing `NaiveConnection`. For UDP, the relay must be
bound during the pre-response phase so the handshake can return a compliant
`BND.ADDR` and `BND.PORT`. TCP data movement remains owned by the current
`Socks5ServerSocket` and `NaiveConnection` classes.

## 6. Implementation milestones

### M0 — Baseline and guardrails (complete)

- Configure `origin` and `upstream`.
- Check out a stable-tag-based feature branch.
- Record scope and non-goals.
- Establish the rule that every milestone runs the existing TCP test suite.

Exit criterion: clean working tree on the frozen tag plus this plan.

### M1 — Chromium integration spike (5–10 person-days)

- Trace how the existing `quic://` proxy session is created and retained.
- Prototype obtaining a request stream for an arbitrary CONNECT-UDP target.
- Decide whether to:
  - expose a narrow factory from `QuicSessionPool`, or
  - add a Naive-owned adapter using existing public session/request objects.
- Prove `QuicProxyDatagramClientSocket` can authenticate through
  `NaiveProxyDelegate` without bypassing proxy headers.
- Build a client-only test against a controlled CONNECT-UDP test server.

Exit criterion: one UDP echo datagram traverses Chromium H3 Datagram APIs, and
the API boundary is documented. Stop and redesign if this requires duplicating
QUIC or HTTP/3 internals.

Progress:

- [x] Trace the existing proxy-session and request-stream acquisition paths.
- [x] Extract and compile-verify the reusable RFC 9298 request-URL boundary.
- [x] Confirm the minimized source checkout does not contain the normal
  Chromium `net_unittests` target; URL-specific tests must be added when a
  focused test target is restored or introduced.
- [x] Build and compile-verify a Naive-owned adapter around
  `QuicSessionRequest` / `RequestStream`. It owns cancellation-safe async state,
  retains the QUIC session handle, exposes the stream and endpoint metadata,
  and rejects unsupported proxy-chain shapes without affecting existing paths.
- [x] Add and compile-verify `NaiveConnectUdpTunnel`, which passes the acquired
  stream, endpoint metadata, and user agent into
  `QuicProxyDatagramClientSocket::ConnectViaStream()` without wiring an ingress.
- [x] Add preemptive proxy authentication to
  `QuicProxyDatagramClientSocket`. The Naive tunnel constructs an
  `HttpAuthController` from the existing session cache, handler factory, and
  resolver; the socket now mirrors Chromium's QUIC stream proxy sequence of
  token generation, authorization-header generation, and delegate headers.
  The generic nested-QUIC caller remains compatible by passing no controller.
- [x] Compile the complete Release `naive` target and run the existing TCP
  HTTP/HTTPS, authentication, and proxy-chain regression suite after the auth
  change. An independent continuing-session review found no blocker for
  Naive's cached Basic credentials.
- [x] Decide and document post-407 behavior. M1 does not require interactive
  407 restart because Naive preloads configured credentials, but a 407 must
  fail explicitly until a fresh-stream retry loop is implemented.
- [x] Complete an authenticated UDP echo round trip against a controlled
  CONNECT-UDP endpoint.

#### M1 execution goals

These goals are deliberately sequential. Each one must leave the Release build
and existing TCP suite passing before the next begins.

1. **G1 — Controlled endpoint selection and smoke test (complete).** Select the
   smallest maintainable HTTP/3 CONNECT-UDP test server already compatible with
   the vendored QUIC versions, pin its build/runtime inputs, and prove that it
   can start locally with HTTP/3 Datagram enabled.

   Exit: the server accepts an independent RFC 9298 probe and its logs expose
   the Extended CONNECT headers and Datagram traffic.

   Result: the endpoint and probe reuse the vendored QUICHE revision through
   the `naive_masque_server` and `naive_masque_probe` GN targets. The endpoint
   binds IPv6 wildcard with a strict `[::1]:port` authority, uses a short-lived
   local certificate, logs redacted Extended CONNECT metadata, and relays to a
   local UDP echo fixture. `tests/masque_g1_smoke.sh` independently verified
   `CONNECT` + `connect-udp`, the RFC 9298 path, UDP egress, and an identical
   19-byte echo response. It ended with `G1_MASQUE_SMOKE_OK`.

2. **G2 — Dormant client spike runner (complete).** Add a test-only executable or focused
   harness that creates the existing `HttpNetworkSession`, invokes
   `NaiveConnectUdpTunnel` directly, and can write/read one datagram. Do not
   route this through SOCKS5 or `NaiveProxy::DoConnect()` yet.

   Exit: the harness deterministically reports stream acquisition, CONNECT-UDP
   response status, and socket read/write results without altering production
   ingress behavior.

3. **G3 — Authenticated CONNECT-UDP handshake (complete).** Configure the controlled
   endpoint to require the same Basic credentials that Naive preloads into
   `HttpAuthCache`; verify the first request carries `Proxy-Authorization` and
   receives success without a 407 round trip. Add a wrong/missing-credential
   failure assertion and record that interactive 407 restart is deferred.

   Exit: authenticated success and explicit auth failure are repeatable, with
   header evidence redacted in logs.

4. **G4 — Bidirectional UDP echo proof (complete).** Put a local UDP echo target behind
   the controlled CONNECT-UDP endpoint. Send a uniquely identified payload via
   `DatagramClientSocket::Write()`, receive it through `Read()`, and verify byte
   equality, target selection, and callback completion in both synchronous and
   asynchronous cases encountered by the runtime.

   Exit: at least one authenticated datagram completes the full client -> H3
   Datagram -> UDP echo -> H3 Datagram -> client round trip.

5. **G5 — M1 lifecycle and evidence closeout (complete).** Exercise cancellation while a
   stream request is pending, tunnel destruction after connection, server-side
   rejection, and session shutdown. Capture NetLog/server evidence that the
   payload used HTTP/3 DATAGRAM rather than reliable stream framing, document
   the final Chromium API boundary, and remove any disposable spike-only code.

  Exit: the M1 echo criterion is reproducible from written commands, no known
  callback/lifetime blocker remains, `git diff --check` passes, the Release
  target builds, and the full existing TCP suite remains green.

   Result: all G1-G5 markers, 56 existing TCP cases, and structured NetLog
   checks pass. A continuing-session independent `agy` audit inspected and
   reran the work, then returned `AUDIT_PASS` with no blocking, high, or medium
   findings. See `docs/m1-agy-audit.md`.

### M2 — SOCKS5 UDP ingress (complete and independently audited)

- Split SOCKS request parsing from response writing so the command is available
  to `NaiveProxy` before the command response is sent.
- Add a SOCKS-specific state/callback in `NaiveProxy::DoConnect()` flow. After
  request parsing, choose CONNECT or UDP ASSOCIATE; after successful handshake,
  branch again before `NaiveConnection` construction.
- For CONNECT, pass the already-handshaken `Socks5ServerSocket` into the
  existing `NaiveConnection` path without changing its data movement.
- For UDP ASSOCIATE with a non-`quic://` upstream, send SOCKS5 reply `0x01` and
  close the control connection. Do not accept and silently drop later packets.
- For an eligible UDP ASSOCIATE, bind the local UDP relay before sending
  success and return its actual reachable address and port as `BND.ADDR` and
  `BND.PORT`, rather than the current `0.0.0.0:0` CONNECT response.
- Add RFC 1928 UDP request/response parsing for IPv4, IPv6, and domains.
- Enforce client endpoint ownership and control-channel lifetime.
- Drop `FRAG != 0` with metrics/logging.
- Add parser and lifecycle unit tests.
- Establish the UDP test skeleton in `tests/` now: local UDP echo fixture,
  SOCKS5 UDP packet helpers, fake datagram backend, and TCP-regression hook.

Exit criterion: local SOCKS UDP packets reach a fake datagram backend; TCP
SOCKS tests remain unchanged and passing. Tests verify the returned relay
address, non-QUIC `0x01` rejection, control-channel closure, and `FRAG != 0`.

### M3 — Native UDP client data path (complete and independently audited)

M3 is the composition layer between M2's verified SOCKS ingress and M1's
verified Chromium CONNECT-UDP tunnel. The detailed, gate-by-gate execution
plan is in `docs/m3-execution-plan.md`.

- G0 freezes the per-association backend context, including the exact transient
  `NetworkAnonymizationKey`, target identity, error/admission semantics,
  limits, no-replay behavior, and a narrow fakeable tunnel seam.
- G1 implements a cancellation-safe single-target connect/read/write backend.
- G2 adds target-keyed routing, queue/byte/association bounds, idle eviction,
  cooldown, and per-target failure isolation.
- G3 composes the real M1 tunnel into `naive_proxy_bin` while preserving fake
  and no-backend test modes, all-QUIC rejection, certificate verification, and
  URL request context lifetime.
- G4 proves IPv4, IPv6, domain, DNS, authentication, multiple targets, and
  concurrent associations against the controlled `naive_masque_server`.
- G5 closes lifecycle, recovery, oversize, resource pressure, and redacted
  observability gaps.
- G6 reruns M1, M2, M3, and all 56 TCP regressions and requires an independent
  continuing-session `agy` result of `AUDIT_PASS`.

Exit criterion: DNS and UDP echo work through the complete SOCKS5 client path
against the controlled compliant endpoint via RFC 9298 CONNECT-UDP and HTTP/3
DATAGRAM, without custom stream encapsulation. M4 remains responsible for the
production Caddy/`forwardproxy` server.

### M4 — Server data path (7–12 person-days, separate repository)

- Fork/pin the Naive `forwardproxy` server dependency.
- Recognize Extended CONNECT with `:protocol = connect-udp`.
- Parse and validate the RFC 9298 URI template target.
- Apply existing auth, ACL, probe-resistance, and logging policies.
- Relay H3 Datagrams to a connected UDP socket and back.
- Enable HTTP/3 Datagram support in the Caddy/quic-go server configuration.
- Add timeouts, per-client limits, IPv4/IPv6 behavior, and clean shutdown.

Exit criterion: a standalone RFC 9298 interoperability test passes in both
directions, and traditional TCP CONNECT behavior is unchanged.

### M5 — End-to-end MVP (5–8 person-days)

- Test SOCKS5 UDP through the Naive client and Caddy server.
- Cover DNS, UDP echo, HTTP/3 through the proxy, IPv4, IPv6, and domains.
- Test multiple destinations over one SOCKS UDP association.
- Test auth failure, server restart, QUIC reconnect, idle expiration, and
  malformed packets.
- Record packet captures demonstrating HTTP/3 DATAGRAM rather than UDP-over-
  stream framing.
- Record packet-size and timing observations for the explicit v1 decision to
  ship without a Naive-specific UDP padding layer.

Exit criterion: the MVP verification matrix passes and existing TCP tests show
no regression.

### M6 — Hardening and release candidate (10–20 person-days)

- Establish a safe inner payload ceiling and validate PMTU behavior.
- Run loss, reordering, latency, and constrained-bandwidth tests.
- Stress association count, queue pressure, and long-running lifecycle.
- Run sanitizers/fuzzers on SOCKS packet parsing and async lifecycle tests.
- Validate macOS, Linux, Windows, and Android as separately tracked targets.
- Document configuration, compatibility, observability, and rollback.

Exit criterion: release checklist passes, no known high-severity lifecycle or
memory-safety issue remains, and maintainers approve the Chromium API boundary.

## 7. Verification matrix

| Area | Minimum verification |
| --- | --- |
| TCP regression | Existing `tests/basic.sh` on every milestone |
| SOCKS protocol | CONNECT unchanged; UDP ASSOCIATE success/error paths |
| SOCKS response | Reachable UDP BND.ADDR/BND.PORT; non-QUIC reply `0x01` |
| Addressing | IPv4, IPv6, domain names, invalid address types |
| UDP payload | DNS, echo, HTTP/3/QUIC, payload limit boundaries |
| Multiplexing | Multiple destinations and concurrent associations |
| Lifecycle | TCP control close, idle timeout, upstream close/reconnect |
| Security | Auth, ACL, spoofed local sender, resource exhaustion limits |
| Network | Loss, reordering, PMTU/oversize, IPv4/IPv6 path changes |
| Interop | Independent RFC 9298 client/server where practical |

## 8. Engineering rules

- Never mix UDP feature work with opportunistic TCP refactors.
- Land small commits by boundary: parser, association, Chromium adapter,
  server, tests, hardening.
- Keep Chromium changes narrow and documented for the next stable-tag rebase.
- Prefer an adapter/factory over copying internal QUIC session-pool logic.
- No fallback to a custom tunnel protocol if CONNECT-UDP is unavailable.
- Keep the v1 no-UDP-padding decision explicit. Do not reuse the reliable-stream
  TCP padding format for unreliable datagrams without a separate design and
  measurement review.
- Any packet drop caused by queue or size limits must be observable.
- Do not log destinations or payloads by default.
- Maintain a patch inventory across NaiveProxy, forwardproxy, and Caddy.

## 9. Estimated effort and decision gate

The original pre-M1 estimate was 38–68 person-days for a maintainable release
candidate, excluding long cross-platform soak time. M1 retired the dominant
Chromium request-stream uncertainty, and M2 completed the local SOCKS ingress.
Those completed milestones are now tracked by evidence and commits rather than
future estimates.

Current remaining planning ranges:

| Milestone | Range | Current decision gate |
| --- | ---: | --- |
| M3 client composition | 12–20 person-days | Execute G0–G6 in `m3-execution-plan.md` |
| M4 production server | 7–12 person-days | Keep Caddy/`forwardproxy` separate from the client fixture |
| M5 end-to-end MVP | 5–8 person-days | Begin after production client and server paths exist |
| M6 hardening/release | 10–20 person-days | Begin after the MVP matrix passes |

The remaining range is therefore approximately 34–60 person-days, excluding
long cross-platform soak time. Re-estimate after M3 because target-entry
ownership, failure isolation, and resource pressure are now the main client
uncertainties. If M3 requires modifying `QuicSessionPool`, replacing Chromium
QUIC, altering `NaiveConnection`, or adding a private UDP protocol, stop and
revise scope before continuing.

## 10. Current status and next actions

The operational source of truth is `native-udp-status.md`; this checklist is a
roadmap snapshot and should not duplicate per-gate evidence.

Completed:

- [x] Configure the development fork and upstream remotes.
- [x] Create a stable-tag-based development branch.
- [x] Inspect the SOCKS, `NaiveProxy`, and Chromium CONNECT-UDP source paths.
- [x] Freeze v1 scope, milestones, verification matrix, and decision gates.

Development environment and M1 foundation:

- [x] Download the Chromium-matched Clang, GN, and PGO artifacts with the
  existing project script.
- [x] Build unmodified `v150.0.7871.63-1` for macOS arm64.
- [x] Run the existing TCP tests and establish a passing baseline.
- [x] Start M1 with a narrow Chromium CONNECT-UDP URL-construction seam; do not
  begin with SOCKS parsing.
- [x] Rebuild and rerun the existing TCP suite after the first M1 change.
- [x] Implement and compile-verify the `QuicSessionRequest` / `RequestStream`
  adapter without wiring it into existing connection paths.
- [x] Implement and compile-verify the client-side `NaiveConnectUdpTunnel`
  composition around `QuicProxyDatagramClientSocket::ConnectViaStream()`.
- [x] Add `HttpAuthController` proxy-auth generation to the CONNECT-UDP socket
  for Naive's preloaded credentials. Interactive 407 restart remains an
  explicit deferred behavior requiring a fresh QUIC stream.
- [x] Build and start the controlled vendored-QUICHE HTTP/3 CONNECT-UDP
  endpoint, then prove its Extended CONNECT and bidirectional H3 Datagram path
  with an independent probe and local UDP echo fixture.
- [x] Add the G2 test-only runner that invokes `NaiveConnectUdpTunnel` through
  a real `HttpNetworkSession`, without SOCKS ingress.
- [x] Runtime-prove the complete authenticated tunnel against a controlled
  CONNECT-UDP endpoint.
- [x] Keep production server work deferred to M4. M1 identified the required
  behavior with a same-repository, vendored-QUICHE endpoint, so no sibling
  repository is needed for the client spike.

M2 SOCKS5 UDP ingress:

- [x] Add the RFC 1928 IPv4/IPv6/domain codec and table-driven boundary tests.
- [x] Split SOCKS5 request parsing from reply writing without changing the TCP
  data mover.
- [x] Branch CONNECT/BIND/UDP ASSOCIATE before `NaiveConnection` construction.
- [x] Reject non-QUIC or unavailable UDP backends with reply `0x01`.
- [x] Bind the real relay before success and return its actual IPv4/IPv6 BND
  endpoint.
- [x] Add source authorization, wildcard-port learning, bounded queues,
  control-channel lifetime coupling, and synchronous-I/O reentrancy guards.
- [x] Pass deterministic codec, handshake and association tests, real-loopback
  IPv4/IPv6/auth tests, and all 56 existing TCP regressions.
- [x] Complete an independent continuing-session `agy` audit with
  `AUDIT_PASS`; see `docs/m2-agy-audit.md`.

Next:

- [x] Freeze the M3 execution sequence, ownership boundaries, risks, and exit
  evidence in `docs/m3-execution-plan.md`.
- [x] Execute M3-G0 through G5: compose the exact association context/NAK with
  the production Chromium tunnel, then verify routing, limits,
  interoperability, lifecycle, recovery, and redacted observability.
- [x] Complete M3-G6 full regressions, three consecutive lifecycle stress
  runs, and independent `agy` review with `AUDIT_PASS`; see
  `docs/m3-agy-audit.md`.
- [ ] Plan and execute M4 in the separate production Caddy/`forwardproxy`
  server dependency without changing the completed M3 client boundary.
