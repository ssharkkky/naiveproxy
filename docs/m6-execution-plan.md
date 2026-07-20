# M6 Hardening and Release-Candidate Execution Plan

Last updated: 2026-07-20 (Asia/Shanghai)

Status: M5 is complete and independently audited. M6-G0 is complete at
`80d37395a6`; M6-G1 remains release-blocking, M6-G3 qualification is running,
and M6-G4 is complete at `5893f97f6e`.

Operational evidence belongs in [`native-udp-status.md`](native-udp-status.md).
This document defines pending work, ordering, exit criteria, risks, and stop
conditions. It must not be used to claim a gate passed before the status ledger
records the exact successful command and revision.

## 1. Mission and exit boundary

M6 turns the audited native-UDP MVP into a release candidate. It hardens the
existing RFC 9298/HTTP/3 DATAGRAM product; it does not add another transport,
change the TCP data path, or expand v1 protocol scope.

M6 completes only when:

- a documented safe inner-payload policy and PMTU behavior pass;
- deterministic impairment, pressure, soak, fuzz, sanitizer, and race gates
  have no open blocker/high/medium finding;
- macOS, Linux, Windows, and Android have separately attributable build and
  runtime evidence at the level defined in G5;
- all inherited M1-M5 and TCP/server regressions remain green;
- configuration, compatibility, observability, rollback, dependency pins,
  and known limitations are documented;
- a clean release-candidate build is reproduced and an independent review
  returns `AUDIT_PASS` with zero blocker/high/medium finding.

The final marker is `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`. Passing a gate on the
current macOS host is development evidence, not substitute evidence for a
platform that has not yet been qualified.

## 2. Frozen inputs and ownership

| Input | Frozen revision/boundary | M6 rule |
| --- | --- | --- |
| NaiveProxy M5 closeout | `e70ee79e05` | Must remain an ancestor |
| Audited NaiveProxy runtime | audited through `eaf172d971`, including `333b7cb253` | Any later `src/net` change reopens the affected client audit boundary |
| forwardproxy runtime | `8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7` plus build-lock commit `e9663e4` | Later fixture-only work may live under `tests/m6/`; runtime/build-pin changes require owner regressions and review |
| forwardproxy M5 fixture head | `2b2a8ea` | Must remain an ancestor of later fixture commits |
| Caddy | `dd9a89c11194dcb806d845233995ef040f096464` | M6 race-fix pin; owner regression and independent audit required |
| Server toolchain | Go `1.25.12`, xcaddy `0.4.5`, quic-go `0.59.0` | No floating tool or module versions |

Repository ownership remains split:

- NaiveProxy owns client production code, Chromium integration, client tests,
  cross-repository orchestration, and the M6 evidence ledger.
- `forwardproxy` owns server runtime and server-specific fixtures.
- Caddy owns HTTP/3 Datagram listener support.
- Test-only impairment, targets, and probes must never enter a shipped binary.

## 3. Inherited non-negotiable contracts

- Preserve `NaiveConnection`, the TCP data mover, and TCP padding.
- Native UDP v1 is all-`quic://` only and uses RFC 9298 CONNECT-UDP plus
  HTTP/3 DATAGRAM with Context ID `0`.
- Do not add UoT, UDP-over-stream, Capsules as a data path, a second QUIC
  stack, a private framing protocol, or automatic datagram replay.
- Preserve the SOCKS control-channel lifetime, exact transient
  `NetworkAnonymizationKey`, proxy authentication, target isolation, and
  bounded admission behavior audited in M3-M5.
- Do not log payloads, destinations, request paths, credentials, certificate
  private material, or recoverable encodings of those values.
- The shipped client always uses `CertVerifier::CreateDefault()`. Test trust
  must be temporary and reversible; no production bypass is allowed.
- UDP payload padding remains out of v1. G1/G2 may measure encrypted traffic,
  but adding a datagram-padding protocol requires a separate design review.

## 4. Evidence and release-blocker policy

Every result must be one of:

- **verified** — exact command, revision, marker, and artifact/privacy checks
  are recorded in the status ledger;
- **not run** — no release claim is made;
- **blocked** — the reason and owner are recorded;
- **failed** — M6 stops at the current gate until fixed or the scope is
  explicitly revised.

The following block release:

- an open blocker, high, or medium finding;
- memory-safety failure, data race, crash, hang, unbounded growth, secret or
  target disclosure, ambiguous replay, or cross-association delivery;
- a regression in M1-M5, the 56 TCP cases, legacy server behavior, policy,
  authentication, or certificate verification;
