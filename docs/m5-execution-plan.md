# Native UDP M5 End-to-End MVP Execution Plan

Last updated: 2026-07-19 (Asia/Shanghai)

Status: planned; M5-G0 is the next implementation gate.

Documentation entry point: [`README.md`](README.md). Verified project state:
[`native-udp-status.md`](native-udp-status.md). Frozen v1 scope and M0–M6
roadmap: [`native-udp-development-plan.md`](native-udp-development-plan.md).

## 1. Mission and completion boundary

M5 composes the independently audited M3 client and M4 production server into
one product-level path:

```text
independent SOCKS5 UDP / HTTP3 application
        -> Naive SOCKS5 UDP ASSOCIATE
        -> M3 production datagram backend
        -> Chromium RFC 9298 CONNECT-UDP + HTTP/3 DATAGRAM
        -> pinned M4 Caddy + forwardproxy
        -> UDP echo, DNS, or HTTP/3 target
        -> same path in reverse
```

M5 is primarily an integration, configuration, and evidence milestone. It
does not redesign the audited client or server. If the first cross-repository
run exposes a runtime defect, the affected owner repository receives the
smallest separately justified fix and its complete scoped regressions; M5 must
not silently expand into a new protocol or broad refactor.

M5 is complete only when:

- the full path works through the pinned production Caddy/forwardproxy server;
- IPv4, IPv6, domain, deterministic DNS, generic UDP, and a real HTTP/3
  application traverse the SOCKS5 UDP relay;
- multiple targets and concurrent associations remain correctly isolated;
- authentication, policy rejection, malformed input, lifecycle, idle expiry,
  server restart, QUIC reconnect, and no-replay behavior are product-proven;
- the production `naive` binary completes a trusted-certificate smoke without
  weakening its default certificate verifier;
- structured evidence proves HTTP/3 DATAGRAM rather than DATA, Capsules, UoT,
  or private framing, and records a v1 no-UDP-padding traffic baseline;
- all M1–M4 markers, all 56 Naive TCP cases, server legacy/privacy tests, and
  cross-repository artifact checks remain green;
- the final aggregate marker is `M5_NATIVE_UDP_MVP_OK`.

Release-candidate hardening, network impairment testing, broad soak, fuzzing,
sanitizers, PMTU adaptation, and multi-platform qualification remain M6.

## 2. Frozen inputs and repository ownership

M5 begins from these exact completed boundaries:

| Component | Branch/revision | M5 role |
| --- | --- | --- |
| NaiveProxy | `codex/native-udp-foundation`; audited M3 implementation through `578e3992`, M3 closeout `2bb83aec` | production SOCKS5 ingress, target backend, Chromium tunnel, client tests |
| forwardproxy | `codex/native-udp-server`; final `8f044e2`, runtime implementation audited at `7243519` | production CONNECT-UDP policy and UDP egress |
| Caddy | `codex/enable-h3-datagrams`; `cce894a8` | pinned H3/QUIC Datagram server and privacy patch |

Repository ownership is strict:

- NaiveProxy owns the M5 orchestrator, SOCKS5 application probes, client
  NetLog evidence, and any client defect fix.
- forwardproxy owns the pinned production server build, Caddyfile fixture,
  server logs, server regression entry points, and any server defect fix.
- Caddy changes are not expected in M5. Any required Caddy runtime change is a
  stop condition until its M4 audit impact is reviewed.
- Generated binaries, temporary trust stores, keys, certificates, NetLogs,
  qlogs, packet captures, credentials, and target-bearing logs stay in a
  private temporary directory and never enter Git.

The M4 server build remains pinned to Go 1.25.12, xcaddy 0.4.5, Caddy
`cce894a8`, quic-go 0.59.0, and forwardproxy `8f044e2`. M5 must not build from
`@latest`, a floating branch, or a different Caddy checkout.

## 3. Inherited boundaries and M5 decisions

### Protocol and data path

- Native UDP remains all-`quic://` only.
- The wire protocol is RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM with Context
  ID `0`. Do not add UoT, UDP-over-stream, Capsules, CONNECT-IP, a second QUIC
  stack, or private framing.
