# Agent Handoff Guide

This repository is developing native UDP support for NaiveProxy on branch
`codex/native-udp-foundation`. The normal upstream README still describes the
released TCP-focused product; native UDP work is tracked separately under
`docs/`.

## Start here

Read these files in order before changing code:

1. `docs/README.md` — documentation map, authority rules, and handoff checklist.
2. `docs/native-udp-status.md` — verified current state and exact test commands.
3. `docs/m4-execution-plan.md` — the active production-server G0–G6 plan.
4. `docs/native-udp-development-plan.md` — v1 scope and the M0–M6 roadmap.
5. `docs/m3-execution-plan.md` — the completed M3 G0–G6 implementation record.
6. `docs/m3-agy-audit.md` — independent final M3 audit evidence.

M1, M2, and M3 are complete and independently audited. M4-G0 through M4-G5
are complete in the separate server/Caddy repositories. M4-G6 local closeout
is green: reproducible builds, complete client/server regressions, and artifact
inventory passed. The independent `agy` run was terminated at the user's
request before it returned a verdict, so M4 remains open. The next action is a
fresh or resumed read-only audit ending in `AUDIT_PASS`; do not rerun the local
matrix unless code or dependencies change. The controlled QUICHE endpoint
remains a test fixture, not that server implementation.

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
- Preserve the completed M1–M3 client as M4's interoperability oracle. A
  separately justified regression fix must pass the complete client matrix.
- Preserve the exact transient `NetworkAnonymizationKey` assigned to the SOCKS
  connection when constructing its M3 tunnel backend. Chromium proxy
  credentials are not NAK-partitioned; test NAK propagation and cached Basic
  authentication as separate properties.
- Do not log UDP payloads or destinations. Use redacted, rate-limited counters
  and NetLog state/error events.
- Do not replay a datagram after an ambiguous tunnel write/session failure.

## Working-tree safety

- The checkout may contain unrelated untracked `.DS_Store` and `src/tmp/`
  entries. Do not stage, delete, or modify them as part of native UDP work.
- Stage explicit paths. Do not use `git add -A` in this mixed worktree.
- Keep each M4 gate as a small green-to-green commit in the server repository,
  followed by a factual status-ledger update here.
- Treat the status ledger as verified evidence, but inspect the current code
  and diff before relying on a historical statement.

## Required gate loop

For each M4 gate:

1. Confirm this repository and the recorded server repository branch/worktree
   with `git status -sb` before editing.
2. Read the gate, exit criteria, contracts, risks, and stop conditions in
   `docs/m4-execution-plan.md`.
3. Make the narrowest implementation and test changes needed for that gate in
   the server repository; keep this repository documentation-only during M4
   unless a separately justified client regression fix is required.
4. Build the pinned server tuple and run the focused gate tests plus all
   existing server TCP/probe-resistance regressions.
5. At G5/G6, rerun the complete M1–M3 client markers, all 56 Naive TCP
   regressions, the independent server interoperability matrix, and
   `git diff --check` in both repositories. If client source changes earlier,
   run the complete client matrix at every such change.
6. Update `docs/native-udp-status.md` with exact server commits, commands,
   markers, and verified evidence. Update the execution plan only if a design
   decision changes.
7. Commit only each gate's intended paths; never mix server changes into the
   NaiveProxy client commit.

The current client build and verification commands live in
`docs/native-udp-status.md`. M4-G0 must add the canonical pinned server build
and test commands. M4-G6 requires a continuing, read-only `agy` audit over both
server diffs and the interoperability evidence, ending in `AUDIT_PASS` with no
blocker, high, or medium finding.

## Current baseline commits

- `e11a7733` — complete native UDP M1 foundation.
- `fe817a87` — complete SOCKS5 UDP ingress M2.
- `8720c912` — plan native UDP M3 execution.
- `83904eb8` through `578e3992` — M3 G0–G5 implementation and hardening.
- `2bb83aec` — complete M3 regressions, independent audit, and closeout.

If repository state has advanced beyond these commits, trust the current Git
history and the newest status-ledger update rather than this snapshot.
