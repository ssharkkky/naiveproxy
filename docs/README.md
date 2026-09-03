# Native UDP Documentation Index

Last updated: 2026-09-03 (Asia/Shanghai)

This directory tracks the design, implementation evidence, and audits for
adding Chromium-network-stack-driven native UDP proxying to NaiveProxy. The
normal upstream README describes the released TCP-focused product. Repository
operating rules for agents are in [`../AGENTS.md`](../AGENTS.md).

## Current product and deployment

- All four fork development branches are `master`. The immutable current
  product inputs are in [`../release/product.lock.json`](../release/product.lock.json):
  NaiveProxy `9842de51`, forwardproxy `4265c663`, Caddy `0ea5700f`, and
  quic-go `c308178d`.
- M7 G4 is complete (`M7_G4_PARITY_OK`); G5 regression/audit qualification is
  intentionally deferred. The release channel therefore remains
  `experimental`, not stable.
- [`current-deployment.md`](current-deployment.md) is the authority for the
  binaries running on `rtr.local`, `lllinya.com`, and
  `triptrip999.qzz.io`. It links the client and server manifests, online
  SHA256 values, runtime configuration, metrics semantics, and rollback files.
- Older commit IDs and deployment hashes below are historical gate evidence.
  They must not be used to infer the current product lock or live deployment.

## Historical milestone handoff

- Branch: `master`.
- M0-M6 are complete. M1-M5 are independently audited; M6 release candidate audited (`AUDIT_PASS`).
- M3 final client marker: `M3_NATIVE_UDP_CLIENT_OK`; closeout commit
  `2bb83aec`.
- M4 final server marker: `M4_NATIVE_UDP_SERVER_OK`; forwardproxy `8f044e2`,
  Caddy `cce894a8`.
- M5 final marker: `M5_NATIVE_UDP_MVP_OK`; audited client revision
  `eaf172d971`.
- Milestone status: **M6 complete** (release candidate qualified, `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`); merged to `master` at `fcf3bb36f3`.
- M6-G0 release contract/environment marker: `M6_G0_CONTRACT_OK`; commit
  `80d37395a6`.
- All M6 gates complete: G5e Android runtime and G6 release closeout are now verified (`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`).
  G1-G4, macOS G5b, and G5f cross-platform wire interoperability are
  complete. The forced-SOCKS TCP probe
  exposed a forwardproxy padding-negotiation defect; owner runtime fix
  `baa7f2dd` and hostless-listener fixture head `f14924cd` remain the runtime
  boundary; current qualification head `964281a` adds only portable owner-test
  fixtures and is pinned for every new platform run. The fixture uses a
  hostless TLS listener because ordinary
  CONNECT carries the target authority, not the proxy hostname. Caddy race
  fix `dd9a89c1` passed owner regressions and the frozen G4 rerun. G4
  implementation commit is `5893f97f6e`. G5a's fail-closed
  platform evidence schema is complete at `9869f1d6d1`; macOS arm64 is now
  verified at NaiveProxy `d402f9261c`, forwardproxy `f14924cd`, and Caddy
  `dd9a89c1`. Linux x64 is verified by GitHub Actions run `29754432052` at
  NaiveProxy `f7e206a308`, the same forwardproxy/Caddy pins, and final marker
  `M6_G5C_LINUX_X64_OK`. Windows run `30084029306` passed the trusted-leaf
  preflight, reproducible builds, default-verifier rejection, trusted UDP
  echo, and DNS, but its independent H3 application probe timed out. Fixture
  commit `a2e06cc21a` removed the Windows-only wildcard-`.localhost` resolver
  dependency; run `30097890690` then passed H3 and TCP before exposing a
  Windows `WSAECONNRESET` portability gap in the control-close test oracle.
  The first synthetic-error fix was insufficient. Commit `5bcdd97107` now
  validates a real closed UDP port in a fast native Windows preflight; run
  `30107431553` passed that probe and the full TrustedPeople lifecycle.
  Full run `30107604684` then passed product traffic before exposing legacy
  owner tests that depended on wildcard `.localhost` resolution and a fixed
  TLS startup sleep. Forwardproxy test-only commits `9b40eeb` and `964281a`
  separate socket addresses from Host/SNI identity, use resolvable loopback
  targets, and wait for actual TLS readiness. Native Windows fast run
  `30167351024` passed the complete owner `go test ./...` suite with marker
  `M6_G5D_WINDOWS_FORWARDPROXY_TESTS_OK`.
  Full native run `30167583501` then passed at NaiveProxy `3ed7cbc3de`,
  forwardproxy `964281a`, and Caddy `dd9a89c1`, emitting
  `M6_G5D_WINDOWS_X64_OK`. Windows x64 and Android arm64 (G5e) are both verified.
