# M2 Independent `agy` Audit

Date: 2026-07-19 (Asia/Shanghai)

Scope: the complete uncommitted M2 SOCKS5 UDP ingress change on
`codex/native-udp-foundation`, excluding unrelated `.DS_Store` and `src/tmp/`
entries.

## Audit method

One continuing `agy` session was started with
`--dangerously-skip-permissions` and a 30-minute print timeout. The reviewer
was instructed not to edit the repository, to inspect the actual diff rather
than rely on the status document, and to return `AUDIT_PASS` only when no
blocker, high, or medium finding remained and every M2 exit criterion was met.

The audit independently inspected:

- RFC 1928 codec and wire replies;
- two-stage SOCKS5 handshake and `NaiveProxy` command branch point;
- pending-handshake, association, callback, and deferred-destruction
  ownership;
- relay binding and IPv4/IPv6 `BND.ADDR/BND.PORT` behavior;
- source IP/port authorization and wildcard-port learning;
- synchronous I/O yielding and synchronous backend callback reentrancy;
- queue and log-amplification bounds;
- test-only fake backend isolation from the production `naive` binary.

## Independently rerun verification

The reviewer rebuilt all named M1/M2 targets and reran:

- `masque_g1_smoke.sh` — `G1_MASQUE_SMOKE_OK`;
- `masque_g2_naive_tunnel.sh` — `G2_NAIVE_TUNNEL_OK`;
- `masque_g3_basic_auth.sh` — `G3_BASIC_AUTH_OK`;
- `masque_g5_lifecycle.sh` — `G5_LIFECYCLE_OK`;
- `socks5_udp_m2.sh` — `M2_SOCKS5_UDP_INGRESS_OK`;
- `basic.sh` — all 56 existing TCP cases;
- `git diff --check`.

## Result

The final report found no blocker, high, or medium issue. Its low observations
confirmed that malformed/fragmented input cannot pin a wildcard source port
and that the post-`Send()` `finished_` guard correctly handles synchronous
backend reentrancy.

Final verdict:

```text
AUDIT_PASS
```

M2 is therefore complete. The real Chromium CONNECT-UDP backend remains an
explicit M3 task; this audit does not claim that production UDP forwarding is
already exposed.
