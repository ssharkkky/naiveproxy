# Agent Handoff Guide

This repository is developing native UDP support for NaiveProxy on branch
`codex/native-udp-foundation`. The normal upstream README still describes the
released TCP-focused product; native UDP work is tracked separately under
`docs/`.

## Start here

Read these files in order before changing code:

1. `docs/README.md` — documentation map, authority rules, and handoff checklist.
2. `docs/native-udp-status.md` — verified current state and exact test commands.
3. `docs/m6-execution-plan.md` — the completed M6 G0–G6 release-hardening plan.
4. `docs/m5-agy-audit.md` and `docs/m5-execution-plan.md` — completed M5
   product boundary and independent audit.
5. `docs/native-udp-development-plan.md` — frozen v1 scope and M0–M6 roadmap.
6. `docs/m4-execution-plan.md` and `docs/m4-agy-audit.md` — completed server
   implementation and audit history.
7. `docs/m3-execution-plan.md` and `docs/m3-agy-audit.md` — completed client
   implementation and audit history.

M1 through M5 are complete and independently audited. M4's final production
revisions are `forwardproxy` `8f044e2` and Caddy `cce894a8`; the independent
review returned `AUDIT_PASS` with zero blocker/high/medium findings. The audit's
sole low CI-pinning observation was closed by `8f044e2`. M5's independent
review also returned `AUDIT_PASS` with zero blocker/high/medium findings. The
milestone M6 release hardening is **complete** (all gates G0–G6 closed; release candidate qualified with independent audit `AUDIT_PASS`, marker `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`; native UDP merged to `master` at `fcf3bb36f3`). M6-G0 was complete at `80d37395a6`;
M6-G1 payload/PMTU gate is complete with final marker
`M6_G1_PAYLOAD_PMTU_OK`; M6-G2 is complete at `028d3984d4`, G3's
qualification soak passed with `M6_G3_STRESS_SOAK_OK`, and macOS arm64 G5b is
verified. G5 found and fixed the
production client's post-`Build()`
QUIC-parameter ordering defect in `333b7cb253`; its full M1-M3 and 56-case TCP
owner matrix is green. The controlled QUICHE endpoint remains a test fixture,
not the production server.

M6 currently carries a candidate Caddy race fix at `dd9a89c1` (forwardproxy
build-lock commit `e9663e4`). This supersedes `cce894a8` only for M6
release-candidate work; the immutable M4 audit remains evidence for its named
`cce894a8` range. Post-fix G3/G4 and owner regressions are green; a scoped
independent audit completed in G6 (`3881038645`, `AUDIT_PASS`).

The current forwardproxy qualification head is test-only commit `964281a`
(`9b40eeb` plus TLS-readiness/target follow-up). Native Windows fast run
`30167351024` passed its complete `go test ./...` suite; full run
`30167583501` passed the complete Windows Server 2022 x64 row and emitted
`M6_G5D_WINDOWS_X64_OK`. Android real-device G5e is complete (`cfb42328ac`) and G6 closed the release candidate (`3881038645` → `fcf3bb36f3`).

## Frozen engineering boundaries

- Do not change the existing `NaiveConnection` TCP data path or TCP padding.
- Native UDP v1 is available only through an all-`quic://` proxy chain.
- Native UDP means RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM. Do not add UoT,
  UDP-over-stream, a second QUIC stack, or a private wire protocol.
- Reuse `NaiveConnectUdpTunnel`; do not modify `QuicSessionPool` unless an M3
  stop condition is reached and the plan is explicitly revised.
- The M2 echo backend is test-only. It must never enter the production binary.
- `naive_masque_server` is a controlled interoperability fixture, not the M4
  production Caddy/`forwardproxy` server.
- M4 production changes belong in a separately pinned `forwardproxy`/Caddy
  fork or worktree. Do not vendor that implementation into this repository.
- Preserve the completed M1–M3 client and M4 server as M5's audited inputs. A
  separately justified regression fix must pass the complete owner-specific
  matrix and explicitly reconsider the affected audit boundary.
- Preserve the exact transient `NetworkAnonymizationKey` assigned to the SOCKS
  connection when constructing its M3 tunnel backend. Chromium proxy
  credentials are not NAK-partitioned; test NAK propagation and cached Basic
  authentication as separate properties.
- Do not log UDP payloads or destinations. Use redacted, rate-limited counters
  and NetLog state/error events.
- Do not replay a datagram after an ambiguous tunnel write/session failure.
- The deterministic M3 runner may bypass a local test certificate, but the M5
  production-binary gate must use `CertVerifier::CreateDefault()`. Never add a
  shipped certificate-bypass switch.
