# CONNECT Follow-up Execution Plan

Last updated: 2026-09-08 (Asia/Shanghai)

Status: **W1 PRs submitted; W4 Fast Open re-enablement complete; W2/W3 pending.**

This plan records the agreed next work after the September 5 CONNECT fixes:
upstream existing correctness fixes, investigate DNS/address ordering, then
evaluate Go's built-in Happy Eyeballs. It is separate from M7-G5 and M8.
Verified commands, exact revisions, results, and PR links belong in
[`native-udp-status.md`](native-udp-status.md). Current product and deployment
inputs remain governed by [`README.md`](README.md) and
[`current-deployment.md`](current-deployment.md).

## 1. Priority and ownership

| Order | Work | Owner | Status | Completion evidence |
| --- | --- | --- | --- | --- |
| W1 | Submit focused Fast Open correctness PRs upstream | NaiveProxy | Submitted: #825, #826, #827; review pending | Exact base/head SHAs, reused documented validation with limitations, extraction checks, PR URLs in status ledger |
| W2 | Analyze and test DNS delays and address ordering | forwardproxy; records here | Pending investigation | Reproducible scenario matrix and separate optimize/defer decisions for DNS and sorting |
| W3 | Compare current scheduling with Go Happy Eyeballs | forwardproxy; records here | Pending comparison | Fair A/B measurements, ACL/lifecycle validation, and retain/replace decision |
| W4 | Re-enable Fast Open after CONNECT correctness fixes | NaiveProxy client; records here | **Complete (G0-G5)** | Production-delegate matrix, owner regressions, candidate artifacts, short A/B soak, deployment records, and scoped audit boundary in status ledger |

W1 was submitted without waiting for W2/W3 or UDP/BBR upstreaming. Per the
user's instruction, existing documented validation was reused after checking
patch/source equivalence; historical tests were not rerun for extraction.
Track subsequent upstream review and merge outcomes separately from PR
submission. W2/W3 require analysis and tests; they do not preselect a runtime
rewrite. Caddy and quic-go need no companion Fast Open patch in this scope.

## 2. W1: Existing correctness fixes upstream

The immediate candidates repair hangs, callback handling, and invalid-header
handling in ordinary TCP tunnels. Here Fast Open means CONNECT completion
before the proxy response, not kernel TCP Fast Open (RFC 7413). Describe the
observed defects without claiming unverified security impact.

| Patch | Existing source commit(s) | Defect and focused validation scope |
| --- | --- | --- |
| U1 | NaiveProxy `c8ebb943bd` | Body data buffered before initial headers are delivered must wake a pending read after header delivery, without needing another packet or FIN |
| U2 | NaiveProxy `eeb2b8ddc1` + `153de92c8e` | Late CONNECT header/status failure after Fast Open must complete pending reads and avoid invoking the consumed connect callback; test delayed non-200 without FIN, header-read failure, and callback-driven destruction |
| U3 | NaiveProxy `afce211960` | Failed H2 response-header conversion must propagate an error rather than dereference absent headers; test malformed responses with and without Fast Open |

U2's dependent commits were combined into one submitted patch. U1/U2 cite
existing reproduction records. U3 reuses source inspection and broad
regressions; its PR attributes the duplicate-Location trigger to inspection
of both conversion implementations. A dedicated malformed-header reproduction
remains a coverage follow-up; it is not represented as completed by submitting
the existing fix.

- [x] Refresh upstream `klzgrad/naiveproxy` HEAD, contribution requirements,
  and existing issues/PRs; confirm each defect still applies.
- [x] Create isolated branches/worktrees from exact upstream bases. Extract
  minimal production changes with attribution and historical evidence summaries.
- [x] Produce one focused PR per defect, combining U2's dependent commits.
  Check applicability of U1's stream notification separately from socket fixes.
- [x] Review concrete triggers and reuse existing documented reproduction and
  owner-regression evidence. Disclose U1's field-test scope, U2's delayed HTTP
  status trigger, and U3's source-only malformed-header analysis. Do not rely
  on disabling Fast Open to pass its failure tests.
- [x] Check extraction scope, whitespace, and equivalence to verified source.
  No fresh upstream build/test or standalone fixture was added; historical fork
  evidence is not presented as a run on the extracted upstream branch.
