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
- M3 native UDP client composition: G0–G3 complete and locally verified.
- M3 plan commit: `8720c912`.
- M3 implementation commits: G0 `83904eb8`, G1 `4541f756`, G2 `1bd5789e`.
- Next gate: M3-G4, controlled IPv4/IPv6/domain/DNS/auth/multi-target
  interoperability.
- Unrelated untracked `.DS_Store` and `src/tmp/` entries must remain outside
  feature commits.

## Read in this order

1. [`native-udp-status.md`](native-udp-status.md)
   - Current verified facts, milestone ledger, evidence, and canonical test
     commands.
   - This is the operational source of truth.
2. [`m3-execution-plan.md`](m3-execution-plan.md)
   - Active G0–G6 implementation sequence, ownership model, contracts,
     resource limits, stop conditions, and completion markers.
   - This is the source of truth for the next change.
3. [`native-udp-development-plan.md`](native-udp-development-plan.md)
   - Frozen v1 scope, architecture, M0–M6 roadmap, verification matrix, and
     later server/release milestones.
   - Use it for project boundaries, not for the latest gate status.
4. [`m1-agy-audit.md`](m1-agy-audit.md) and
   [`m2-agy-audit.md`](m2-agy-audit.md)
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
2. Read this index, the status ledger, and M3-G4 in the execution plan.
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

G0 through G3 have now resolved the composition boundary:

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
- every M1/M2 gate and all 56 existing TCP cases remain green.

G4 broadens controlled interoperability to IPv6, domain, DNS, multi-target,
and concurrent associations. The complete gate definitions and markers are in
[`m3-execution-plan.md`](m3-execution-plan.md).
