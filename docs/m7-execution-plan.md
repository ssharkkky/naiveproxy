# M7 BBR Congestion-Control Execution Plan

Last updated: 2026-09-01 (Asia/Shanghai)

Status: **M7 planned** (not started). Post-M6 follow-on, highest performance
priority. M0-M6 are complete and independently audited (release candidate
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`, merged to `master` at `fcf3bb36f3`). M7
replaces the outer QUIC CUBIC congestion control with BBR on both ends to remove
the measured per-session throughput cap on lossy high-RTT paths. It is a
congestion-control change only; it does not alter the UDP data path, the TCP
path, or the v1 protocol boundary.

Operational evidence belongs in [`native-udp-status.md`](native-udp-status.md).
This document defines pending work, ordering, exit criteria, risks, and stop
conditions. It must not be used to claim a gate passed before the status ledger
records the exact successful command and revision.

## 1. Mission and exit boundary

M7 raises native-UDP throughput on lossy high-RTT paths by selecting BBR as the
outer QUIC congestion algorithm, on both the client and the server, behind a
configurable switch that defaults to CUBIC (safe rollback without a rebuild).

Motivation (measured, recorded in the status ledger): on the Shanghai→Dallas
reference path (~140 ms RTT, 0–31% bursty ICMP loss) the CUBIC outer QUIC holds
only ~5–10 packets in flight, capping a single native-UDP session at
~50–100 KB/s, while Hy2 (BBR, `standard` profile) reaches ~700–836 KB/s on the
same path. Little's-law in-flight accounting (Native ≈ 5–10 packets vs Hy2
≈ 74–178; BDP ≈ 1180 packets) attributes the gap to CUBIC window collapse under
loss, not to the server pump, flow control, payload efficiency, or client
socket/qdisc.

M7 completes only when:

- the client can select in-tree Chromium BBR (v1 default for parity, v2
  available) via a config option that defaults to CUBIC;
- the server can select BBR (Hy2-ported, `standard` default; `conservative` /
  `aggressive` selectable) via a `quic.Config` option that defaults to CUBIC;
- the `quic-go` fork is byte-identical to `v0.59.0` except for the added BBR
  algorithm, the `CongestionControl` enum, and the `OnCongestionEventEx` wiring;
- the CUBIC (default) path is provably unchanged (all M1–M6 + 56 TCP + server
  legacy/privacy regressions green with CUBIC selected);
- a reference-path measurement shows BBR (both ends) materially above the
  recorded CUBIC baseline on the lossy path, toward the Hy2 reference;
- a scoped independent audit re-considers the client and server runtime
  boundaries and returns `AUDIT_PASS` with zero blocker/high/medium finding.

The final marker is `M7_BBR_OK`. BBR is a congestion algorithm, not a new wire
protocol; it must not change CONNECT-UDP framing, the DATAGRAM path, padding,
NAK handling, or privacy behavior.

## 2. Frozen inputs and ownership

| Input | Frozen revision/boundary | M7 rule |
| --- | --- | --- |
| NaiveProxy M6 closeout | `fcf3bb36f3` | Must remain an ancestor |
| Audited NaiveProxy runtime | audited through `eaf172d971`, including `333b7cb253` | Any later `src/net` change reopens the affected client audit boundary |
| In-tree Chromium BBR | `src/net/third_party/quiche/.../congestion_control/bbr_sender.cc` (v1) + `bbr2_*.cc` (v2); selection at `quic_sent_packet_manager.cc:153-157` | Reuse as-is; no new BBR code; connect via `client_connection_options` (`kTBBR`/`kB2ON`) before `Build()` |
| forwardproxy runtime | qualification head `964281a`; build-lock `e9663e4` | Zero forwardproxy source change (BBR inherits via Caddy `quic-go` replace) |
| Caddy | `dd9a89c1` | `go.mod` replace `quic-go => ssharkkky/quic-go <pin>`; set `CongestionAlgorithm` at both QUIC config sites |
| quic-go | `v0.59.0` | Minimal fork base; byte-identical except BBR/enum/`OnCongestionEventEx` |
| Hy2 BBR source | (record exact commit in G0) | Port `core/internal/congestion/bbr/` (`standard` profile default); pin the exact commit |
| Server toolchain | Go `1.25.12`, xcaddy `0.4.5`, quic-go `0.59.0` | No floating tool or module versions |

Repository ownership remains split:

- NaiveProxy owns the client `quic_congestion` option and the before-`Build()`
  connection-option wiring, plus client tests.
- A separately pinned `ssharkkky/quic-go` fork owns the server BBR port, the
  `CongestionControl` enum, and the `OnCongestionEventEx` wiring.
- Caddy owns the `go.mod` replace and the `CongestionAlgorithm` selection.
- `forwardproxy` is unchanged.

## 3. Inherited non-negotiable contracts

- Preserve `NaiveConnection`, the TCP data mover, and TCP padding.
- Native UDP remains RFC 9298 CONNECT-UDP + HTTP/3 DATAGRAM, Context ID `0`.
- BBR is a congestion algorithm only: it must not add UoT, UDP-over-stream, a
  second QUIC stack, a private framing protocol, or automatic datagram replay.
- CUBIC remains the default on both ends; BBR is strictly opt-in, so rolling
  back to CUBIC is a config change, not a rebuild.
- Preserve the exact transient `NetworkAnonymizationKey`, proxy authentication,
  target isolation, and bounded admission behavior audited in M3–M6.
- Do not log UDP payloads, destinations, or credentials; keep redacted,
  rate-limited counters and NetLog state/error events.
- The shipped client keeps `CertVerifier::CreateDefault()`.

## 4. Evidence and release-blocker policy

Every result must be one of:

- **verified** — exact command, revision, marker, and artifact/privacy checks
  are recorded in the status ledger;
- **not run** — no release claim is made;
- **blocked** — the reason and owner are recorded;
- **failed** — M7 stops at the current gate until fixed or the scope is
  explicitly revised.

The following block M7:

- a regression in M1–M6, the 56 TCP cases, or server legacy/privacy behavior
  with CUBIC selected;
- a `quic-go` fork diff that touches anything other than BBR, the
  `CongestionControl` enum, or the `OnCongestionEventEx` wiring;
- an ambiguous datagram replay, a memory-safety failure, or a data race;
- a secret or target disclosure; or an unpinned dependency, including the Hy2
  BBR source commit.

## 5. Sequential gates

### G0 — baseline, contract, and pin freeze

Status: not started.

Purpose: freeze the reference CUBIC baseline, the exact pins (including the Hy2
BBR commit), the production `quic.Config` site(s), and the config schema before
changing a runtime boundary.

Work:

1. Record the current CUBIC single- and 8-parallel throughput on the reference
   lossy path (and a clean-path control) in the status ledger.
2. Pin the exact Hy2 BBR source commit and record its hash; confirm the
   `standard` profile equals the shipped Hy2 reference (BBRv1 core: gain 2.885,
   8-phase `probe_bw`, cwnd gain 2.0).
3. Determine which Caddy `quic.Config` site(s) production actually uses
   (`serveHTTP3` and/or `ListenQUIC`) and freeze both for the change.
4. Unit-prove the in-tree BBR is selected by `kTBBR`/`kB2ON`, and that the
   ported server BBR implements the full `SendAlgorithmWithDebugInfos`
   (including `OnCongestionEvent`/`OnCongestionEventEx`/`PacingRate` as
   applicable to the fork's interface).
5. Freeze the config schema: client `quic_congestion` (`cubic` default / `bbr1`
   / `bbr2`); server `quic_congestion` (`cubic` default / `bbr-standard` /
   `bbr-conservative` / `bbr-aggressive`).

Exit: markers `M7_G0_BASELINE_OK`, `M7_G0_PINS_OK`, `M7_G0_CONFIG_SCHEMA_OK`,
and `M7_G0_CONTRACT_OK` recorded; no runtime source changed.

Stop if the audited M6 runtime differs from its boundary, the Hy2 BBR commit is
unavailable, or the production `quic.Config` site cannot be determined.

### G1 — client BBR enablement (opt-in, CUBIC default)

Status: not started.

Purpose: let the shipped client select Chromium BBR without changing default
behavior.

Work:

1. Add `quic-congestion` parsing to `naive_config.cc` (follow the existing
   option pattern), default `cubic`.
2. In `naive_proxy_bin.cc`, after obtaining `quic_context->params()`, push
   `quic::kTBBR` (`bbr1`) or `quic::kB2ON` (`bbr2`) to
   `client_connection_options` before `Build()`; push nothing for `cubic`.
3. Verify the default (`cubic`) is byte-for-byte the existing path.

Exit: 56 TCP owner cases + native-UDP matrix green at default; BBR selected
when `bbr1`/`bbr2` is set (unit + a live single-session throughput check).
Marker `M7_G1_CLIENT_BBR_OK`.

Stop if the default path changes or the before-`Build()` ordering is broken.

### G2 — server quic-go minimal fork (BBR + enum + OnCongestionEventEx)

Status: not started.

Purpose: add selectable BBR to the server with the smallest possible fork diff.

Work:

1. Create the `ssharkkky/quic-go` fork from `v0.59.0` exact; branch `codex/bbr`.
2. Port Hy2 `core/internal/congestion/bbr/` into `internal/congestion/bbr/` with
   the type remap (`congestion.ByteCount`→`protocol.ByteCount`,
   `congestion.PacketNumber`→`protocol.PacketNumber`,
   `congestion.MaxCongestionWindowPackets`→`protocol.MaxCongestionWindowPackets`,
   `apernet/.../monotime.Time`→`internal/monotime.Time`); `standard` profile
   default.
3. Add a `CongestionControl` enum to `quic.Config` (`Cubic` default, `Bbr` +
   profile) and switch the send-algorithm selection (`internal/ackhandler/
   sent_packet_handler.go`) on it.
4. Wire `OnCongestionEventEx` (Solution B): an optional interface method; the
   handler collects the acked/lost lists and the event time at the
   congestion-event point and type-asserts to call it. CUBIC is untouched when
   the method is absent.
5. Prove the fork diff is limited to BBR/enum/`OnCongestionEventEx` (byte-
   identical to `v0.59.0` otherwise).

Exit: unit — BBR window does not collapse under seeded loss; the CUBIC path is
byte-identical; the fork diff review is clean. Marker `M7_G2_QUICGO_FORK_OK`.

Stop if the fork diverges from `v0.59.0` outside the allowed files, or the
CUBIC path changes.

### G3 — Caddy fork integration and scoped server audit

Status: not started.

Purpose: point Caddy at the fork and select BBR without touching forwardproxy.

Work:

1. Add `go.mod` replace `quic-go => ssharkkky/quic-go <pin>` to the Caddy fork.
2. Set `CongestionAlgorithm` (and profile) at both QUIC config sites; keep the
   CUBIC default.
3. Run server legacy + privacy regressions; confirm forwardproxy is unchanged.

Exit: server regressions green with CUBIC and with BBR; scoped server audit
re-considered. Marker `M7_G3_CADDY_INTEGRATION_OK`.

Stop if forwardproxy behavior changes or a dependency floats.

### G4 — end-to-end parity measurement

Status: not started.

Purpose: prove the throughput gain on the reference path.

Work:

1. Deploy client `bbr1` + server `bbr-standard`; measure single- and 8-parallel
   throughput on the lossy path versus the G0 CUBIC baseline.
2. Confirm the download approaches the Hy2 reference (~700+ KB/s) and the upload
   is not regressed.

Exit: a recorded, attributable throughput improvement toward the Hy2 reference.
Marker `M7_G4_PARITY_OK`.

Stop if BBR does not improve on CUBIC on the reference path (re-open root cause).

### G5 — full regression, cross-platform, and independent audit

Status: not started.

Purpose: close M7 with full regressions and a scoped audit.

Work:

1. Full M1–M6 + 56 TCP + server legacy/privacy matrix at CUBIC (default) and BBR.
2. Cross-platform BBR runs where the outer QUIC stack is exercised.
3. A scoped independent audit re-considers the client and server runtime
   boundaries; record exact revisions.

Exit: all regressions green; audit `AUDIT_PASS` with zero blocker/high/medium;
final marker `M7_BBR_OK`; status ledger updated with exact commands, pins, and
commits.

Stop on any open blocker/high/medium finding or regression.

## 6. Gate dependency and change policy

```text
M6 audited baseline (fcf3bb36f3)
  -> G0 baseline + pins + config schema
  -> G1 client BBR (opt-in, cubic default)
  -> G2 quic-go fork (BBR + enum + OnCongestionEventEx)
  -> G3 Caddy integration + scoped server audit
  -> G4 end-to-end parity
  -> G5 full regression + audit
```

G1 (client) and G2 (server fork) may proceed in parallel once G0 is frozen, but
evidence and commits remain ordered. Each gate is one or more small green-to-
green commits in the repository that owns the change. A production-source fix
must include a minimized reproducer, the narrowest owner-repository patch,
focused and inherited regressions, an explicit audit-boundary impact note, and
updated exact revisions in the status ledger.

## 7. Artifact and privacy contract

BBR adds no new artifacts. Temporary measurement roots are private and removed
on success, failure, and signals. Throughput summaries are redacted aggregates
(no payloads, destinations, credentials). No packet captures, certificates, or
private keys are committed.

## 8. Effort estimate

| Gate | Estimate |
| --- | ---: |
| G0 baseline/contract/pins | 0.5–1 person-day |
| G1 client BBR enablement | 1–2 person-days |
| G2 quic-go fork (BBR port + enum + wiring) | 5–9 person-days |
| G3 Caddy integration + scoped server audit | 1–2 person-days |
| G4 end-to-end parity | 0.5–1 person-day |
| G5 full regression + audit | 2–3 person-days |

Total: approximately 10–18 person-days, dominated by the G2 quic-go fork. BBR is
the highest-priority post-M6 follow-on (it addresses the measured throughput
cap); the H2 datagram fallback (M8) is the lower-priority reachability
follow-on and should land after M7.