- a required platform with missing or failing G5 evidence;
- an unpinned dependency, unreproducible release build, or cleanup that leaves
  trust entries, processes, credentials, packet captures, or private keys.

Low findings may be deferred only with an owner, rationale, mitigation, and a
tracked follow-up. A current-host gate may pass while another platform remains
`not run`, but G5 and M6 cannot close in that state.

## 5. Sequential gates

### G0 — release contract and environment readiness

Status: complete. The read-only contract/environment runner and focused
incremental Release build passed on macOS arm64 with final marker
`M6_G0_CONTRACT_OK`. Exact evidence is in the status ledger.

Purpose: freeze M6's contracts before changing or stressing a runtime boundary.

Work:

1. Add a machine-readable M6 contract covering pins, gates, platform tiers,
   evidence states, release blockers, duration tiers, and forbidden artifacts.
2. Add a read-only contract/environment runner that verifies the three Git
   boundaries, audited M5 ancestry, current macOS arm64 toolchain, existing
   Release targets, and exact Go/xcaddy pins.
3. Freeze a non-privileged user-space UDP impairment shim as G2's default.
   `pf`/`dnctl`, Linux `netem`, and administrator access are optional
   cross-checks, not prerequisites for deterministic tests.
4. Record macOS arm64 as environment-ready and Linux x64, Windows x64, and
   Android arm64 as pending. Do not turn pending entries into pass claims.

Exit:

- `tests/m6/g0_contract.sh` passes without modifying trust or runtime source;
- focused Release target graph/build readiness is confirmed;
- markers `M6_G0_RELEASE_CONTRACT_OK`, `M6_G0_PLATFORM_CONTRACT_OK`,
  `M6_G0_M5_BASELINE_OK`, `M6_G0_TOOLCHAIN_OK`, and
  `M6_G0_CONTRACT_OK` are recorded.

Stop if the M5 runtime differs from its audited source boundary, a dependency
pin is unavailable, or the current build cannot be reproduced incrementally.

### G1 — inner payload ceiling and PMTU behavior

Status: complete. G1b2's shipped/default-verifier positive phase completed
inside one temporary user-domain trust window and was followed by cleanup and
an explicit untrusted check. G1d then passed three complete regression runs;
the final marker is recorded in the status ledger.

Sub-gates:

- [x] G1a — deterministic production-backend unit proof for exact ceiling,
  ceiling-plus-one drop, live ceiling reduction, later restoration, exact
  payload preservation, and accounting; commit `1870779147`.
- [x] G1b1 — live production-backend/production-server ceiling measurement for
  IPv4, IPv6, and domain targets without hard-coding a release value; three
  fresh roots measured 1314 bytes; commit `9c72a7da08`.
- [x] G1b2 — repeat the frozen measurement through shipped `naive` with the
  default verifier inside one explicitly authorized temporary-trust window;
  IPv4/IPv6/domain all measured 1314 bytes and trust cleanup restored the
  untrusted negative result.
- [x] G1c — lower the outer UDP payload ceiling to 1232 bytes (IPv6 minimum
  PMTU 1280 minus IPv6/UDP headers), then restore it; prove candidate 1200-byte
  delivery, 1314-byte blackhole, recovery, target isolation, no replay, and
  redacted observability in three fresh roots; commit `9c72a7da08`.
- [x] G1d — choose/document the 1200-byte candidate policy and run the
  complete focused/cumulative regression set three times; the final marker is
  `M6_G1_PAYLOAD_PMTU_OK`.

Purpose: replace the MVP's 1200-byte/4096-byte probes with an explicit and
measured release policy.

Work:

1. Measure the live H3 Datagram payload ceiling for IPv4, IPv6, and domain
   targets through the shipped client and production server.
2. Test exact-ceiling success, ceiling-plus-one local drop, repeated oversize
   recovery, empty payload, and a later healthy datagram on the same and an
   unrelated association.
3. Exercise a lowered and later restored path MTU. The deterministic unit seam
   proves the backend queries the live tunnel ceiling for each write; the
   black-box shaper proves the candidate safe payload under an IPv6-minimum
   outer path. Neither test may fragment or convert an oversized datagram into
   reliable stream data.
4. Choose and document the v1 application-facing inner-payload policy. Do not
   hard-code a number before the measurements identify overhead and platform
   variance. The measured 1200-byte candidate is documented in
   [`native-udp-payload-policy.md`](native-udp-payload-policy.md); it remains a
   release candidate until platform qualification completes.
5. Preserve rate-limited, redacted oversize/transport-error observability.

Exit: deterministic boundary and PMTU-change tests pass three times, recovery
and no-replay evidence pass, and `M6_G1_PAYLOAD_PMTU_OK` is recorded.

