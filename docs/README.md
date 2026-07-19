# Native UDP Documentation Index

Last updated: 2026-07-19 (Asia/Shanghai)

This directory tracks the design, implementation evidence, and audits for
adding Chromium-network-stack-driven native UDP proxying to NaiveProxy. The
normal upstream README describes the released TCP-focused product. Repository
operating rules for agents are in [`../AGENTS.md`](../AGENTS.md).

## Current handoff

- Branch: `codex/native-udp-foundation`.
- M0-M4 are complete. M1-M4 are independently audited.
- M3 final client marker: `M3_NATIVE_UDP_CLIENT_OK`; closeout commit
  `2bb83aec`.
- M4 final server marker: `M4_NATIVE_UDP_SERVER_OK`; forwardproxy `8f044e2`,
  Caddy `cce894a8`.
- Active milestone: M5 end-to-end MVP. M5-G0 is complete and M5-G1 is next.
- Overall progress remains 5 of 7 milestones (71%), approximately 75-80% by
  weighted engineering scope.
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
| [`m5-execution-plan.md`](m5-execution-plan.md) | Active M5 G0-G6 sequencing, contracts, test matrix, risks, and stop conditions | Update when an M5 design decision changes |
| [`native-udp-development-plan.md`](native-udp-development-plan.md) | Stable v1 scope, architecture, M0-M6 roadmap, estimates, and release boundary | Update only when scope or milestone boundaries change |
| [`m4-execution-plan.md`](m4-execution-plan.md) | Completed production-server plan and gate record | Historical; factual clarifications only |
| [`m3-execution-plan.md`](m3-execution-plan.md) | Completed production-client plan and gate record | Historical; factual clarifications only |
| [`m1-agy-audit.md`](m1-agy-audit.md), [`m2-agy-audit.md`](m2-agy-audit.md), [`m3-agy-audit.md`](m3-agy-audit.md), [`m4-agy-audit.md`](m4-agy-audit.md) | Immutable independent review evidence for the revisions named inside each report | Do not rewrite conclusions; append only labeled factual clarification |

This separation is intentional: the status ledger says what is verified, the
active execution plan says what to do next, the development plan defines the
long-lived scope, and historical plans/audits preserve why earlier boundaries
were accepted.

## Read in this order

1. [`native-udp-status.md`](native-udp-status.md) — current facts and commands.
2. [`m5-execution-plan.md`](m5-execution-plan.md) — current gate and exit
   criteria.
3. [`native-udp-development-plan.md`](native-udp-development-plan.md) — frozen
   v1 scope and remaining M5/M6 boundary.
4. [`m4-agy-audit.md`](m4-agy-audit.md) and
   [`m4-execution-plan.md`](m4-execution-plan.md) — server evidence inherited
   by M5.
5. [`m3-agy-audit.md`](m3-agy-audit.md) and
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
git -C /path/to/naive-forwardproxy-m4 status -sb
git -C /path/to/caddy-naive-udp-m4 status -sb
git log -5 --oneline --decorate
git diff --check
```

Then:

1. Read the status ledger and the complete current M5 gate.
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
   status ledger before changing an audited runtime boundary. M5-G0 may add
   only the new harness skeleton and contract evidence.

## M5 immediate boundary

M5-G0 froze the dynamic-port topology, temporary artifact/trust ownership,
independent SOCKS5-UDP-backed HTTP/3 probe, exact marker list, and safe
production-binary certificate strategy. M5-G1 now owns the first IPv4 echo
from the real M3 production backend to the pinned M4 Caddy/forwardproxy server.
It must not modify Caddy/runtime protocol code or claim shipped-binary trust
evidence yet.

The deterministic M3 runner is valid for the broad M5 matrix because it uses
the real production backend/factory, but M5 completion also requires a
separate smoke through the shipped `naive` binary and its default certificate
verifier. See [`m5-execution-plan.md`](m5-execution-plan.md) for the exact gate
distinction.