- The shipped client must configure forced QUIC origins before
  `URLRequestContextBuilder::Build()`; `QuicSessionPool` copies those params at
  construction. Keep `333b7cb253` inside the M5/G6 client audit boundary.
- M5 must include an independent SOCKS5-UDP-backed HTTP/3 application probe;
  generic UDP echo alone is not product-level application evidence.

## Working-tree safety

- The checkout may contain unrelated untracked `.DS_Store` and `src/tmp/`
  entries. Do not stage, delete, or modify them as part of native UDP work.
- Stage explicit paths. Do not use `git add -A` in this mixed worktree.
- Keep each future gate as a small green-to-green commit in the repository
  that owns the change, followed by a factual status-ledger update here.
- Treat the status ledger as verified evidence, but inspect the current code
  and diff before relying on a historical statement.

## Required next-milestone loop

1. Confirm all three recorded repositories and branches with `git status -sb`
   before editing.
2. Read the active gate, contracts, risks, and stop conditions in
   `docs/m6-execution-plan.md`, plus the completed M5 audit boundary, before
   changing a runtime boundary.
3. Make the narrowest client, server, or test-harness change needed for one
   gate; do not mix unrelated cross-repository changes into one commit.
4. Build the affected Release/server targets and run the focused gate tests.
5. Keep all completed M1–M5 markers, all 56 Naive TCP cases, server legacy and
   privacy regressions, and `git diff --check` green.
6. Update `docs/native-udp-status.md` only with verified commands, markers, and
   exact commits; update the roadmap only when a milestone boundary changes.
7. Treat the M5 plan and audit as historical evidence. Stage explicit intended
   paths and commit one green M6 gate at a time.

The canonical client and server build/verification commands live in
`docs/native-udp-status.md`. Treat `docs/m4-agy-audit.md` and
`docs/m5-agy-audit.md` as immutable evidence for their reviewed ranges; any
later client/server runtime change requires scoped regression and audit
reconsideration.

## Current baseline commits

- `e11a7733` — complete native UDP M1 foundation.
- `fe817a87` — complete SOCKS5 UDP ingress M2.
- `8720c912` — plan native UDP M3 execution.
- `83904eb8` through `578e3992` — M3 G0–G5 implementation and hardening.
- `2bb83aec` — complete M3 regressions, independent audit, and closeout.
- `9ec8fff82c` — complete M4 local verification record in this repository.
- `4ec0f8bb9a` — complete M4 audit record and handoff to M5.
- `7243519` — audited M4 server implementation; `8f044e2` closes the audit's
  CI-only Caddy pin finding.
- `cce894a8` — final audited Caddy H3 Datagram/privacy patch stack.
- `dd9a89c1` — M6 Caddy TLS module race fix; pending scoped post-fix audit.
- `333b7cb253` — configure production QUIC origins before context build.
- `c73b5a486f` — complete M5 G4/G5 lifecycle and shipped-binary gates.
- `eaf172d971` — audited M5 local closeout revision.
- `e70ee79e05` — complete M5 MVP audit/finalizer record.
- `80d37395a6` — plan M6 and complete its release-contract/environment gate.
- `1870779147` — begin M6-G1 with deterministic live-ceiling transition tests.
- `9c72a7da08` — measure the live product ceiling and IPv6-minimum PMTU behavior.
- `028d3984d4` — complete the seeded M6 network-impairment product matrix.
- `e93c8e5aa6` — complete macOS arm64 M6 qualification (G5b).
- `9f10b14f2e` — complete native Windows x64 qualification (G5d, `M6_G5D_WINDOWS_X64_OK`).
- `cfb42328ac` — complete Android arm64 real-device qualification (G5e).
- `3881038645` / `fcf3bb36f3` — M6-G6 release-candidate matrix, independent audit
  (`AUDIT_PASS`, `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`), and closeout; merged to `master`.

If repository state has advanced beyond these commits, trust the current Git
history and the newest status-ledger update rather than this snapshot.

## Follow-on work (post-M6, not yet implemented)

- **Performance: CUBIC → BBR.** Native UDP on a lossy high-RTT path is
  CUBIC-capped (~50–100 KB/s per session) because the outer QUIC uses CUBIC on
  both ends and CUBIC collapses its window under loss; Hy2 (BBR, `standard`
  profile) reaches ~700–800 KB/s on the same path. Planned fix: client enables
  in-tree Chromium BBR (`kTBBR` v1 / `kB2ON` v2) behind a `quic_congestion`
  config option; server ports Hy2's Go BBR into a minimal `quic-go` fork
  (selectable profile, default `standard`). Any implementation is a client/server
  runtime change subject to the owner-matrix regression + audit-reconsideration
  rules above. (Detailed plan: local `bbr-server-plan.md`, untracked.)
