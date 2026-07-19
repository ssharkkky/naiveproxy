# Native UDP M4 Production Server Execution Plan

Last updated: 2026-07-19 (Asia/Shanghai)

Status: M4-G0 through M4-G5 complete; M4-G6 is the next implementation gate.

Documentation entry point: [`README.md`](README.md). Verified client state:
[`native-udp-status.md`](native-udp-status.md). Project scope and roadmap:
[`native-udp-development-plan.md`](native-udp-development-plan.md).

## 1. Purpose and milestone boundary

M4 adds the production server half of NaiveProxy native UDP. M1 through M3
already provide an audited Chromium-driven client path. M4 must accept that
client's standard RFC 9298 request on the Naive Caddy/`forwardproxy` server and
relay HTTP/3 Datagrams to a fixed UDP target without changing traditional TCP
CONNECT behavior.

```text
independent RFC 9298 client                 M3 Naive client (M5)
             |                                      |
             +---------- HTTP/3 CONNECT-UDP --------+
                                    |
                         patched production Caddy
                                    |
                      forwardproxy auth and policy
                                    |
                       one connected UDP socket
                                    |
                              fixed target
```

M4 is a separate server-repository workstream. Production changes belong in a
pinned fork/worktree of `klzgrad/forwardproxy` plus the smallest required Caddy
patch. They must not be copied into this NaiveProxy Chromium source tree. This
repository retains the status ledger, cross-project contracts, and the frozen
M3 client interoperability oracle.

M4 completion means the production server passes an independent RFC 9298
interoperability and regression matrix. Full SOCKS5-to-Naive-client-to-Caddy
product validation remains M5.

## 2. Verified source baseline

The plan is based on read-only inspection of these exact upstream snapshots:

| Component | Inspected snapshot | Relevant fact |
| --- | --- | --- |
| Naive `forwardproxy` | branch `naive`, commit `d62c80d3dd2c706b6b87579844d2397bddd18317` (the inspected checkout labels this commit with the current Naive tags, including `v2.11.2-naive`) | H2/H3 CONNECT rejects any non-empty `:scheme` or `:path`; the existing policy/dial helper permits only TCP |
| Caddy | `v2.11.2`, commit `ffb6ab0644f24c5ee6542aca6bd59b7a1b0a8f91` | `http3.Server` is constructed without `EnableDatagrams: true` |
| quic-go | `v0.59.0`, commit `7659dd8e0fa06b41290ad29af323d93d673c6b36` | `http3.HTTPStreamer` exposes `HTTPStream()`, and the stream exposes `SendDatagram` / `ReceiveDatagram` |

These facts are source evidence, not runtime interoperability proof. In
particular, the current `forwardproxy/go.mod` still declares Caddy `v2.8.4`,
quic-go `v0.44.0`, Go 1.21/toolchain 1.22.2, while its release workflow uses
`xcaddy@latest`. Caddy `v2.11.2` instead requires Go 1.25 and quic-go `v0.59.0`.
M4-G0 must therefore record one exact, reproducible build tuple before code is
written.

The inspected quic-go maps an incoming Extended CONNECT `:protocol` value to
`http.Request.Proto` while retaining `ProtoMajor == 3`. That is a likely
dispatch seam, but Caddy middleware wrapping and stream takeover remain
runtime decision gates in M4-G1; production code must not assume the seam is
preserved until the spike proves it.

## 3. Frozen M4 scope

### Included

- HTTP/3 Extended CONNECT with `:protocol = connect-udp` only.
- RFC 9298 URI template parsing for IPv4, IPv6, and domain targets.
- HTTP/3 Datagrams with RFC 9298 Context ID `0`.
- One fixed target and one connected UDP socket per CONNECT-UDP stream.
- Existing Basic authentication, probe resistance, ACL, allowed-port, and
  normal-site routing semantics.
- Bounded associations, queues/bytes, idle lifetime, and clean cancellation.
- Exact reproducible server build inputs and server-side regression tests.
- Redacted, rate-limited observability that never records the requested
  destination, request path, or UDP payload.

### Excluded

- HTTP/2 Capsules or any UDP-over-stream fallback.
- UoT, private framing, a second QUIC implementation, CONNECT-IP,
  CONNECT-UDP-LISTEN, or multi-peer/listen semantics.
- UDP payload padding. M4 preserves the explicit v1 no-padding decision.
- Transparent datagram replay after any ambiguous write or session failure.
- Refactoring or behavior changes to the existing TCP CONNECT/padding path.
- Shipping the controlled `naive_masque_server` fixture as production code.
- Full Naive SOCKS5 client-to-Caddy MVP claims; those belong to M5.