Stop on silent truncation, DATA/Capsule fallback, payload replay, target leak,
or a required production change that lacks owner-specific regressions.

### G2 — deterministic network impairment

Status: complete at `028d3984d4`. Three full fresh-root matrices passed all
five frozen profiles with echo, DNS, independent HTTP/3 application, control
close, recovery, target isolation, no replay, and privacy evidence.

Purpose: verify useful behavior under adverse but reproducible networks.

Work:

1. Implement a test-only seeded UDP shaper between client and production
   Caddy, with bounded loss, reordering, delay, jitter, and bandwidth modes.
2. Run isolated and combined profiles over echo, DNS, and independent HTTP/3
   application traffic. Record seed, profile, sent/received counts, elapsed
   time, reconnect count, and redacted error counters.
3. Verify control-channel closure, later fresh traffic, target isolation, and
   no replay after an ambiguous impaired datagram.
4. Keep pass criteria protocol-aware: unreliable application datagrams may be
   lost; the gate requires bounded recovery and no corruption/replay, not
   impossible lossless delivery.

Exit: every frozen profile completes within its timeout, healthy traffic
recovers after impairment removal, three repeated seeded runs agree, and
`M6_G2_NETWORK_IMPAIRMENT_OK` is recorded.

Stop if the shaper can reorder traffic across test roots, retains payload
artifacts, needs production code, or makes a nondeterministic pass/fail claim.

### G3 — resource pressure and soak

Status: complete. After the pre-fix and explicit-duration diagnostic runs, a
clean unshortened qualification root on Caddy `dd9a89c1` passed admission,
churn, resource recovery, privacy, and harness checks and emitted
`M6_G3_STRESS_SOAK_OK`. Exact aggregate evidence is in the status ledger.

Purpose: prove bounded operation beyond the short M5 concurrency matrix.

Work:

1. Exercise per-target, per-association, and per-proxy admission boundaries,
   queue/byte caps, churn, repeated target retirement, and capacity reuse.
2. Sample resident memory, file descriptors/handles, task/thread counts,
   association counters, and failure/recovery counts without retaining target
   or payload values.
3. Provide `smoke` (minutes), `qualification` (hours), and `extended`
   (maintainer-scheduled) duration tiers. Store seeds and aggregate metrics.
4. Include server restart, outer-QUIC loss, control close, idle expiry, and
   post-soak fresh traffic.

Exit: qualification soak has no crash, hang, stale process, capacity leak,
unbounded monotonic resource growth, privacy leak, or replay;
`M6_G3_STRESS_SOAK_OK` is recorded. Extended soak may be scheduled separately
only if the release checklist explicitly names its result and owner.

Stop on unbounded growth, failure to reclaim capacity, cross-association
delivery, or a result that depends on shortening production timeouts.

### G4 — fuzz, sanitizer, race, and lifecycle hardening

Status: complete on the post-fix runtime. Client/forwardproxy evidence is at
`5893f97f6e`; Caddy race fix `dd9a89c11194dcb806d845233995ef040f096464`,
owner regressions, and the full frozen-budget rerun all pass. A scoped
independent audit of the new Caddy range remains part of G6.

Purpose: attack parser and asynchronous ownership boundaries with automated
defensive testing.

Work:

1. Add focused fuzz targets/corpora for RFC 1928 UDP decoding/encoding and
   RFC 9298 path/Context-ID parsing without duplicating production parsers.
2. Add randomized, seed-replayable lifecycle/state-machine tests for pending
   connect/read/write, destruction, timeout, close, and callback reentrancy.
3. Run supported ASan/UBSan builds for focused Naive targets; run server
   `go test -race`; record unsupported sanitizer/platform combinations as
   `not applicable`, never as passes.
4. Minimize and commit only non-sensitive regression inputs for any finding.

Exit: the frozen fuzz-time/corpus budget and sanitizer/race matrix are green,
all discovered defects have regression tests, and
`M6_G4_SANITIZER_FUZZ_OK` is recorded.

Stop immediately on a memory-safety issue, data race, assertion bypass,
non-reproducible crash, or a corpus containing sensitive material.

### G5 — platform qualification

Purpose: establish separately attributable release evidence for each promised
platform rather than extrapolating from macOS.

Sub-gates:

- [x] G5a — freeze and validate the fail-closed platform evidence record;
  initial records remain `not run` and cannot become `verified` without every
  required command, revision, marker, OS version, and architecture field;
  contract commit `9869f1d6d1`, runner `09af3795c7`.
- [x] G5b — qualify macOS arm64 through the complete row below. The
  authorized shipped-product and focused G2-G4 matrix passed on macOS 26.5.2
  arm64 and the exact evidence record is `verified`.