- One client target maps to one fixed-target CONNECT-UDP stream. Domain targets
  remain domains at the client and are resolved/policy-checked by the server.
- A failed or ambiguous datagram is never replayed. Recovery is demonstrated
  only with a later new application datagram.
- Existing `NaiveConnection`, TCP padding, TCP configuration, and M1–M4
  resource constants remain frozen unless a reproducible M5 defect requires a
  separately reviewed change.

### Deterministic runner versus production binary

The existing `naive_socks5_udp_m3_runner` constructs the real M3 production
factory and data path but deliberately installs `MockCertVerifier` for a
short-lived local certificate. It is suitable for deterministic G1–G4
composition tests, but it is not evidence that the shipped `naive` binary uses
its normal trust path correctly.

M5 therefore requires both:

1. deterministic full-matrix runs with the existing test-only runner against
   the real pinned M4 server; and
2. at least one smoke using `out/Release/naive`, its normal configuration
   parser, and `CertVerifier::CreateDefault()` against a certificate trusted by
   the test environment.

There is no production certificate-bypass switch. M5 must not add one. G0 must
choose a reversible trust fixture or an already trusted endpoint. If a trusted
fixture cannot be created without persistent or unsafe host changes, stop and
document the blocker rather than weakening production verification. Any
temporary trust-store mutation must be narrowly scoped, recorded, and restored
even on interruption.

### Independent application evidence

Existing M3 Python probes validate SOCKS5 UDP framing and UDP payload equality.
M5 additionally needs an application-level HTTP/3 probe that is independent of
NaiveProxy's CONNECT-UDP implementation. G0 will freeze the smallest
maintainable test-only SOCKS5-UDP `PacketConn` adapter plus pinned HTTP/3 client
implementation. It must:

- create and retain the SOCKS5 TCP control connection;
- use the returned real `BND.ADDR/BND.PORT`;
- encode/decode RFC 1928 UDP datagrams for IPv4, IPv6, and domain targets;
- expose packet semantics to a pinned HTTP/3 client without reliable-stream
  fallback;
- keep target TLS bypass, if needed for a controlled inner HTTP/3 fixture,
  test-only and separate from Naive's outer proxy certificate verification.

### Privacy and observability

- Client and server logs may record state, generic failure reason, byte count,
  and powers-of-two counters only. They must not expose target, MASQUE path,
  UDP payload, proxy credentials, or recoverable encoded credentials.
- NetLog/qlog/packet evidence is stored only under the temporary test root.
- The no-padding baseline records only packet sizes, direction, and relative
  timing. It must not retain application payloads, destinations, credentials,
  certificate private keys, or decrypted packet contents.
- Test output uses deterministic marker names but non-sensitive payloads.

## 4. Sequential execution gates

Each gate is a small green-to-green commit in the repository that owns its
changes. Do not begin a later gate until the current gate's exit criteria pass
and `docs/native-udp-status.md` records the evidence.

### M5-G0 — topology, trust, harness, and evidence contract

Status: next.

Work:

1. Verify the three pinned branches/revisions, clean worktrees, build tools,
   and existing M3/M4 aggregate markers before adding M5 code.
2. Freeze a dynamic-port test topology for Caddy, local SOCKS, IPv4/IPv6 echo,
   DNS, and inner HTTP/3 fixtures. No test may depend on fixed port `19443`.
3. Add the `tests/socks5_udp_m5.sh` orchestration skeleton with strict cleanup,
   temporary artifact ownership, bounded waits, and deterministic failure
   diagnostics.
4. Freeze the independent SOCKS5-UDP `PacketConn`/HTTP3 application probe,
   dependency pin, and repository owner. Do not reuse Naive's tunnel code.
5. Select and prove the safe production-binary trust strategy. Add a negative
   untrusted-certificate assertion before the positive trusted smoke.
6. Freeze the G1-G6 marker list, credential/target sentinels, artifact scan,
   capture format, and no-padding measurement fields.

Exit:

- the exact client/server binaries and revisions are printed by the harness;
- the test topology reserves ports without race-prone fixed assumptions;
- cleanup restores any temporary trust state and leaves no child process;
- production certificate verification is not weakened;
- marker: `M5_G0_PRODUCT_CONTRACT_OK`.