If implementation would require any excluded mechanism, stop and revise the
project plan rather than silently expanding M4.

## 4. Server contracts to freeze in G0

### Request classification

The new branch executes only when all of the following are true:

- method is `CONNECT`;
- HTTP major version is 3;
- the Extended CONNECT protocol is exactly `connect-udp`;
- the request has the RFC 9298 `https` scheme, authority, and strict default
  URI-template path;
- HTTP/3 Datagrams were negotiated and the underlying stream is available.

Legacy CONNECT must continue into the existing TCP branch byte-for-byte. Other
Extended CONNECT protocols must not be mistaken for normal TCP CONNECT or
CONNECT-UDP.

### Target and URI-template parsing

Accept only this default template:

```text
/.well-known/masque/udp/{target_host}/{target_port}/
```

The parser must:

- perform exactly one URI-template percent-decoding step;
- accept IPv4, domain names, and percent-encoded IPv6 such as `%3A%3A1`;
- validate a decimal port in the inclusive range 1–65535;
- reject a query, fragment, missing or extra segment, invalid escape,
  encoded slash/backslash ambiguity, empty host, zone identifier, userinfo,
  or authority-style brackets in the template variable;
- keep parsing errors free of destination/path logging.

Domain resolution and each candidate IP must pass the same authorization
policy used by TCP. Policy/lookup must be factored from TCP dialing rather
than weakened or duplicated. An upstream-chained `forwardproxy` configuration
is unsupported for native UDP v1 unless G0 proves a compatible datagram dial
contract; otherwise reject CONNECT-UDP before sending success.

### RFC 9298 Datagram framing

quic-go's stream API owns HTTP/3 quarter-stream-id framing. `forwardproxy`
owns the CONNECT-UDP application payload:

```text
client H3 Datagram:  Context ID varint (must be 0) || UDP payload
UDP egress:                                             UDP payload
server H3 Datagram:  Context ID varint 0            || UDP payload
```

- Decode a canonical QUIC variable-length integer and accept only Context ID
  `0` in v1.
- Treat a context-only application payload as a valid zero-length UDP
  datagram, not EOF.
- Drop malformed or unsupported contexts deterministically and account for
  them without logging destination or payload.
- Enforce the live datagram ceiling before adding the Context ID on return.
- Never retry or replay an ambiguous datagram send.

### HTTP result mapping

All failures that can be known before tunnel establishment must be returned
before the final success response. G0 freezes focused tests for at least:

| Condition | Response |
| --- | ---: |
| Missing or wrong credentials | `407` |
| Malformed template, target, port, or context setup | `400` |
| ACL or allowed-port denial | `403` |
| Unsupported HTTP version/protocol/datagram capability/upstream mode | `501` |
| DNS or UDP socket establishment failure | `502` |

Resource admission must also receive one documented pre-success response
(`429` or `503`) in G0. After a `200`, stream, socket, timeout, or datagram
errors terminate only that association and are exposed through redacted
counters; they cannot be converted into a second HTTP response.

### Ownership, bounds, and privacy

- One handler-owned association controls its request context, H3 stream,
  connected UDP socket, pumps, buffers, and timers.
- Cancellation of the request/stream or Caddy shutdown cancels both pumps and
  closes the UDP socket exactly once.
- G0 freezes per-client and global association caps, queue/byte caps, maximum
  accepted payload, idle timeout, DNS timeout, and synchronous-work/yield
  behavior before a load test is run.
- Queue pressure and oversize datagrams are bounded observable drops; they do
  not create unbounded goroutines or memory.
- Logs and metrics may contain a random/redacted association id, state,
  non-sensitive reason, byte count, and powers-of-two cumulative counters.
  They must not contain target host/IP/port, URI path, payload, credentials,
  or raw headers.

## 5. Sequential execution gates

Each gate is a narrow green-to-green commit in the server implementation
repository, followed by a factual status-ledger update here. Do not begin a
later gate before the current exit criteria pass.

### M4-G0 — repository, build tuple, contracts, and test skeleton

Status: complete in server commit `bf092e6`.

Work:

1. Create or identify the maintained `forwardproxy`/Caddy fork and record its
   remote, branch, clean baseline, and patch ownership in this ledger.
