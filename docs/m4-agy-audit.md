# M4 Independent `agy` Audit

Date: 2026-07-19 (Asia/Shanghai)

Scope: the complete committed M4 native UDP production-server work across the
NaiveProxy documentation repository, the `forwardproxy` server fork, and the
Caddy Datagram patch fork. Unrelated untracked `.DS_Store` and `src/tmp/`
entries in the NaiveProxy checkout were explicitly excluded.

## Audited revisions

| Repository | Branch | Audited HEAD | Audited diff |
| --- | --- | --- | --- |
| NaiveProxy | `codex/native-udp-foundation` | `9ec8fff82c` | `fa7a1c2dfa..9ec8fff82c` |
| `forwardproxy` | `codex/native-udp-server` | `7243519fdf` | `d62c80d..7243519` |
| Caddy | `codex/enable-h3-datagrams` | `cce894a8a0` | `ffb6ab06..cce894a8` |

The audit verified that the NaiveProxy M4 diff was documentation-only, the
`forwardproxy` and Caddy worktrees were clean, and all three audited diffs
passed `git diff --check`.

## Audit method

A separate, user-run Antigravity (`agy`) session using Claude Opus 4.6
Thinking operated read-only. It inspected the actual committed diffs and
critical source, test, build, and configuration files rather than accepting
the status ledger as proof. It also cross-checked the recorded G0-G6 test
evidence against the implementation and test harnesses.

The review covered:

- strict HTTP/3 Extended CONNECT classification, RFC 9298 URI-template
  parsing, canonical Context ID `0`, and absence of Capsules, UoT, DATA
  fallback, CONNECT-IP, or private framing;
- shared authentication, DNS, ACL, allowed-port, selected-address, upstream,
  probe-resistance, and legacy TCP behavior;
- fixed-target association ownership, admission limits, idle expiry,
  cancellation, goroutine joining, fair yielding, oversize handling, and the
  no-replay rule;
- request-path, destination, payload, credential, and encoded-credential
  privacy across `forwardproxy` and Caddy debug/access logging;
- the pinned Go, xcaddy, Caddy, quic-go, and `forwardproxy` build inputs and
  the consistency of the recorded interoperability/regression evidence.

The reviewer independently rejected one proposed IPv6-bracket finding: the
RFC 9298 default template places `{target_host}` in a path segment, where
Chromium correctly sends `%3A%3A1`, not authority-style brackets.

## Findings

- Blocker: 0.
- High: 0.
- Medium: 0.
- Low: 1.

The low finding was that `.github/workflows/build.yml` in `forwardproxy`
pinned Caddy commit `2ff83e69`, while the audited local build script and
`go.mod` correctly pinned privacy-fixed commit `cce894a8`. CI-produced binaries
could therefore omit the Caddy debug-log redaction patch even though the
documented local production build was unaffected.

This finding was closed immediately after the read-only audit by
`forwardproxy` commit `8f044e2` (`Pin CI to privacy-fixed Caddy`), a one-line
workflow-only change that updates the Caddy ref to
`cce894a8a0e987eb1722cf99729499bdaba6c38d`. Runtime server source and the
audited protocol/lifecycle implementation were not changed.

## Result

The reviewer concluded that all five M4 security and correctness boundaries
hold: protocol, policy/authentication, lifecycle/resources, privacy, and build
integrity.

Final verdict:

```text
AUDIT_PASS
Zero blocker, high, or medium findings.
```

M4 is therefore complete. The frozen production revisions for M5 are
`forwardproxy` `8f044e2` and Caddy `cce894a8`; full SOCKS5-to-Naive-client-to-
production-Caddy validation remains the separate M5 milestone.