- [ ] G5c — qualify Linux x64 through the complete row below.
- [ ] G5d — qualify Windows x64 through the complete row below.
- [ ] G5e — qualify Android arm64 host-app/package behavior through the
  complete row below.
- [ ] G5f — confirm cross-platform wire interoperability and close the final
  record without promoting emulation-only evidence to runtime evidence.

Minimum matrix:

| Platform | Build evidence | Runtime evidence required |
| --- | --- | --- |
| macOS arm64 | Release client and focused tests | full shipped-client product smoke, default verifier positive/negative, G1-G4 focused gates |
| Linux x64 | reproducible Release client/server | SOCKS UDP echo, DNS, independent H3 application, TCP parity, trust positive/negative, focused impairment/lifecycle |
| Windows x64 | reproducible Release client | SOCKS UDP echo, DNS, independent H3 application, TCP parity, trust positive/negative, control close/reconnect |
| Android arm64 | reproducible library/package integration | host-app SOCKS UDP echo, DNS, independent H3 application, TCP parity, trust behavior, app lifecycle suspend/resume or restart |

Each platform record must include OS/runtime version, architecture, exact Git
and dependency revisions, commands, markers, and known limitations. Emulation
may establish build readiness but does not replace required runtime evidence.
If a platform cannot meet the matrix, native UDP must remain unqualified or
disabled for that platform rather than being described as release-ready.

Exit: all four platform records satisfy their rows, cross-platform wire
interoperability is confirmed, and `M6_G5_PLATFORM_QUALIFICATION_OK` is
recorded.

### G6 — release-candidate closeout and independent audit

Purpose: reproduce the release from clean inputs and close M6.

Work:

1. Run complete M1-M6 client/product/server matrices, all 56 TCP cases,
   legacy/privacy/race tests, and three clean-root product repetitions.
2. Rebuild with exact pins, inventory all patches and generated artifacts,
   scan evidence for forbidden data, and verify process/trust cleanup.
3. Publish configuration, compatibility, observability, troubleshooting,
   known-limitations, upgrade, and rollback documentation.
4. Obtain maintainer approval for the Chromium API boundary and payload
   policy.
5. Run a read-only independent review over the exact M6 ranges. Any
   blocker/high/medium finding returns ownership to the relevant earlier gate.

Exit: `M6_G6_LOCAL_RELEASE_CHECKLIST_OK`, independent `AUDIT_PASS` with zero
blocker/high/medium finding, and `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`.

## 6. Gate dependency and change policy

```text
M5 audited baseline
  -> G0 contract/environment
  -> G1 payload + PMTU
  -> G2 impairment
  -> G3 pressure + soak
  -> G4 fuzz + sanitizers
  -> G5 platform qualification
  -> G6 release closeout + independent audit
```

G1-G4 test-harness work may overlap locally only after G0, but their evidence
and commits remain ordered. G5 consumes the frozen G1-G4 contracts. G6 cannot
begin while any required platform is `not run`, `blocked`, or `failed`.

Each gate is one or more small green-to-green commits in the repository that
owns the change. A production-source fix must include:

1. a minimized reproducer;
2. the narrowest owner-repository patch;
3. focused and inherited regressions;
4. an explicit audit-boundary impact note;
5. updated exact revisions in the status ledger and machine contract.

## 7. Artifact and privacy contract

Temporary roots must be private and removed on success, failure, and signals.
Packet/qlog/NetLog files, certificates, private keys, credentials, core dumps,
sanitizer logs, and soak logs are evidence inputs, never default Git inputs.
Only redacted aggregate summaries may be committed.

Forbidden committed/runtime evidence fields include payloads, destinations,
RFC 9298 request paths, credentials or encodings, certificate private keys,
and decrypted packets. Permitted aggregate fields include relative time,
direction, packet size, connection age, seed/profile identifier, counts,
resource totals, error class, and platform/toolchain version.

## 8. Effort estimate

| Gate | Estimate |
| --- | ---: |
| G0 contract/environment | 0.5-1.5 person-days |
| G1 payload/PMTU | 2-3 person-days |
| G2 impairment | 2-3 person-days |
| G3 pressure/soak harness and analysis | 2-4 person-days, excluding unattended soak time |
| G4 fuzz/sanitizer/race | 2-4 person-days |
| G5 platform qualification | 2-5 person-days, excluding external runner availability |
| G6 release/audit closeout | 1-2 person-days |

The ranges overlap where one harness serves several gates; the roadmap's
retained M6 planning range remains 10-20 person-days, excluding unattended
soak time and waits for platform infrastructure.