- All 7 milestones (M0-M6) are complete (100%); M6 closed as a qualified release
  candidate (`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`); merged to `master` at
  `fcf3bb36f3`. Not yet a production release (see `native-udp-release-guide.md`).
- Post-M6 follow-on: **M7 BBR congestion control has completed through G4**;
  G5 is deferred. **M8 H2 datagram fallback is planned but not started**.
  Both retain CUBIC as the product default and add no private wire protocol.
- Unrelated untracked `.DS_Store` and `src/tmp/` entries must remain outside
  native UDP commits.

M5 composes the audited halves without redesigning them:

```text
SOCKS5 UDP / HTTP3 application
  -> production Naive client path (M1-M3)
  -> RFC 9298 CONNECT-UDP + HTTP/3 DATAGRAM
  -> production Caddy/forwardproxy server (M4)
  -> UDP or HTTP/3 target
```

## Documents by role

| Document | Role | Update policy |
| --- | --- | --- |
| [`current-deployment.md`](current-deployment.md) | Current live client/server artifacts, manifests, SHA256, configuration, metrics semantics, and rollback | Update after every production artifact replacement |
| [`native-udp-status.md`](native-udp-status.md) | Operational source of truth: verified state, evidence, commands, markers, and exact commits | Update only after a gate actually passes |
| [`m6-execution-plan.md`](m6-execution-plan.md) | Completed M6 G0-G6 release-hardening sequence, release blockers, platform matrix, risks, and stop conditions | Historical; factual clarifications only |
| [`m7-execution-plan.md`](m7-execution-plan.md) | M7 BBR congestion-control sequence; G1-G4 complete and G5 intentionally deferred | Historical plan plus factual G4 closeout |
| [`m8-execution-plan.md`](m8-execution-plan.md) | Planned M8 H2 datagram-fallback sequence (RFC 9298 stream option over H2): gates, risks, stop conditions | Pending plan; update only after a gate passes |
| [`native-udp-payload-policy.md`](native-udp-payload-policy.md) | Frozen M6 1200-byte application/PMTU policy and exact freeze criteria | Frozen; historical |
| [`native-udp-release-guide.md`](native-udp-release-guide.md) | M6 release-candidate configuration, compatibility, observability, troubleshooting, upgrade, rollback, and limitations guide | G6 closed; update if release configuration changes |
| [`product-release-sop.md`](product-release-sop.md) | Cross-repository build, dependency-update, release, provenance, and rollback SOP | Update when release automation or ownership changes |
| [`../tests/m6/platform_qualification.json`](../tests/m6/platform_qualification.json) | Machine-readable G5 platform evidence state; all rows fail closed until exact build/runtime evidence is supplied | Update only from attributable platform results |
| [`m5-execution-plan.md`](m5-execution-plan.md) | Completed M5 G0-G6 sequencing, contracts, test matrix, and verified results | Historical; factual clarifications only |
| [`native-udp-development-plan.md`](native-udp-development-plan.md) | Stable v1 scope, architecture, M0-M6 roadmap, estimates, and release boundary | Update only when scope or milestone boundaries change |
| [`m4-execution-plan.md`](m4-execution-plan.md) | Completed production-server plan and gate record | Historical; factual clarifications only |
| [`m3-execution-plan.md`](m3-execution-plan.md) | Completed production-client plan and gate record | Historical; factual clarifications only |
| [`m1-agy-audit.md`](m1-agy-audit.md), [`m2-agy-audit.md`](m2-agy-audit.md), [`m3-agy-audit.md`](m3-agy-audit.md), [`m4-agy-audit.md`](m4-agy-audit.md), [`m5-agy-audit.md`](m5-agy-audit.md) | Immutable independent review evidence for the revisions named inside each report | Do not rewrite conclusions; append only labeled factual clarification |

This separation is intentional: the status ledger says what is verified, the
active execution plan says what to do next, the development plan defines the
long-lived scope, and historical plans/audits preserve why earlier boundaries
were accepted.

## Read in this order

1. [`current-deployment.md`](current-deployment.md) — current live artifacts
   and runtime facts.
2. [`native-udp-status.md`](native-udp-status.md) — verified milestone ledger
   and commands.
