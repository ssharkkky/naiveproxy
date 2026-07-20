# M5 Independent `agy` Audit

Date: 2026-07-20 (Asia/Shanghai)

Scope: the complete committed M5 native UDP end-to-end product work across
the NaiveProxy client repository, the pinned `forwardproxy` production-server
fork plus M5-only fixtures, and the pinned Caddy HTTP/3 Datagram fork.
Unrelated untracked `.DS_Store` and `src/tmp/` entries were excluded.

## Audited revisions

| Repository | Branch | Audited HEAD/runtime | Audited range |
| --- | --- | --- | --- |
| NaiveProxy | `codex/native-udp-foundation` | `eaf172d971` | `cd9a676df9..eaf172d971` |
| `forwardproxy` | `codex/native-udp-server` | fixtures through `2b2a8ea`; runtime base `8f044e2` | runtime drift check `8f044e2..2b2a8ea` |
| Caddy | `codex/enable-h3-datagrams` | `cce894a8a0e987eb1722cf99729499bdaba6c38d` | exact pinned M4 patch stack |

The review specifically included production-client fix `333b7cb253`, G4/G5
implementation `c73b5a486f`, G6 runner commits `4a395a7f4e` and `d1aee3663f`,
and local closeout record `eaf172d971`.

## Audit method

A continuing non-interactive `agy -p` session used Gemini 3.1 Pro High with
permission prompts disabled and a 45-minute print timeout. The first prompt
used broad security-audit language; Gemini declined before performing a code
review. The same conversation then received an explicitly authorized,
defensive release-quality prompt excluding vulnerability hunting, penetration
testing, exploit analysis, or bypass guidance. That second request performed
the review and produced the verdict recorded below.

The reviewer operated read-only. It inspected the actual Git ranges and
critical source/test files, cross-checked repository ownership and dependency
pins, and evaluated the already completed G6 evidence. It did not rerun the
privileged G5 trust mutation or the long full matrix. The complete long client,
product, fresh-root, server-idle, normal/race, and focused-Caddy matrices were
run locally before the review and are recorded in
[`native-udp-status.md`](native-udp-status.md).

In a final same-session addendum, the reviewer independently ran the required
short, non-privileged `tests/m5/g1_cross_repo_echo.sh` smoke. It exited `0`
with `M5_G1_IPV4_ECHO_BYTES_OK`, `M5_G1_AUTHENTICATED_CONNECT_UDP_OK`,
`M5_G1_H3_DATAGRAM_EVIDENCE_OK`, `M5_G1_LOG_PRIVACY_OK`, and
`M5_G1_CROSS_REPO_ECHO_OK`. Post-run status checks found no review-created
change in any repository; NaiveProxy retained only the explicitly excluded
pre-existing untracked entries.

## Reviewed boundaries

The independent review confirmed:

- standard RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM with Context ID `0`, with
  no UoT, Capsules data path, private framing, UDP-over-stream fallback,
  second QUIC stack, or ambiguous datagram replay;
- no change to `NaiveConnection`, the TCP data mover, or TCP padding in M5;
- `333b7cb253` installs the existing forced-origin/RFCv1 `QuicContext` before
  `URLRequestContextBuilder::Build()`, so `QuicSessionPool` sees the params,
  while production continues to use `CertVerifier::CreateDefault()`;
- forwardproxy runtime source does not drift after `8f044e2`; the +116-line
  M5 delta through `2b2a8ea` is confined to `tests/m5/` fixtures;
- the independent Go SOCKS5 UDP `net.PacketConn`/quic-go HTTP/3 probe imports
  no Naive tunnel implementation;
- NetLog/server redaction and the encrypted size tap retain neither targets,
  request paths, payloads, nor credentials. The tap has only size, direction,
  relative time, and connection age;
- exact Go 1.25.12, xcaddy 0.4.5, Caddy `cce894a8`, quic-go 0.59.0,
  forwardproxy runtime `8f044e2`, and fixture `2b2a8ea` pins;
- the status ledger accurately discloses the split G6 execution and the
  module-cwd-only harness correction rather than claiming a nonexistent
  single post-fix all-in-one run.

Trust-fixture clarification: the G0 `macos_trust_fixture.sh` exercises the
user-domain `security` primitive with an isolated temporary keychain. The G5
production smoke instead installs its generated root in the current user's
login keychain so the shipped Chromium default verifier can see it, then
removes the trust and exact generated certificate and verifies the server is
untrusted again. Both paths have interruption cleanup; only G5 is production-
binary trust evidence.

## Findings

- Blocker: 0.
- High: 0.
- Medium: 0.
- Low: 0.

## Result

The reviewer concluded that the M5 protocol, lifecycle, trust, privacy,
dependency, product-composition, and regression claims are supported by the
reviewed implementation and recorded evidence.

Final verdict:

```text
AUDIT_PASS
Zero blocker, high, or medium findings.
```

M5 may close at the audited revisions above. Network impairment, PMTU
adaptation, broad soak, fuzz/sanitizer expansion, and multi-platform release
qualification remain M6.