2. Pin exact `forwardproxy`, Caddy, quic-go, Go, and xcaddy versions/commits.
   Remove `@latest` and branch-floating build inputs from the M4 workflow.
3. Produce a clean server binary twice from the pinned tuple and record the
   build metadata/checksums needed to diagnose dependency drift.
4. Freeze the request/result table, URI parser contract, Context ID rules,
   policy factoring, no-replay rule, ownership, limits, privacy, and the
   unsupported-upstream decision in tests/comments.
5. Establish focused Go tests, an independent RFC 9298 test client skeleton,
   local UDP echo/DNS fixtures, and legacy TCP/probe-resistance regression
   entry points.

Exit:

- the server fork and complete version tuple are reproducible and documented;
- baseline Caddy/`forwardproxy` tests and normal TCP CONNECT pass;
- test skeleton marker: `M4_G0_SERVER_BASELINE_OK`;
- no client or TCP data-path source is changed.

Estimated effort: 1–3 person-days.

### M4-G1 — Caddy/quic-go capability spike

Status: complete in forwardproxy commit `121f097` and Caddy commits
`2002a520` / `2ff83e69`.

Work:

1. Add the smallest reviewed Caddy patch or supported hook that creates the
   HTTP/3 server with Datagrams enabled. Do not add an unrelated Caddy fork
   delta.
2. Send a real Extended CONNECT through the complete Caddy route/middleware
   chain and prove exactly where `connect-udp` is exposed to `forwardproxy`.
3. Prove a wrapped `http.ResponseWriter` can expose or safely unwrap
   `http3.HTTPStreamer`, and document stream ownership/close behavior.
4. Run a minimal direct H3 Datagram echo through that stream, including
   negotiated SETTINGS evidence, a zero-length application payload, stream
   close, and server shutdown.
5. Keep this spike free of target parsing, ACL, and production UDP relay logic.

Exit:

- real Caddy proves Extended CONNECT protocol visibility;
- the handler can take over exactly the corresponding H3 stream;
- negotiated bidirectional H3 Datagrams pass without DATA/Capsule framing;
- lifecycle closes without a goroutine or stream leak;
- marker: `M4_G1_CADDY_H3_DATAGRAM_OK`.

Stop condition: if Caddy middleware hides the protocol or stream, or enabling
Datagrams requires an unstable broad patch, stop and redesign the integration
boundary before G2.

Estimated effort: 2–4 person-days.

### M4-G2 — strict protocol, target, and policy layer

Status: complete in forwardproxy commit `f9b40f6`.

Work:

1. Add a separate CONNECT-UDP dispatch before the legacy TCP CONNECT branch.
   Preserve existing authentication, host routing, probe resistance, and
   passthrough processing that occurs before CONNECT dispatch.
2. Implement the strict default URI-template parser and table-driven IPv4,
   IPv6, domain, port, escaping, query, and malformed-input tests.
3. Implement Context ID `0` encode/decode with malformed, nonzero, and empty
   payload coverage.
4. Extract reusable target authorization/resolution from the TCP-only dial
   helper. Apply allowed-port, domain rules, per-resolved-IP ACL decisions,
   DNS timeout, and rebinding-safe selected-address handling consistently.
5. Freeze and test the pre-success HTTP status mapping without exposing
   destinations in returned/logged errors.

Exit:

- only valid H3 `connect-udp` requests reach the UDP association factory;
- denied/malformed/unsupported/auth cases return the exact frozen statuses;
- legacy TCP CONNECT tests remain unchanged and green;
- marker: `M4_G2_PROTOCOL_POLICY_OK`.

Estimated effort: 2–3 person-days.

### M4-G3 — bounded UDP association and pumps

Status: complete in forwardproxy commit `1b6d04b`.

Work:

1. Open one connected UDP socket to the policy-approved fixed address before
   returning `200`; do not re-resolve or switch to an unapproved address.
2. Implement H3-Datagram-to-UDP and UDP-to-H3-Datagram pumps with Context ID
   framing, buffer ownership, cancellation, and exact byte accounting.
3. Preserve zero-length datagrams, enforce live MTU/oversize behavior, and
   never replay after an ambiguous send/session failure.
4. Implement the frozen association/queue/byte caps, idle timeout, resource
   admission, fair yielding, and clean request/server shutdown.
5. Add deterministic fake-stream/socket tests plus real IPv4 and IPv6 UDP
   echo tests; use the race detector and leak checks.

Exit:

- bidirectional fixed-target UDP works with bounded resources;
- cancellation, idle expiry, pressure, empty/oversize, and failures terminate
  only the affected association;