Estimated effort: 1–2 person-days.

### M5-G1 — first audited-client-to-production-server echo

Work:

1. Build the current M3 targets and the exact pinned M4 Caddy binary.
2. Start the production forwardproxy Caddy route with short-lived test data,
   Basic authentication, H3 Datagrams, and redacted debug/access logs.
3. Start `naive_socks5_udp_m3_runner` with the correct cached credentials and
   its real production backend/factory.
4. Complete SOCKS5 negotiation, validate the returned UDP relay endpoint, and
   round-trip one IPv4 datagram through the production server to a local echo
   target.
5. Require client NetLog and server evidence for one authenticated
   `connect-udp` stream and H3 Datagram traffic without logging the target or
   payload.

Exit:

- byte-identical IPv4 echo completes through all client and server layers;
- the M4 production server, not `naive_masque_server`, owns the server hop;
- no production-binary claim is made yet because this gate uses the test-only
  certificate verifier;
- marker: `M5_G1_CROSS_REPO_ECHO_OK`.

Estimated effort: 0.5–1 person-day.

### M5-G2 — addressing, DNS, multiplexing, and HTTP/3 application matrix

Work:

1. Verify IPv4, IPv6, domain, deterministic DNS query/response, binary,
   zero-length, safe-size, and oversize-then-healthy datagrams.
2. Interleave multiple targets through one SOCKS5 UDP association and verify
   original address-family/domain framing on every response.
3. Run multiple concurrent SOCKS5 associations and confirm no target, payload,
   response, or lifecycle state crosses association boundaries.
4. Run the independent SOCKS5-UDP PacketConn/HTTP3 client against a controlled
   local HTTP/3 origin through the complete Naive/Caddy path. Verify an HTTP/3
   request/response and inner QUIC connection close.
5. Correlate client NetLog and server structured evidence without relying on
   application payload visibility.

Exit markers:

```text
M5_G2_IPV4_IPV6_DOMAIN_OK
M5_G2_DNS_OK
M5_G2_ZERO_OVERSIZE_OK
M5_G2_MULTI_TARGET_OK
M5_G2_CONCURRENT_ASSOCIATIONS_OK
M5_G2_HTTP3_APPLICATION_OK
```

Estimated effort: 1–2 person-days.

### M5-G3 — authentication, policy, malformed input, and isolation

Work:

1. Verify correct, missing, and wrong upstream Basic credentials. Rejections
   must not emit a UDP response, leak credentials, or terminate unrelated
   targets/associations.
2. Verify local SOCKS5 authentication success and failure independently from
   upstream proxy authentication.
3. Reconfirm exact SOCKS reply `0x01` for direct, HTTPS/H2, mixed, and
   unavailable native-UDP backends.
4. Exercise server ACL/allowed-port rejection, domain policy, DNS failure,
   unsupported upstream mode, and association admission. Verify the server's
   expected status and the client's target-scoped failure/cooldown behavior.
5. Send malformed/truncated RFC 1928 packets, `FRAG != 0`, spoofed source IP or
   port, and invalid address types; then prove a healthy packet still passes.
6. Scan all logs and artifacts for target, path, payload, plain credentials,
   Base64 credentials, and double-Base64 credentials.

Exit markers:

```text
M5_G3_AUTH_POLICY_OK
M5_G3_NON_QUIC_REJECTION_OK
M5_G3_MALFORMED_ISOLATION_OK
M5_G3_PRIVACY_OK
```

Estimated effort: 1–2 person-days.

### M5-G4 — lifecycle, restart, reconnect, idle expiry, and no replay

Work:

1. Close the SOCKS TCP control channel with idle, open, and pending target
   tunnels; verify local relay closure and server association release.
2. Stop Caddy with active traffic, keep the client process alive, restart the
   same pinned server configuration, wait through the documented cooldown,
   and prove a later new datagram creates a fresh working tunnel.
3. Force or observe an outer QUIC session close and verify target-scoped
   retirement, fresh connection establishment, and continued operation of an
   unrelated target.
