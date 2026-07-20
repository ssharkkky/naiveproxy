# Native UDP Documentation Index

Last updated: 2026-07-20 (Asia/Shanghai)

This directory tracks the design, implementation evidence, and audits for
adding Chromium-network-stack-driven native UDP proxying to NaiveProxy. The
normal upstream README describes the released TCP-focused product. Repository
operating rules for agents are in [`../AGENTS.md`](../AGENTS.md).

## Current handoff

- Branch: `codex/native-udp-foundation`.
- M0-M5 are complete. M1-M5 are independently audited.
- M3 final client marker: `M3_NATIVE_UDP_CLIENT_OK`; closeout commit
  `2bb83aec`.
- M4 final server marker: `M4_NATIVE_UDP_SERVER_OK`; forwardproxy `8f044e2`,
  Caddy `cce894a8`.
- M5 final marker: `M5_NATIVE_UDP_MVP_OK`; audited client revision
  `eaf172d971`.
- Active milestone: M6 hardening and release-candidate qualification.
- M6-G0 release contract/environment marker: `M6_G0_CONTRACT_OK`; commit
  `80d37395a6`.
- Active M6 gates: G5b-G5f platform qualification and G6 release closeout.
  G1-G4 are complete. G5b is being requalified after a forced-SOCKS TCP probe
  exposed a forwardproxy padding-negotiation defect; owner fix `baa7f2dd` is
  pinned for every new platform run. Caddy race fix `dd9a89c1` passed owner
  regressions and the frozen G4 rerun. G4 implementation commit is
  `5893f97f6e`. G5a's fail-closed
  platform evidence schema is complete at `9869f1d6d1`; all corrected platform
  runtime rows remain fail-closed until rerun.
- Overall progress remains 6 of 7 milestones (86%), approximately 93-95% by
  weighted engineering scope. This is not a production-release claim while
  G5 and G6 evidence remains open.
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
| [`native-udp-status.md`](native-udp-status.md) | Operational source of truth: verified state, evidence, commands, markers, and exact commits | Update only after a gate actually passes |
| [`m6-execution-plan.md`](m6-execution-plan.md) | Active M6 G0-G6 release-hardening sequence, release blockers, platform matrix, risks, and stop conditions | Update while M6 sequencing or contracts change |
| [`native-udp-payload-policy.md`](native-udp-payload-policy.md) | Candidate M6 1200-byte application/PMTU policy and exact freeze criteria | Keep `candidate` until G1b2/G1d and platform evidence close |
| [`native-udp-release-guide.md`](native-udp-release-guide.md) | Candidate M6 configuration, compatibility, observability, troubleshooting, upgrade, rollback, and limitations guide | Keep draft until G6 release/audit closeout |
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

1. [`native-udp-status.md`](native-udp-status.md) — current facts and commands.
2. [`m6-execution-plan.md`](m6-execution-plan.md) — active release-hardening
   gate and exit contracts.
3. [`m5-agy-audit.md`](m5-agy-audit.md) and
   [`m5-execution-plan.md`](m5-execution-plan.md) — completed MVP boundary.
4. [`native-udp-development-plan.md`](native-udp-development-plan.md) — frozen
   v1 scope and remaining M6 boundary.
5. [`m4-agy-audit.md`](m4-agy-audit.md) and
   [`m4-execution-plan.md`](m4-execution-plan.md) — server evidence inherited
   by M5.
6. [`m3-agy-audit.md`](m3-agy-audit.md) and
   [`m3-execution-plan.md`](m3-execution-plan.md) — client evidence inherited
   by M5.

Read M1/M2 audit records when changing their specific Chromium tunnel or
SOCKS5 ingress boundaries; they are not required for an unrelated test-harness
change after their constraints are understood.

## Authority rules

When documents disagree, use this order:

1. Current code, tests, and Git history.
2. `native-udp-status.md` for claims already verified.
3. The active milestone execution plan for pending work.
4. `native-udp-development-plan.md` for frozen scope and later milestones.
5. Historical execution plans and audit records for the revisions they name.

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
git -C /Users/stoneshi/Documents/naive-forwardproxy-m4 status -sb
git -C /Users/stoneshi/Documents/caddy-naive-udp-m4 status -sb
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
   /Users/stoneshi/Documents/naive-forwardproxy-m4/native_udp_*.go
   /Users/stoneshi/Documents/naive-forwardproxy-m4/forwardproxy.go
   /Users/stoneshi/Documents/naive-forwardproxy-m4/scripts/test-m4-g5-server.sh
   /Users/stoneshi/Documents/naive-forwardproxy-m4/tests/m4/Caddyfile
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