- [x] Submit the PRs with the concrete trigger, before/after behavior, source
  commit attribution, and validation. Record URLs and subsequent review state.
- [ ] Track upstream review/merge for [U1 #825](https://github.com/klzgrad/naiveproxy/pull/825),
  [U2 #826](https://github.com/klzgrad/naiveproxy/pull/826), and
  [U3 #827](https://github.com/klzgrad/naiveproxy/pull/827). Add focused tests for
  uncovered cases or adaptations when needed; preserve the stated evidence
  boundary instead of relabeling historical runs as fresh qualification.

September 7 privacy correction: historical deployment-document links were
removed from all three PR bodies; technical evidence summaries and their
limitations remain. The [status ledger](native-udp-status.md#documentation-privacy-cleanup-2026-09-07)
tracks the history rewrite and outstanding GitHub cache/PR-reference cleanup.

### Related changes requiring separate review

Client `b652d34aa5` stops enabling Fast Open from cached padding capability.
Server `7307332` waits for a successful target connection before returning
200 and propagates cancellation and 502/504 failures. These have correctness
motivation, but change the existing early-success policy and its latency
tradeoff; they are not prerequisites for U1-U3.

- [ ] Prepare a separate upstreamability assessment for the CONNECT response
  policy changes, including establishment latency and failure propagation.
- [ ] For `klzgrad/forwardproxy` (observed target branch `naive`), separate
  response/error semantics from address racing in `7307332`. Adapt any shared
  ACL helper dependencies to upstream; do not cherry-pick the mixed commit.

Address racing belongs to W2/W3's evidence and its own prospective PR. Keep
changes to the CONNECT establishment policy distinct from changes to
`NaiveConnection`, the TCP data mover, and padding, which remain out of scope.

## 3. W2: DNS and address-order investigation

### Current behavior and standards boundary

At forwardproxy `7307332`, `resolveTargetCheckACL` waits for `LookupIPAddr`,
filters and deduplicates the result while preserving order, and passes only
approved numeric addresses to `dialTCPAddresses`. `interleaveTCPAddresses`
preserves each family's order and alternates families with a first-family
count of one. Attempts normally start 250 ms apart; failure acceleration
retains a 100 ms minimum interval. Each attempt has a project-specific 5 s
timeout within the configured total deadline, which includes DNS.

[RFC 8305](https://www.rfc-editor.org/rfc/rfc8305.html) sections 4/5 describe
address sorting, interleaving, staggered attempts, and canceling losers. The
current scheduler follows those connection-racing principles. It does not
implement section 3's incremental A/AAAA processing and recommended 50 ms
Resolution Delay, or section 6's dynamic candidate updates. This limited
scope is not itself a TCP wire incompatibility; practical latency and
resource effects must drive changes, not a blanket compliance claim.

RFC 6724 sorting is delegated to Go/the platform resolver. The inspected Go
1.26.0 pure-Go lookup path calls `sortByRFC6724`. Delegating sorting is valid;
it does not establish identical behavior on all supported resolver paths.

### Investigation tasks

- [ ] Inventory resolver/toolchain modes actually used by released servers:
  pure Go, applicable cgo/system paths, OS, cache, hosts-file and search-domain
  behavior. Record which platforms are tested and which remain unverified.
- [ ] Add a controlled DNS fixture for fast A/slow AAAA, the reverse, one-family
  no-data/error/timeout, both slow, and successful single/dual-stack answers.
  Distinguish a dropped query from a delayed answer and test total cancellation.
- [ ] Measure DNS completion, first TCP attempt, target connection, and CONNECT
  response separately. Test warm/cold conditions where applicable; retain only
  aggregate/redacted timings for real traffic, never targets or payloads.
- [ ] Verify that resolver sorting survives ACL filtering and deduplication,
  each family's relative order survives interleaving, and `tcp4`/`tcp6` never
  dial the other family. Include preferred-family removal by ACL and multiple
  candidates per family, not only one IPv4/IPv6 pair.
- [ ] Determine whether partial DNS results would materially reduce observed
  latency. If warranted, prototype incremental candidates in an isolated
  experiment: ACL-check every new IP before dialing, deduplicate, honor the
  total deadline, and cancel outstanding work after a winner.
- [ ] Record separate DNS and sorting decisions. Keep resolver sorting unless
  a supported path demonstrates a gap. Implement an optimization only in a
  separate owner change justified by measured benefit and regression evidence.

Exit evidence: commands, fixture settings, toolchain/resolver modes, repeated
measurements, and optimize/defer conclusions. A decision to retain current
behavior is a valid result. Synthetic DNS delays demonstrate a mechanism;
they do not prove that current deployment latency is caused by DNS.

## 4. W3: Go Happy Eyeballs comparison

The comparison baseline is the release's Go 1.26.0, whose `net.Dialer`
Happy Eyeballs races two serial address-family queues, with a default 300 ms
fallback delay. Recheck the implementation if the toolchain changes. Current
custom scheduling can progress through multiple candidates of the same
family while earlier attempts are pending; this can cost more concurrent
sockets and goroutines. Go's built-in implementation can advance immediately
after definitive failures, whereas this scheduler retains 100 ms spacing.

`net.Dialer` has no public input for a prevalidated list of candidate IPs.
Passing the hostname after an ACL lookup performs resolution again and must
not reuse the old ACL verdict unchecked. A comparison prototype can assess
`ControlContext` validation of the actual numeric destination, while retaining
hostname/port policy and error semantics. Resolver injection through a DNS
transport is another possible design, not a free list-injection API. Reject
any alternative that can dial an unapproved address.

- [ ] Build an isolated standard-library prototype with explicit ACL handling,
  request cancellation, upstream-proxy context lifetime, and 502/504 mapping.
- [ ] Compare built-in `FallbackDelay=250ms` against the current 250 ms value;
  report Go's 300 ms default separately. Align total deadlines and disclose
  the differing per-address timeout policies rather than attributing all
  differences to the scheduling algorithm.
- [ ] Cover healthy dual stack; either family blackholed; the first candidate
  in each family blackholed with a later reachable same-family address; same-family
  only; immediate refusal; high RTT/loss; all candidates failing; and many
  candidates under concurrent requests. Reuse W2's DNS scenarios.
- [ ] Cover ACL-denied candidates, changed DNS answers, cancellation during
  DNS/dial, simultaneous successes, late successful losers, and teardown.
  Verify actual destinations before connect and absence of connection leaks.
- [ ] Record success/error counts, first-attempt and connection latency
  distributions (including p50/p95/p99 with sample counts), attempt count,
  peak active dials/file descriptors/goroutines, and post-cancel cleanup.
  Fix scenario seeds, repeat counts, and acceptable resource/latency budgets
  before comparing; explain uncertainty and platform coverage.
- [ ] Compare maintenance and upstream-review cost alongside measurements.
  Publish a retain/replace decision and the scenarios that justify it.

Replacement requires equivalent ACL and lifecycle correctness, acceptable
latency/resource results against the predeclared budgets, and a demonstrated
maintenance or performance benefit. Retaining the current implementation is
valid when faster progress through blackholed candidates justifies its cost.
Neither option should be described as complete RFC 8305 merely because it
uses Go or implements connection racing. Any selected replacement is a
separate implementation task with its own tests and review.

## 5. W4: Fast Open re-enablement

W4 restores the existing cached-padding Fast Open request behavior after the
CONNECT response-order fixes. It does not revert U1-U3, the client resource
exhaustion fix from upstream PR #819, the server's target-connect/error
semantics, or any TCP data-path change. In this repository, Fast Open is the
internal CONNECT policy that permits a learned-padding H2/H3 socket to become
usable before the proxy response; it is not kernel TCP Fast Open (RFC 7413).

The production `NaiveProxyDelegate` is the qualification subject. The
test-only `LegacyFastOpenDelegate` remains available only as an independent
regression mode and must not be used to claim production coverage. The
deterministic runner must prove both the early second CONNECT completion and a
pending application read completing with the delayed non-2xx response.

### Gates and contracts

- **G0 — contract (recorded here):** freeze the production delegate path,
  delayed H2/H3 response fixture, event ordering, timing measurements, and
  stop conditions. Real deployment endpoints and operator paths remain outside
  Git.
- **G1 — implementation (complete, `b0ced3798d`):** restore the historical header ordering in the
  production delegate, update the runner so standard and async-failure cases
  use that delegate, and keep the Legacy mode explicit. Build the affected
  Release targets and commit only the green-to-green client/test change.
- **G2 — qualification (complete):** run the production-delegate H2/H3 matrix for 200,
  502, and 504 with cold and learned padding; verify early completion,
  pending-read failure, callback cardinality, and malformed response handling;
  directly exercise Fast Open cancellation with an active callback owner and
  with the callback owner destroyed before socket close. Regress U1/U2/U3, the
  complete owner matrix, and all 56 TCP cases.
  Record exact commands and markers.
- **G3 — candidate and A/B (complete):** freeze a product lock using the current
  sanitized source identifiers, produce an exact candidate artifact, and
  compare Fast Open enabled/disabled on the test client under a declared
  workload. Run a controlled short alternating sample before touching the
  router; record request count, success/failure counts, latency samples,
  process restarts, and server-side error markers.
- **G4 — deployment (complete):** deploy the exact candidate to the test client first,
  then the router after the soak passes. Keep the previous router binary as the
  rollback artifact. The production server was not changed because its running
  binary already matched the candidate server artifact; no production client
  sing-box process, binary, or configuration was changed.
- **G5 — record and audit boundary (complete):** record matrix, A/B, soak, artifact,
  and deployment evidence separately. Reconsider only the client delegate path's
  audit boundary; completed M3-M6 audit markers do not automatically extend to
  this new runtime behavior.

The approved live comparison used the same Linux validation client and server:
27 successful HTTPS samples with Fast Open enabled and 29 with it disabled. The
enabled samples had median/mean/max latency 0.394/0.422/0.547 seconds; the
disabled samples had 0.381/0.412/1.105 seconds. Every sample returned HTTP 204,
both client service runs remained at NRestarts=0, and the enabled candidate
was restored after the comparison. This is controlled A/B smoke evidence, not
a claim of statistical performance improvement; the longer 24-48 hour soak was
removed from the acceptance contract.

The exact candidate lock is e8cc010356; combination run 34155408450 emitted
PRODUCT_COMBINATION_OK. Official release
v150.0.7871.63-5-native-udp-fastopen supplied the Linux x64 and OpenWrt
x86_64 client artifacts and the pinned server artifact. Their contained binary
hashes and deployment records are in native-udp-status.md and the release
manifests.

### Acceptance and stop conditions

Acceptance requires the production delegate to pass the deterministic H2/H3
CONNECT matrix, the full 56-case HTTP/HTTPS TCP owner regressions, and the
existing native-UDP/product-combination checks. The dedicated Fast Open
cancellation fixture covers pending-read cancellation with both an active
callback owner and an owner destroyed before socket close. In the async-failure case, the
second CONNECT must complete before the fixture's delayed response, exactly one
application read callback must complete with a negative error, and the runner
must exit without a watchdog timeout. Candidate and live evidence must include
sample counts, elapsed distributions, and the configured total dial deadline;
the per-address 5-second dial timeout must not be presented as a universal
end-to-end failure bound because DNS and the overall dial deadline are separate
stages.

Stop the experiment on an ACL bypass, a callback/lifetime error, a pending
operation that does not complete, an unbounded resource increase, a new reset
or error signature outside the documented CONNECT mapping, an owner-matrix
regression, or any failure outside the configured deadline. Keep the disabled
version ready for immediate deployment rollback.

## 6. Execution and audit boundaries

Before editing runtime code, follow the repository handoff reading order and
check all four repositories with `git status -sb`. Use isolated upstream
worktrees for W1 and keep experiments owned by forwardproxy for W2/W3.

For production changes, build affected Release/server targets, pass the
focused reproductions and complete applicable owner matrix, and preserve
M1-M5 markers, all 56 TCP cases, server legacy/privacy regressions, and
`git diff --check`. Use the canonical commands in the status ledger. Scope
and reconsider the affected audit boundary explicitly; historical M3-M6
`AUDIT_PASS` verdicts do not extend to later runtime changes.

Stop the affected experiment on an ACL bypass, callback lifetime defect,
unbounded resource growth, leaked connection, or inherited regression. Keep
experiments out of product locks and deployment until qualification. Record
only actually executed commands and attributable results, including failures
and untested platforms. Update checklist states as work completes, and stage
explicit intended paths without touching unrelated untracked files.