3. [`m7-execution-plan.md`](m7-execution-plan.md) — completed G1-G4 BBR work
   and deferred G5 boundary.
4. [`m6-execution-plan.md`](m6-execution-plan.md) — completed release-hardening
   gates and exit contracts.
5. [`m5-agy-audit.md`](m5-agy-audit.md) and
   [`m5-execution-plan.md`](m5-execution-plan.md) — completed MVP boundary.
6. [`native-udp-development-plan.md`](native-udp-development-plan.md) — frozen
   v1 scope and M6 boundary (complete).
7. [`m4-agy-audit.md`](m4-agy-audit.md) and
   [`m4-execution-plan.md`](m4-execution-plan.md) — server evidence inherited
   by M5.
8. [`m3-agy-audit.md`](m3-agy-audit.md) and
   [`m3-execution-plan.md`](m3-execution-plan.md) — client evidence inherited
   by M5.

Read M1/M2 audit records when changing their specific Chromium tunnel or
SOCKS5 ingress boundaries; they are not required for an unrelated test-harness
change after their constraints are understood.

## Authority rules

When documents disagree, use this order:

1. Current code, tests, Git history, and `release/product.lock.json` for source
   inputs.
2. `current-deployment.md` and its linked manifests for live deployment facts.
3. `native-udp-status.md` for milestone claims already verified.
4. The active milestone execution plan for pending work.
5. `native-udp-development-plan.md` for frozen scope and later milestones.
6. Historical execution plans and audit records for the revisions they name.

Do not duplicate detailed evidence across documents:

- record exact commands, markers, commits, and results in the status ledger;
- keep pending gate design in the active execution plan;
- keep stable scope/milestone summaries and the retained M0/M1 design record
  in the development plan; keep this index concise;
- create an audit record only after an independent review actually runs.

## Ten-minute handoff checklist

Confirm all three worktrees before editing:

```bash
git status -sb
git -C /path/to/naive-forwardproxy-m4 status -sb
git -C /path/to/caddy-naive-udp-m4 status -sb
git log -5 --oneline --decorate
git diff --check
```

Then:

1. Read the status ledger, active M6 gate, and completed M5 audit boundary.
2. Verify the pinned M5 inputs: Naive client closeout `2bb83aec`, forwardproxy
   `8f044e2`, and Caddy `cce894a8`.
3. Inspect the client composition boundaries:

   ```text
   src/net/tools/naive/socks5_udp_association.{h,cc}
   src/net/tools/naive/naive_connect_udp_datagram_backend.{h,cc}
   src/net/tools/naive/naive_connect_udp_tunnel.{h,cc}
   src/net/tools/naive/naive_proxy.{h,cc}
   src/net/tools/naive/naive_proxy_bin.cc
   ```

4. Inspect the server composition boundaries:

   ```text
   /path/to/naive-forwardproxy-m4/native_udp_*.go
   /path/to/naive-forwardproxy-m4/forwardproxy.go
   /path/to/naive-forwardproxy-m4/scripts/test-m4-g5-server.sh
   /path/to/naive-forwardproxy-m4/tests/m4/Caddyfile
   ```

5. Run the existing focused client/server verification commands from the
   status ledger before changing an audited runtime boundary. M6 must preserve
   or explicitly reconsider the completed M5 audit boundary.

## M5 completed boundary

M5-G0 froze the topology, trust, application-probe, and evidence contracts;
G1 proved the first authenticated production-server echo; G2 completed the
addressing, DNS, payload, multiplexing, concurrency, and independent HTTP/3
application matrix; G3 proved authentication/policy failures, admission,
malformed/spoofed input isolation, exact non-QUIC rejection, and cross-layer
privacy. G4 proved control teardown, two server restarts, outer-QUIC recovery,
real client idle, and no replay. G5 proved the shipped binary with the default
certificate verifier, real production server idle, ordinary TCP SOCKS, H3
DATAGRAM evidence, and the v1 no-padding baseline. G6's complete regressions,
three fresh-root repetitions, artifact closeout, and independent G1 rerun pass;
the final review returned `AUDIT_PASS` with zero blocker/high/medium findings.

The deterministic M3 runner remains the broad-matrix fixture because it uses
the real production backend/factory. The separate shipped-`naive` smoke now
passes with `CertVerifier::CreateDefault()` after the QUIC configuration-order
fix in `333b7cb253`. See [`m5-execution-plan.md`](m5-execution-plan.md) for the
exact gate distinction and G6 closeout boundary.