- no goroutine/socket/buffer leak or datagram replay is observed;
- marker: `M4_G3_UDP_ASSOCIATION_OK`.

Estimated effort: 2–4 person-days.

### M4-G4 — forwardproxy/Caddy production integration

Status: complete in forwardproxy commit `15c07ab`.

Work:

1. Wire the G2/G3 handler after existing auth/probe-resistance routing and
   before the legacy TCP CONNECT branch.
2. Verify correct, wrong, and missing Basic credentials; allowed and denied
   ports/hosts/IPs; domain resolution; IPv4/IPv6; resource admission; and each
   frozen HTTP result.
3. Verify normal websites, PAC behavior, hidden/probe-resistant routes, and
   active-probing behavior remain indistinguishable from the baseline.
4. Add only redacted counters and structured state/error events. Audit all
   Caddy/forwardproxy error paths so neither request path nor destination is
   emitted at debug level.
5. Verify ordinary H1/H2/H3 TCP CONNECT and Naive TCP padding remain on their
   original path with no behavior or performance regression.

Exit:

- production routing and policy semantics apply to CONNECT-UDP;
- no legacy web/TCP/probe-resistance regression occurs;
- privacy tests find no destination, path, payload, or credentials;
- marker: `M4_G4_FORWARDPROXY_INTEGRATION_OK`.

Estimated effort: 1–3 person-days.

Verified result:

- real H3 IPv4, IPv6, domain, missing/wrong/correct Basic auth, exact
  `400/403/407/501`, and probe-resistance passthrough cases pass;
- URI redaction preserves downstream camouflage semantics and protects outer
  Caddy access logging;
- structured powers-of-two counters contain no destination, path, payload, or
  credential;
- the complete legacy suite and race detector remain green;
- marker: `M4_G4_FORWARDPROXY_INTEGRATION_OK`.

### M4-G5 — independent production-server interoperability matrix

Status: complete in forwardproxy commit `7243519` and Caddy commit
`cce894a8`.

Work:

1. Drive the real pinned Caddy binary with an independent RFC 9298 client,
   not the M3 Naive client alone.
2. Prove IPv4, IPv6, domain, deterministic DNS, zero-length datagrams, safe
   maximum/oversize behavior, multiple simultaneous streams, idle expiry,
   client cancellation, server shutdown/restart, and every resource limit.
3. Capture client/server/qlog or equivalent structured evidence that payloads
   use H3 Datagrams, not HTTP DATA, Capsules, UoT, or private framing.
4. Run the existing `forwardproxy` test suite, TCP H1/H2/H3 regressions,
   probe-resistance tests, `go test -race ./...`, repeated lifecycle/stress
   loops, and the reproducible build.
5. Optionally use the frozen M3 tunnel as a smoke oracle, but reserve the full
   SOCKS5-to-production-Caddy matrix and product-level reconnect claims for M5.

Exit:

- the standalone server satisfies RFC 9298 in both directions;
- complete legacy server regressions and repeated lifecycle runs are green;
- marker: `M4_G5_SERVER_INTEROP_OK`.

Estimated effort: 1–2 person-days.

Verified result:

- a standalone pinned Caddy binary passes an independent quic-go RFC 9298
  client across IPv4, IPv6, domain, DNS, zero/safe/oversize payloads,
  simultaneous streams, cancellation, 32/33 association admission, real
  production idle expiry, active shutdown/restart, and repeated stress;
- SETTINGS and direct `RequestStream.SendDatagram`/`ReceiveDatagram` evidence
  prove H3 Datagrams rather than DATA, Capsules, UoT, or private framing;
- debug and access logs contain neither target/path/payload sentinels nor
  either recoverable Basic credential encoding;
- complete legacy, race, and focused Caddy tests pass;
- marker: `M4_G5_SERVER_INTEROP_OK`.

### M4-G6 — release closeout and independent audit

Work:

1. Rebuild from the exact pinned tuple and rerun G0–G5, race, stress, privacy,
   and TCP/probe-resistance matrices from a clean checkout.
2. Inspect the complete forwardproxy and Caddy diffs; publish a patch
   inventory, build tuple, test commands, markers, and rollback boundary.
3. Confirm no generated artifact, credential, certificate, log, packet
   capture, or destination-bearing test output entered either repository.
4. Run a fresh, continuing, read-only `agy` audit over both repository diffs
   and the actual test evidence. Require the reviewer to independently rerun
   the critical matrix and inspect policy, lifecycle, privacy, TCP isolation,
   standards framing, and dependency pinning.
