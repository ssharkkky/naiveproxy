# Native UDP Documentation Index

Last updated: 2026-07-19 (Asia/Shanghai)

This directory contains the design, execution, status, and audit record for
adding Chromium-network-stack-driven native UDP proxying to NaiveProxy.
Repository-level agent operating rules are in [`../AGENTS.md`](../AGENTS.md).

## Current handoff snapshot

- Branch: `codex/native-udp-foundation`.
- M0: complete.
- M1 Chromium CONNECT-UDP integration: complete, audited, commit `e11a7733`.
- M2 SOCKS5 UDP ingress: complete, audited, commit `fe817a87`.
- M3 native UDP client composition: complete and independently audited;
  `agy` returned `AUDIT_PASS` with zero blocker/high/medium findings.
- M3 plan commit: `8720c912`.
- M3 implementation commits: G0 `83904eb8`, G1 `4541f756`, G2 `1bd5789e`,
  G3 `c2352710`, G4 `4927d06a`, G5 `578e3992`.
- M3 final regression/audit closeout commit: `2bb83aec`.
- M3 audit record: [`m3-agy-audit.md`](m3-agy-audit.md).
- M4 production-server execution plan: [`m4-execution-plan.md`](m4-execution-plan.md).
- M4-G0 server baseline commit: `bf092e6` in
  `ssharkkky/forwardproxy` branch `codex/native-udp-server`.
- M4-G1 capability commit: `121f097`; Caddy Datagram commits `2002a520` and
  `2ff83e69` in `ssharkkky/caddy` branch `codex/enable-h3-datagrams`.
- M4-G2 protocol/policy commit: `f9b40f6`.
- M4-G3 bounded association commit: `1b6d04b`.
- M4-G4 production integration commit: `15c07ab`.
- M4-G5 pinned-binary interoperability commit: `7243519`; Caddy debug-secret
  redaction commit: `cce894a8`.
- M4-G6 local closeout: green; independent `agy` run was user-terminated before
  a verdict. Next action: independent audit only.
- Unrelated untracked `.DS_Store` and `src/tmp/` entries must remain outside
  feature commits.

## Read in this order

1. [`native-udp-status.md`](native-udp-status.md)
   - Current verified facts, milestone ledger, evidence, and canonical test
     commands.
   - This is the operational source of truth.
2. [`m4-execution-plan.md`](m4-execution-plan.md)
   - Active M4 production-server G0–G6 sequence, contracts, decision gates,
     verification matrix, source ownership, and stop conditions.
3. [`native-udp-development-plan.md`](native-udp-development-plan.md)
   - Frozen v1 scope, architecture, M0–M6 roadmap, verification matrix, and
     later server/release milestones.
   - Use its M4 section for the next project boundary.
4. [`m3-execution-plan.md`](m3-execution-plan.md)
   - Completed G0–G6 sequence, ownership model, contracts, resource limits,
     stop conditions, and completion markers.
5. [`m1-agy-audit.md`](m1-agy-audit.md),
   [`m2-agy-audit.md`](m2-agy-audit.md), and
   [`m3-agy-audit.md`](m3-agy-audit.md)
   - Historical independent audit evidence for completed milestones.
   - They are immutable evidence except for clearly labeled factual
     clarifications.

## Authority and update rules

When documents disagree, use this order:

1. Current code, tests, and Git history.
2. `native-udp-status.md` for claims already verified.
3. The active milestone execution plan for work not yet completed.
4. `native-udp-development-plan.md` for frozen scope and later roadmap.
5. Historical audit records for the state reviewed at that time.

Do not copy the same detailed evidence into every document:

- update the status ledger after a gate actually passes;
- update the active execution plan only when sequencing, contracts, limits, or
  risks change;
- update the development plan only when v1 scope or milestone boundaries
  change;
- add an audit record only after an independent audit has actually run.

## Ten-minute new-agent checklist

From the repository root:

```bash
git status -sb
git log -5 --oneline --decorate
git diff --check
```

Then:

