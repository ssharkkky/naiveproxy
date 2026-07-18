# Agent Handoff Guide

This repository is developing native UDP support for NaiveProxy on branch
`codex/native-udp-foundation`. The normal upstream README still describes the
released TCP-focused product; native UDP work is tracked separately under
`docs/`.

## Start here

Read these files in order before changing code:

1. `docs/README.md` — documentation map, authority rules, and handoff checklist.
2. `docs/native-udp-status.md` — verified current state and exact test commands.
3. `docs/m3-execution-plan.md` — the active milestone and sequential G0–G6
   gates.
4. `docs/native-udp-development-plan.md` — v1 scope and the M0–M6 roadmap.

The next implementation target is M3-G0. M1 and M2 are complete, audited, and
must remain green.

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
- Keep each M3 gate as a small green-to-green commit.
- Treat the status ledger as verified evidence, but inspect the current code
  and diff before relying on a historical statement.

## Required gate loop

For each M3 gate:

1. Confirm the branch and worktree with `git status -sb`.
2. Read the gate, exit criteria, risks, and stop conditions in
   `docs/m3-execution-plan.md`.
3. Make the narrowest implementation and test changes needed for that gate.
4. Build the affected Release targets and run the focused gate tests.
5. Rerun all earlier M1/M2 markers, all 56 TCP regressions, and
   `git diff --check` before declaring the gate complete.
6. Update `docs/native-udp-status.md` with commands, markers, and verified
   evidence. Update the execution plan only if a design decision changes.
7. Commit only the gate's intended files.

The canonical build and verification commands live in
`docs/native-udp-status.md`. M3-G6 additionally requires one continuing,
read-only `agy` audit session and a final `AUDIT_PASS` with no blocker, high,
or medium finding.

## Current baseline commits

- `e11a7733` — complete native UDP M1 foundation.
- `fe817a87` — complete SOCKS5 UDP ingress M2.
- `8720c912` — plan native UDP M3 execution.

If repository state has advanced beyond these commits, trust the current Git
history and the newest status-ledger update rather than this snapshot.
