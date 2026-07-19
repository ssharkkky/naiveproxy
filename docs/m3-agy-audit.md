# M3 Independent `agy` Audit

Date: 2026-07-19 (Asia/Shanghai)

Scope: the complete committed M3 native UDP client implementation on
`codex/native-udp-foundation`, diff `c6ec957f..578e3992`. Unrelated untracked
`.DS_Store` and `src/tmp/` entries were explicitly excluded.

## Audit method

A fresh non-interactive `agy -p` session used Gemini 3.1 Pro High with
`--dangerously-skip-permissions` and a 30-minute print timeout. The reviewer
was required to remain read-only, inspect the actual committed diff rather
than trust the status ledger, derive every G0–G6 exit criterion, and return
`AUDIT_PASS` only if no blocker, high, or medium finding remained.

The review covered:

- the pre-`NaiveConnection` SOCKS UDP branch, exact `0x01` failures, and real
  `BND.ADDR/BND.PORT`;
- exact transient NAK propagation and all-QUIC-only eligibility;
- production factory versus fake-runner isolation;
- target routing, original endpoint framing, callback/generation ownership,
  synchronous yielding, and posted retirement;
- stream-close notification, empty datagrams, payload ceiling, timeout, idle,
  cooldown, failure isolation, and no replay;
- target/packet/byte/association/response limits;
- cached Basic auth and explicit 407 behavior;
- `kEverything` NetLog path/payload privacy and powers-of-two counters;
- unchanged TCP behavior and absence of UoT, a second QUIC stack, M4 server
  work, or production certificate weakening.

## Independently rerun verification

The reviewer rebuilt the named Release targets and independently ran:

- all M1 scripts;
- `tests/socks5_udp_m2.sh`;
- `tests/socks5_udp_m3.sh`;
- all 56 cases from `tests/basic.sh`;
- `git diff --check` and a final worktree/artifact inspection.

## Findings

- Blocker: 0.
- High: 0.
- Medium: 0.
- Low: 1. The frozen `Socks5UdpBackendLimits::kMaxTargets == 32` cap could be
  small for an aggressive multi-target client, but the implementation handles
  pressure as bounded graceful drops, so the reviewer accepted it for v1.

The external report stated that the NAK reaches the request/auth controller.
For precision, Chromium proxy credentials are not NAK-partitioned in this
project: exact NAK propagation and cached Basic authentication are verified as
separate properties, as required by the M3 plan.

## Result

The reviewer concluded that every M3 boundary holds and that the existing TCP
pipeline is not polluted by the UDP implementation.

Final verdict:

```text
AUDIT_PASS
Zero blocker, high, or medium findings.
```

M3 is therefore complete. Production Caddy/`forwardproxy` CONNECT-UDP remains
the separate M4 milestone.