1. Confirm the branch is `codex/native-udp-foundation` and identify any local
   changes before editing.
2. Read this index, the status ledger, the active M4 execution plan, and the
   M3 audit record before planning the next change.
3. Inspect the actual M2 factory and association boundary:

   ```text
   src/net/tools/naive/socks5_udp_datagram_backend.h
   src/net/tools/naive/socks5_udp_association.{h,cc}
   src/net/tools/naive/naive_proxy.{h,cc}
   ```

4. Inspect the actual M1 tunnel boundary:

   ```text
   src/net/tools/naive/naive_connect_udp_tunnel.{h,cc}
   src/net/tools/naive/naive_quic_proxy_stream_request.{h,cc}
   src/net/quic/quic_proxy_datagram_client_socket.{h,cc}
   ```

5. Run the existing focused build/tests from the status ledger before making
   a structural change. The current local workspace has prepared Release output
   under `src/out/Release`; a fresh clone must build it first.

## Current M3 implementation summary

M3 is a composition layer:

```text
SOCKS5 UDP relay (M2)
  -> one production backend per SOCKS association
  -> target-keyed tunnel owners (M3)
  -> one NaiveConnectUdpTunnel per destination (M1)
  -> Chromium CONNECT-UDP + HTTP/3 DATAGRAM
```

G0 through G5 have now resolved the composition, interoperability, and
lifecycle boundary:

- the exact per-association transient NAK and immutable transport context reach
  a target-keyed, bounded production backend;
- the backend has deterministic synchronous/asynchronous, routing, limits,
  cooldown, failure-isolation, empty-datagram, MTU, and destruction tests;
- production `naive` and a test-only full-path runner share the same real M1
  adapter/factory;
- direct, HTTPS/H2, mixed-chain, and no-backend UDP requests return exact SOCKS
  reply `0x01`;
- controlled IPv4 echo and cached Basic authentication pass through real
  CONNECT-UDP plus HTTP/3 DATAGRAM;
- controlled IPv6, domain, DNS, same-association multi-target, and concurrent
  association traffic pass through the same production adapter;
- even `kEverything` NetLog capture redacts CONNECT-UDP paths and records only
  byte counts, never UDP destinations or payload bytes;
- real session shutdown and target idle expiry produce fresh tunnels only for
  later packets, while deterministic coverage proves ambiguous old payloads
  are never replayed;
- pending connect/read/write destruction, explicit 407 failures, connect
  timeout, zero-length/oversize behavior, every frozen capacity limit, and
  powers-of-two NetLog rate limiting are verified;
- every M1/M2 gate and all 56 existing TCP cases remain green.

G6 reran the complete regression matrix, repeated the full M3 lifecycle suite
three consecutive times, and obtained an independent Gemini 3.1 Pro High
`AUDIT_PASS` with zero blocker, high, or medium findings. M3 is complete; the
next architectural boundary is the separate M4 production server path.

## Current M4 implementation summary

M4-G0 through G5 are complete in the separate forwardproxy/Caddy forks. The
pinned Caddy enables both HTTP/3 and QUIC Datagrams; forwardproxy now strictly
classifies RFC 9298 requests, shares TCP DNS/ACL/allowed-port policy, owns a
bounded fixed-target connected UDP association, and relays canonical Context
ID `0` H3 Datagrams without private framing or replay.

G4 verified real H3 IPv4, IPv6, domain, authentication, policy status, upstream
rejection, and probe-resistance behavior. CONNECT-UDP paths are redacted before
Caddy access logging, and powers-of-two counters contain only association id,
generic reason, count, and byte count. The complete legacy server suite and
race detector remain green. G5 then proved the same contracts through the
standalone pinned production binary, including deterministic DNS, zero/live
maximum/oversize behavior, 32-stream admission, production idle expiry,
active shutdown/restart, repeated stress, and server-log privacy. G6 clean
rebuild and full client/server regressions also pass. M4 remains open solely
because the independent audit was stopped before returning `AUDIT_PASS`.