5. Update this repository's status ledger and roadmap only after the audit
   result is known.

Exit:

- final marker: `M4_NATIVE_UDP_SERVER_OK`;
- independent result: `AUDIT_PASS` with zero blocker, high, or medium finding;
- the exact production server commits and build tuple are recorded for M5.

Estimated effort: 1–2 person-days.

## 6. Required verification matrix

| Area | Minimum M4 evidence |
| --- | --- |
| Protocol | H3 Extended CONNECT visibility, negotiated H3 Datagrams, strict RFC 9298 template and Context ID `0` |
| Addressing | IPv4, IPv6 percent encoding, domains, DNS failure, disallowed resolved IP, port boundaries |
| Payload | binary, zero-length, live maximum, oversize, loss-tolerant no-replay behavior |
| Authentication | absent, wrong, and correct Basic credentials without credential logging |
| Policy | ACL, allowed ports, probe resistance, normal-site/PAC passthrough, unsupported upstream mode |
| Resources | per-client/global association, queue/byte, timeout, fair-yield, and shutdown bounds |
| Lifecycle | stream cancel, UDP error, idle expiry, client close, server shutdown/restart, repeated stress |
| Privacy | debug logs/counters contain no target, path, payload, credentials, or raw headers |
| TCP regression | existing forwardproxy H1/H2/H3 CONNECT and Naive padding behavior unchanged |
| Build | exact Go/xcaddy/Caddy/quic-go/forwardproxy tuple reproduces from a clean checkout |
| Independence | a non-Naive RFC 9298 client proves the server before product-level M5 testing |

## 7. Source and test change map

Expected `forwardproxy` fork changes:

- CONNECT dispatch and protocol classification;
- strict URI-template and Context ID codecs;
- transport-neutral authorization/resolution factoring;
- UDP association ownership, pumps, limits, observability, and focused tests;
- pinned build workflow and independent interoperability harness.

Expected Caddy changes:

- the smallest patch/configuration seam required to enable H3 Datagrams;
- only if G1 proves it necessary, a narrow response-writer unwrapping seam;
- focused H3 Datagram/cancellation tests and a documented patch inventory.

Expected changes in this NaiveProxy repository during M4:

- status, plan, audit, and exact server-commit/build-tuple references;
- no production Caddy/forwardproxy implementation;
- no changes to the completed M1–M3 client boundary except a separately
  justified regression fix discovered by M4 testing.

## 8. Risk register and stop conditions

| Risk | Required control | Stop condition |
| --- | --- | --- |
| Floating build selects incompatible Caddy/quic-go APIs | Exact G0 tuple and clean rebuild | Do not code against `@latest` or a moving branch |
| Extended CONNECT protocol is lost through Caddy middleware | Real G1 visibility test | Redesign before guessing from URL fields |
| Wrapped writer hides the H3 stream | G1 unwrap/ownership proof | Do not use reflection or unsafe access |
| Caddy does not negotiate H3 Datagrams | Minimal explicit enablement plus SETTINGS evidence | An echo over DATA/Capsules is not success |
| UDP bypasses TCP ACL/port/DNS policy | Factor authorization from dialing and test each resolved IP | Do not duplicate a weaker UDP policy path |
| Success is sent before target/admission failure is known | Resolve, authorize, admit, and open before `200` | Do not rely on an impossible post-200 status |
| Callback/goroutine/socket survives stream shutdown | Context ownership, race/leak/stress tests | Fix lifecycle before integration |
| Context ID or quarter-stream-id framing is duplicated | quic-go owns stream framing; forwardproxy owns Context ID | Stop on any private or double framing |
| Logs reveal the MASQUE target path | Privacy assertions at debug level | Redact before interoperability testing |
| UDP change alters legacy TCP/probe resistance | Separate branch and full regressions each gate | Revert/redesign before proceeding |

## 9. Effort and completion policy

M4 is budgeted at 10–18 person-days for an audited production-server
milestone. The earlier 7–12 day range was optimistic: source inspection proved
that M4 also needs a Caddy Datagram patch, a reproducible dependency tuple, a
runtime middleware/stream-access decision gate, transport-neutral policy
factoring, and cross-repository lifecycle/privacy tests.

The first H3 Datagram echo is not M4 completion. The milestone closes only
after G0–G6, legacy TCP/probe-resistance regressions, reproducible builds, and
an independent audit all pass.