4. Exercise the real client target-idle and production server two-minute idle
   boundaries without test-only runtime constant changes; verify later traffic
   creates fresh state.
5. Use unique sequence identifiers at the fixtures to prove pre-failure or
   ambiguous datagrams are never replayed after restart/reconnect.
6. Repeat restart/reconnect and concurrent traffic enough to catch leaked
   processes, sockets, associations, or stale callbacks.

Exit markers:

```text
M5_G4_CONTROL_CLOSE_OK
M5_G4_SERVER_RESTART_OK
M5_G4_QUIC_RECONNECT_OK
M5_G4_IDLE_RECONNECT_OK
M5_G4_NO_REPLAY_OK
```

Estimated effort: 1–2 person-days.

### M5-G5 — production binary, wire evidence, and no-padding baseline

Work:

1. Run `out/Release/naive`, not the M3 runner, through its normal CLI/config
   parser with `--listen=socks://...` and `--proxy=quic://...`.
2. First prove an untrusted server certificate fails. Then use the reversible
   G0 trust fixture and prove correct authenticated IPv4 echo plus one HTTP/3
   application request through `CertVerifier::CreateDefault()`.
3. Capture production client NetLog plus server H3 Datagram evidence. Where a
   packet/qlog capture is used, keep keys and captures temporary and verify
   that the evidence distinguishes DATAGRAM frames from stream DATA without
   entering Git.
4. Record packet sizes, directions, burst timing, and connection lifetime for
   representative echo, DNS, and HTTP/3 traffic. Record the explicit decision
   that v1 ships without a Naive-specific UDP padding layer; do not add padding
   in M5.
5. Verify normal TCP SOCKS traffic through the same production client/server
   configuration remains unchanged.

Exit markers:

```text
M5_G5_DEFAULT_CERT_VERIFIER_OK
M5_G5_PRODUCTION_BINARY_OK
M5_G5_H3_DATAGRAM_EVIDENCE_OK
M5_G5_NO_PADDING_BASELINE_OK
```

Estimated effort: 1–2 person-days.

### M5-G6 — complete regressions, artifact closeout, and independent review

Work:

1. Rebuild the full named M1–M3 Release target set and the pinned M4 Caddy
   binary from their recorded inputs.
2. Run every M1 script, M2/M3 aggregate, the M5 aggregate, all 56 Naive TCP
   cases, forwardproxy normal/race/legacy/privacy suites, and focused Caddy H3
   Datagram tests.
3. Repeat the non-idle M5 product matrix three consecutive times against fresh
   temporary roots. Run the production-duration idle case at least once.
4. Run `git diff --check` in all repositories and inspect tracked/untracked
   artifacts for binaries, logs, captures, keys, certificates, credentials,
   destinations, or generated dependency trees.
5. Run one bounded, read-only independent audit over the actual M5 diffs and
   test evidence. The reviewer must inspect protocol isolation, production
   certificate verification, application-level H3 proof, recovery/no-replay,
   privacy, TCP isolation, and exact dependency pinning. The full long matrix
   is run locally in step 2; the reviewer must at minimum rerun the focused G1
   smoke and independently validate the evidence manifest.
6. Record exact commits, commands, markers, remaining M6 risks, and rollback
   boundaries in the status ledger.

Exit:

- final marker: `M5_NATIVE_UDP_MVP_OK`;
- independent result: `AUDIT_PASS` with zero blocker, high, or medium finding;
- M1–M4 evidence remains valid or any scoped fix has a new explicit audit
  boundary;
- M6 receives a frozen MVP artifact and an explicit hardening backlog.

Estimated effort: 1 person-day.

## 5. Required verification matrix

