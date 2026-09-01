# M8 H2 Datagram Fallback Execution Plan

Last updated: 2026-09-01 (Asia/Shanghai)

Status: **M8 planned** (not started). Post-M6 follow-on, lower priority than M7
(BBR). M0-M6 are complete and independently audited (release candidate
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`, merged to `master` at `fcf3bb36f3`). M8
extends native UDP so SOCKS5 UDP ASSOCIATE still works when the outer connection
is H2 (not QUIC), by carrying CONNECT-UDP datagrams over a reliable H2 stream —
the RFC 9298 stream delivery option. H3 DATAGRAM remains the primary/native
path; the H2 stream is a graceful fallback only when the outer is H2.

This crosses the v1 "no UDP-over-stream" boundary and is therefore a scoped v2
extension, still pure RFC 9298 (length-prefixed datagrams over the tunnel) with
no private wire protocol.

Operational evidence belongs in [`native-udp-status.md`](native-udp-status.md).
This document defines pending work, ordering, exit criteria, risks, and stop
conditions. It must not be used to claim a gate passed before the status ledger
records the exact successful command and revision.

## 1. Mission and exit boundary

M8 makes native UDP robust when QUIC is unavailable: when the outer proxy
connection is H2 (over TCP/TLS), UDP ASSOCIATE datagrams are carried over a
reliable H2 stream using the RFC 9298 stream option; when the outer is QUIC, the
existing H3 DATAGRAM path is used unchanged.

Motivation: the product today is all-`quic://`. If QUIC is blocked or degraded,
Chrome-style traffic falls back to H2 and the TCP path keeps working, but UDP
ASSOCIATE hard-fails. An H2 fallback keeps UDP available in that case and hides
it inside an ordinary HTTPS stream (better stealth than a bare QUIC connection).
It also removes the ~1200-byte H3 DATAGRAM size ceiling on the H2 path.

M8 completes only when:

- the H3 DATAGRAM (primary) path is provably unchanged (all M1–M6 + 56 TCP +
  server legacy/privacy regressions green);
- when the outer is H2, UDP ASSOCIATE datagrams are carried over a reliable H2
  stream using RFC 9298 stream framing (length-prefixed datagrams, Context ID
  `0`), and the same privacy/admission/NAK rules hold;
- the per-path delivery semantics are documented (H3: best-effort, no
  head-of-line blocking; H2: reliable, ordered, head-of-line blocking possible);
- a consistent maximum datagram policy is defined and documented for both paths;
- the `AGENTS.md` boundary is revised to "quic:// native DATAGRAM primary, H2
  reliable-stream fallback (RFC 9298 stream option)";
- a scoped independent audit re-considers the "no UDP-over-stream" boundary and
  returns `AUDIT_PASS` with zero blocker/high/medium finding.

The final marker is `M8_H2_FALLBACK_OK`.

## 2. Frozen inputs and ownership

| Input | Frozen revision/boundary | M8 rule |
| --- | --- | --- |
| NaiveProxy M6 closeout | `fcf3bb36f3` (or the M7 head if BBR lands first) | Must remain an ancestor |
| Audited NaiveProxy runtime | audited through `eaf172d971`, including `333b7cb253` | Any later `src/net` change reopens the affected client audit boundary |
| H3 DATAGRAM path | M1–M6 audited (`naive_connect_udp_tunnel`, `QuicProxyDatagramClientSocket`) | Must remain byte-for-byte the primary path |
| forwardproxy runtime | qualification head `964281a` | Add H2 CONNECT-UDP stream handling alongside DATAGRAM |
| Caddy | `dd9a89c1` | Serve the H2 CONNECT-UDP tunnel in addition to H3 |
| RFC 9298 | CONNECT-UDP stream delivery option | H2 framing is length-prefixed datagrams; no private protocol |

Repository ownership remains split:

- NaiveProxy owns the client H2 datagram-carry path and the primary/fallback
  selection, plus client tests.
- `forwardproxy` owns the server H2 CONNECT-UDP stream handling.
- Caddy owns the H2 tunnel serving.

## 3. Inherited non-negotiable contracts

- Preserve `NaiveConnection`, the TCP data mover, and TCP padding.
- The H3 DATAGRAM primary path is unchanged; M8 only adds an H2 fallback.
- The H2 path adds no private wire protocol: datagrams are length-prefixed per
  the RFC 9298 stream option, Context ID `0`.
- No automatic datagram replay: on the reliable H2 stream, retransmission is the
  stream's; the client must not re-send a datagram after an ambiguous write.
- Preserve the exact transient `NetworkAnonymizationKey`, proxy authentication,
  target isolation, and bounded admission behavior audited in M3–M6.
- Do not log UDP payloads, destinations, or credentials on either path; keep
  redacted, rate-limited counters and NetLog state/error events.
- The shipped client keeps `CertVerifier::CreateDefault()`.

## 4. Evidence and release-blocker policy

Every result must be one of:

- **verified** — exact command, revision, marker, and artifact/privacy checks
  are recorded in the status ledger;
- **not run** — no release claim is made;
- **blocked** — the reason and owner are recorded;
- **failed** — M8 stops at the current gate until fixed or the scope is
  explicitly revised.

The following block M8:

- a change to the audited H3 DATAGRAM primary path;
- a regression in M1–M6, the 56 TCP cases, or server legacy/privacy behavior;
- a payload/destination/credential disclosure on either path;
- an ambiguous datagram replay or cross-association delivery;
- an undocumented per-path semantic difference; or an unpinned dependency.

## 5. Sequential gates

### G0 — design and boundary revision

Status: not started.

Purpose: freeze the H2 stream framing, the per-path semantic contract, the max
datagram policy, and the config/enable decision, and revise the boundary.

Work:

1. Freeze the H2 CONNECT-UDP framing (RFC 9298 stream option: length-prefixed
   datagrams, Context ID `0`) and a unit contract for it.
2. Freeze the per-path delivery semantics (H3 best-effort/no head-of-line
   blocking vs H2 reliable/ordered/head-of-line blocking) and document the
   app-visible difference.
3. Freeze the maximum datagram policy for both paths (H3 DATAGRAM cap vs H2
   stream flow-control limit) and the behavior at the boundary.
4. Decide automatic fallback (QUIC→DATAGRAM, H2→stream) versus an explicit
   opt-in; record the decision.
5. Revise the `AGENTS.md` boundary (all-`quic://` → quic:// native DATAGRAM
   primary + H2 reliable-stream fallback).

Exit: markers `M8_G0_FRAMING_OK`, `M8_G0_SEMANTICS_OK`, and `M8_G0_BOUNDARY_OK`
recorded; no runtime source changed.

Stop if the framing would require a private protocol or would change the TCP path.

### G1 — client H2 fallback

Status: not started.

Purpose: carry UDP ASSOCIATE datagrams over an H2 stream when the outer is H2.

Work:

1. Add the H2 datagram-carry path (length-prefixed CONNECT-UDP datagrams over
   the H2 stream) alongside the existing `QuicProxyDatagramClientSocket`.
2. Select H3 DATAGRAM when the outer is QUIC; the H2 stream when the outer is H2.
3. Verify the H3 DATAGRAM path is unchanged at default.

Exit: 56 TCP + native-UDP (H3) regressions green; an H2-path echo plus an
HTTP/3-over-SOCKS-UDP application probe pass over an H2 outer. Marker
`M8_G1_CLIENT_H2_OK`.

Stop if the H3 DATAGRAM path changes.

### G2 — server H2 fallback

Status: not started.

Purpose: serve the H2 CONNECT-UDP stream in addition to H3 DATAGRAM.

Work:

1. Add forwardproxy H2 CONNECT-UDP stream handling (read length-prefixed
   datagrams, deliver to the target UDP socket) alongside the DATAGRAM path.
2. Serve the H2 tunnel in Caddy alongside H3.
3. Interop: H2 client↔H2 server and H3 client↔H3 server; mixed where valid.

Exit: server H2 + H3 interop green; server legacy/privacy regressions green.
Marker `M8_G2_SERVER_H2_OK`.

Stop if server H3 DATAGRAM behavior changes.

### G3 — cross-path semantics and privacy

Status: not started.

Purpose: verify the documented per-path semantics and privacy on both paths.

Work:

1. Verify H2 reliable/ordered/head-of-line vs H3 best-effort delivery under a
   loss profile; confirm the documented difference.
2. Verify NAK preservation, NetLog redaction, no payload logging, bounded
   admission, and no ambiguous replay on both paths.

Exit: markers `M8_G3_SEMANTICS_OK` and `M8_G3_PRIVACY_OK` recorded.

Stop on any payload/destination disclosure or replay.

### G4 — full regression and scoped audit

Status: not started.

Purpose: close M8 with full regressions and a scoped audit of the boundary
revision.

Work:

1. Full matrix: TCP, H3 DATAGRAM, H2 fallback, and cross-path.
2. A scoped independent audit re-considers the "no UDP-over-stream" boundary;
   record exact revisions.

Exit: all regressions green; audit `AUDIT_PASS` with zero blocker/high/medium;
final marker `M8_H2_FALLBACK_OK`; status ledger updated.

Stop on any open blocker/high/medium finding or regression.

## 6. Gate dependency and change policy

```text
M6 audited baseline (fcf3bb36f3 / M7 head)
  -> G0 design + boundary revision
  -> G1 client H2 fallback
  -> G2 server H2 fallback
  -> G3 cross-path semantics + privacy
  -> G4 full regression + audit
```

Each gate is one or more small green-to-green commits in the repository that owns
the change. A production-source fix must include a minimized reproducer, the
narrowest owner-repository patch, focused and inherited regressions, an explicit
audit-boundary impact note, and updated exact revisions in the status ledger.
The `AGENTS.md` boundary revision is made in G0 and is the audit's explicit scope.

## 7. Artifact and privacy contract

Temporary measurement roots are private and removed on success, failure, and
signals. Throughput/latency summaries are redacted aggregates (no payloads,
destinations, credentials). No packet captures, certificates, or private keys
are committed. The H2 path must not log datagram payloads or destinations,
matching the H3 path.

## 8. Effort estimate

| Gate | Estimate |
| --- | ---: |
| G0 design + boundary revision | 1–2 person-days |
| G1 client H2 fallback | 3–5 person-days |
| G2 server H2 fallback | 3–5 person-days |
| G3 cross-path semantics + privacy | 1–2 person-days |
| G4 full regression + audit | 2–3 person-days |

Total: approximately 10–17 person-days. M8 is the lower-priority reachability
follow-on; it should land after M7 (BBR) so the outer-QUIC congestion fix is
already in place before the H2 stream path is added.