| Area | Minimum M5 evidence |
| --- | --- |
| Product topology | SOCKS5 application -> production M3 backend -> pinned M4 Caddy/forwardproxy -> target and back |
| Production binary | shipped `naive` CLI/config plus default certificate verifier succeeds with trusted TLS and fails with untrusted TLS |
| Addressing | IPv4, IPv6, domain, deterministic DNS, original SOCKS response framing |
| Applications | generic UDP echo and an independent real HTTP/3 request/response over the SOCKS UDP relay |
| Payload | binary, zero-length, safe size, local oversize drop followed by healthy traffic |
| Multiplexing | multiple targets in one association and multiple concurrent associations |
| Authentication | local SOCKS auth and upstream Basic auth: correct, missing, and wrong |
| Policy | non-QUIC `0x01`, ACL/port/DNS/upstream/admission failures and target isolation |
| Malformed traffic | truncation, invalid address type, `FRAG != 0`, spoofed source, recovery with a later valid packet |
| Lifecycle | control close, target idle, server idle, outer QUIC close, shutdown/restart, fresh later traffic |
| Delivery semantics | unique fixture sequence evidence proves no replay after ambiguous failure |
| Standards evidence | H3 Datagram negotiation/frames; no DATA, Capsules, UoT, second QUIC stack, or private framing |
| Privacy | no target, path, payload, credential, or recoverable encoding in logs/counters/artifacts |
| Traffic baseline | sizes/directions/timing only; explicit v1 no-UDP-padding measurement record |
| Regression | M1–M4 matrices, 56 Naive TCP cases, server legacy/race/privacy, exact pinned rebuild |

## 6. Expected source and test change map

Expected M5 changes in NaiveProxy:

- `tests/socks5_udp_m5.sh` orchestration and cleanup;
- an independent test-only SOCKS5-UDP PacketConn/HTTP3 application probe;
- narrowly shared non-sensitive fixture helpers where duplication would make
  lifecycle cleanup less reliable;
- M5 status, evidence manifest, and documentation updates.

Expected M5 changes in forwardproxy:

- a dynamic-port/product-composition Caddyfile or script fixture;
- only test harness/configuration changes unless M5 exposes a reproducible
  server defect.

Expected Caddy changes: none.

Production NaiveProxy or forwardproxy source changes are not pre-authorized by
this plan. A reproducible defect may justify the smallest owner-repository fix,
but the gate stops until its M1–M4 regression and audit impact are written down.

## 7. Risk register and stop conditions

| Risk | Required control | Stop condition |
| --- | --- | --- |
| Test runner is mistaken for shipped `naive` | Separate G1–G4 runner evidence from G5 production-binary evidence | Do not claim M5 complete without default-verifier production smoke |
| Local TLS fixture requires unsafe trust bypass | Reversible temporary trust strategy plus negative test | Never add a production certificate-bypass flag or persistent untracked trust change |
| HTTP/3 probe reuses Naive internals | Independent SOCKS5 PacketConn and pinned H3 library | A UDP echo alone is not the required application-level proof |
| Server fork or Caddy drifts | Exact heads, build tuple, and binary metadata at G0/G6 | Stop on floating refs or an unexpected module graph |
| Product reconnect replays old data | Unique sequence fixture and later-new-packet rule | Do not add transparent retransmission |
| M5 changes audited M3/M4 runtime code | Owner-specific minimal diff and full scoped regressions | Stop and revise the audit boundary before continuing |
| Fixed ports create flaky or unsafe tests | Reserve dynamic TCP/UDP ports and bounded readiness checks | Do not serialize around hard-coded `19443` as the final harness |
| Capture leaks secrets or targets | Temporary private root and forbidden-sentinel scan | Delete/rework evidence before it can be staged |
| Long idle test is shortened in production | Run real production constants once | Test-only overrides cannot satisfy the production idle exit criterion |
| TCP behavior changes | All 56 client cases plus server legacy/padding suite | Revert or isolate before proceeding |

## 8. Effort and completion policy

Direct source inspection adds two concrete tasks that the earlier 5–8 day
roadmap estimate did not fully expose: a safe production-binary trust fixture
and an independent SOCKS5-UDP-backed HTTP/3 application probe. M5 is therefore
budgeted at 6–10 person-days, excluding unattended production-duration idle
waits.

The first echo through production Caddy is G1 evidence, not M5 completion. M5
closes only after G0–G6, the shipped `naive` trust-path smoke, application-level
HTTP/3 proof, restart/reconnect/no-replay evidence, complete regressions,
artifact/privacy checks, and the independent final review all pass.
