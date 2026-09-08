# NaiveProxy Native UDP Project Status

Last updated: 2026-09-08 (Asia/Shanghai)

Documentation entry point: [`README.md`](README.md). Current deployment:
[`current-deployment.md`](current-deployment.md). M7 milestone record:
[`m7-execution-plan.md`](m7-execution-plan.md).

This is the execution ledger for the native UDP project. The development plan
defines scope and design; this file records what has actually been built and
verified. Update it at every completed G target and milestone.

## Current product lock and deployment

The current source and deployment authority is intentionally separated from
the historical milestone evidence below:

- Product lock: [`release/product.lock.json`](../release/product.lock.json),
  version `v150.0.7871.63-6-native-udp-fastopen-dns`, channel `experimental`, SHA256
  `df54c246c590368ebc5559a0ecd6a508274b2c5248fad6f492f5146cd1c929d2`.
- Locked `master` commits: NaiveProxy `4de6443f5ab3842bfead7f65b544207d83d290e3`
  (includes the CONNECT response fixes and Fast Open re-enablement), forwardproxy
  `cad30c35a736bd856789b3c7318a571d5c6d26ae`, Caddy
  `0ea5700f64254ba24e39d57b1febece2fa34927e`, and quic-go
  `c308178d8c77061d5e261ce9df37f2bcc0ab22bf`.
- Live deployment authority: [`current-deployment.md`](current-deployment.md).
- Machine-readable evidence: [client manifest](../release/manifests/current-client.json)
  and [server manifest](../release/manifests/current-server.json).
- Current online SHA256: router client `c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145`,
  Linux validation client `cdcff06ca5ecaabf839e298b9c1f298482af763c9c7e1f8c8828b83c218e49df`,
  server `3a5b1aa0e467415d93f3c8a13ffb71fcff47e65452a0a178db01be75e4c00daa`.

Release-6 provenance recheck (2026-09-08): downloaded artifact `10051577336`
has archive SHA256 `9c39c0caa3ea9922f4d681959178222e5b88fca9cbf2fa199faa710a9ddd5845`;
its `caddy` binary SHA256 is
`3a5b1aa0e467415d93f3c8a13ffb71fcff47e65452a0a178db01be75e4c00daa`.
`go version -m` reports `go1.26.0`, an embedded replacement of
`github.com/caddyserver/forwardproxy` by `_product/forwardproxy`, and the
locked Caddy/quic-go replacements; `caddy list-modules` reports
`http.handlers.forward_proxy`. The checked-out forwardproxy `cad30c35` contains
`25b4cd60` and its production constants are exactly 512 total and 128 per
client/source. The explicit-value contract assertion is covered by
forwardproxy test commit `eb7c78e` (`GOTOOLCHAIN=go1.26.0 go test ./...` passed).
No release or deployment replacement is required by this recheck.

## Incremental DNS acceptance and release 6 deployment (2026-09-08)

Forwardproxy `0d4e10f` was reviewed before release. Independent deterministic
tests reproduced a DNS-wakeup timing defect: an answer or NODATA arriving at
150 ms started the next candidate before the promised 250 ms normal stagger.
Fix `6416ca0` tracks `nextReadyAt`; only actual dial failures shorten the
window. The original review reproductions now pass with `-race`, including
NODATA with another same-family address already queued, and exhausted initial
candidates followed by late successful DNS. Reproducible experimental sources
and synthetic results are committed in `53c3a0d` (22 Route B scenarios).

The reviewed branch was merged to forwardproxy `master` as
`cad30c35a736bd856789b3c7318a571d5c6d26ae`. Product-lock commit
`93b47bc5c394cbac460b0aaa9c6d04771f5abb49` pins that server and the unchanged
client/Caddy/quic-go inputs above. Release tag
`v150.0.7871.63-6-native-udp-fastopen-dns` points to that lock commit.

Fresh acceptance:

```bash
# Forwardproxy repository; Go 1.26.0.
GOTOOLCHAIN=go1.26.0 go build ./...
GOTOOLCHAIN=go1.26.0 go test -race -count=1 ./...  # passed, 7.922 s
# Independent review worktree, at the repaired candidate.
GOTOOLCHAIN=go1.26.0 GOMAXPROCS=4 go test -race -run '^TestReview' -count=1 .  # passed
# NaiveProxy repository.
./scripts/verify-product-lock.sh  # PRODUCT_LOCK_OK
gh workflow run 'Product combination' --ref master
```

Product combination [34210375332](https://github.com/ssharkkky/naiveproxy/actions/runs/34210375332)
passed with `PRODUCT_COMBINATION_OK`. Downloaded evidence contains the identical
product lock and the CONNECT, Fast Open, M1-M5, and default certificate-verifier
markers. Its log contains all 56 TCP `TEST PASS` results. Locked server normal
and race tests passed in the same run. The dynamic scheduler is a new server
runtime boundary, reviewed with the defect and fix above and qualified by this
fresh combination; historical M3-M6 `AUDIT_PASS` records do not automatically
extend to it. This is scoped acceptance, not a new independent `AUDIT_PASS` or
completion of deferred M7-G5.

Server Release run `34215280766`, job `102025466714`, artifact `10051577336`
passed. The downloaded archive SHA256 is
`9c39c0caa3ea9922f4d681959178222e5b88fca9cbf2fa199faa710a9ddd5845`, matching
the published `SHA256SUMS`. Its product lock is byte-identical; Go provenance
shows Go 1.26.0, the CI checkouts of Caddy/forwardproxy, and quic-go
`v0.62.1-0.20260902185508-c308178d8c77`. Module `http.handlers.forward_proxy`
is present. The exact binary was installed without rebuilding at
2026-09-08 10:30:53 UTC, after validating the existing config, backing up the
old binary and metrics, and bounding service stop. Running process SHA256 is
`3a5b1aa0e467415d93f3c8a13ffb71fcff47e65452a0a178db01be75e4c00daa`.
Service is active/running, `NRestarts=0`, and unauthenticated CONNECT returns
the expected `407`.

Client Release run `34215280733` passed its OpenWrt x86_64 job `102025866208`
before deployment. Release asset `550308415` and CI artifact `10051902303`
contain the same archive, SHA256
`bdf2b28b79cf89ff806c10556722b04146bc8c5fafeed130815c6ba8ab9c76d5`; the
archive also matches the GitHub asset digest. The Naive executable SHA256
`c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145` matches
CI and is byte-identical to release 5, consistent with unchanged client runtime.
The new Release archive was nevertheless installed atomically and only the
Naive service restarted at 2026-09-08 10:44:37 UTC. Naive configuration hash
was unchanged. No sing-box operation was performed. The separate validation
client was not replaced. Other client platform builds were still running at
router deployment time. At deployment closeout, 49/50 platform/toolchain jobs
had succeeded; only macOS x64 was still building and no completed job failed.
The remaining platform's final result must be checked in run `34215280733`;
this deployment record does not claim all 50 jobs passed.

The bounded deployment probe talks directly to the Naive SOCKS5 listener.
It emits counts and timings without deployment endpoints, target addresses,
credentials, or raw connection errors:

```bash
# Build in tests/m5; copy the static probe to the router's temporary directory.
CGO_ENABLED=0 GOTOOLCHAIN=go1.26.0 go build -o /tmp/release-smoke ./cmd/release-smoke
# Run on the router, using its unchanged local SOCKS listener.
/tmp/release-smoke -samples 96 -concurrency 8 -failure
```

| Stage | TCP | UDP DNS | TCP median / p95 / max (ms) | Failed-target duration (ms) |
| --- | --- | --- | --- | --- |
| Before replacement | 96/96 | 2/4 | 535.241 / 1018.370 / 1430.121 | 5148.431 |
| After server replacement, old client | 96/96 | 4/4 | 534.468 / 838.032 / 1170.252 | 5204.671 |
| After both replacements, first batch | 96/96 | 4/4 | 467.058 / 1069.745 / 6349.762 | 5381.241 |
| After both replacements, second batch | 96/96 | 4/4 | 485.705 / 799.117 / 1202.064 | 5180.063 |
| After both replacements, third batch | 96/96 | 4/4 | 490.973 / 711.733 / 952.941 | 5177.385 |

Before replacement, follow-up TCP/UDP batches also returned 8/8 + 4/4 and
8/8 + 3/4; the latter used an alternate public DNS resolver and recorded one
query timeout. These failures are retained, not excluded from the record.
The first post-client batch includes a successful request taking 6.350 s;
the failed-target bound does not imply every successful application request
finishes within 6 s. HTTP 403/421 responses are completed TLS/HTTP exchanges,
not application-content success. Small samples on public targets do not prove
a statistical latency improvement or absence of packet loss.

Three post-client batches total 288/288 TCP and 12/12 UDP DNS. The router
service remained running with the expected executable hash; the server stayed
active/running with `NRestarts=0`. This is a short qualification observation,
not a 24-48 hour soak. Read-only verification at 11:24:24 UTC (router) and
11:24:27 UTC (server) confirmed the same process hashes, running services, and
both timestamped rollback binaries. That is approximately 40 minutes after
client replacement and 54 minutes after server replacement. Temporary router
installation files and the probe were removed; rollback binaries remain.

Deployment markers: `RELEASE6_CONFIG_OK`, `RELEASE6_SERVER_DEPLOY_OK`,
`RELEASE6_CLIENT_DEPLOY_OK`. Exact rollback paths, archive/job IDs, and current
hashes are in the deployment page and manifests. The older implementation
record below describes pre-release branch state and is superseded by this
acceptance and deployment record.

## CONNECT follow-up W2: DNS incremental implementation (2026-09-08)

The deferred W2 owner change (investigated the same day, decision "DNS =
defer; separate owner change justified by the measurements") is implemented
as Route B in the forwardproxy repository on branch
`codex/w2-dns-incremental-dial`, base `master` `d50ef3f`, as three commits:
`0d4e10f` (Route B implementation, 5 files, +860/-31), `6416ca0` (scheduling
timing fix found in review, see "Rework" below), and `53c3a0d` (w2w3
experiment harness + matrix evidence, 21 files). No merge to `master` was
performed (session constraint); deployment follows the W4 pattern
(candidate lock + A/B soak + rollback) and is pending owner start.

### Design (verified invariants)

- `dialContextCheckACL` now resolves `ip4` and `ip6` in parallel through an
  injected `lookupIPFamily`; the production default
  (`lookupIPFamilyDefault`) is `net.DefaultResolver.LookupIP` per family.
  Both lookups share the request context, which carries the total deadline
  (DNS included) and request cancellation.
- Each family's completion (success or failure) immediately admits its
  ACL-filtered, deduplicated (shared seen set, cross-family) addresses into
  the start queue in per-family resolver order; the first-arriving family
  dials immediately. No Resolution Delay.
- A late family joins its own queue; the scheduler alternates families lazily
  and checks context before each new dial. A winner, deadline expiry, or request
  cancellation cancels the other family's in-flight lookup; no goroutine
  leaks (matrix leak budget + untagged leak check green).
- The lazy interleave pop reproduces the audited static
  `interleaveTCPAddresses` order (first-family count one, then strict
  alternation, same-family continuation). A simultaneous first-arrival tie
  breaks to v6, matching the RFC 6724 order observed from the Go 1.26.0
  pure-Go resolver on the verified probe host (W2 P1).
- The audited `connect_dial.go` (`7307332`) is byte-for-byte unchanged;
  the new incremental dialer reuses its window policy - 250 ms stagger,
  100 ms minimum spacing after an actual dial failure, 5 s per-attempt cap,
  single winner. The dynamic-admission wake path is new scheduling code
  (not covered by the 7307332 audit): a DNS wake (late family completing,
  including NODATA/failure, or the feed closing) only re-checks candidate
  availability and termination and never starts a dial before the window
  measured from the last start has elapsed. One timing defect in that new
  code was found and fixed in `6416ca0` (see "Rework" below). Explicit
  `tcp4`/`tcp6` never dial the other family.
- Error mapping unchanged: both families failed -> 502 (504 when the cause
  is a deadline/timeout), all ACL-denied -> 403, addresses passed ACL but
  none matched the requested family -> 502, dials started and failed ->
  502/504 via `tcpDialError`.
- `prepareTargetPolicy` extracts the shared pre-resolution checks (split,
  port policy, domain ACL). The CONNECT-UDP path keeps the merged
  `resolveTargetCheckACL` and its sequential UDP dials, unchanged.
- The W3 scheduler conclusion (`W3_RETAIN_SCHEDULER_OK`) stands: the
  scheduler core is byte-for-byte unchanged.

### Step 1 — experiment (w2w3-tagged Route B matrix, Go 1.26.0)

```bash
# run at the root of the forwardproxy checkout, branch codex/w2-dns-incremental-dial
GOTOOLCHAIN=go1.26.0 go test -race -tags w2w3 -run 'TestW2W3RouteBMatrix' -count=1 -v .   # ok, 22/22 scenarios, 147.3 s
```

22 scenarios: re-derived W2 D1-D11 plus warm control D2w (12), and
N1_late_after_winner, N2a/N2b_denied_early/late, N3_both_servfail,
N4_cancel_inflight, N6/N6b_tcp4/tcp6_dual, N7_all_denied, N8_tcp6_v4only,
N9_multi_order (10). 30 real-port runs per scenario (20 for the three 2 s
drop scenarios); all invariants asserted per run (attempt order, status,
timing bands, zero ACL-denied dials, per-family cancellation observation,
final goroutine-leak budget). The harness (`experiments_w2w3_*.go`,
`//go:build w2w3`) and the evidence
(`experiments/w2w3/results/w2_routeb_matrix.jsonl`, 630 rows - one fresh
green run of the committed production code after `6416ca0`, plus
`w2_routeb_summary.jsonl`) are committed in `53c3a0d`, so the matrix is
reproducible from the branch. Medians
(`t_conn` = target connect; "old" = 2026-09-08 merged-lookup matrix above):

| scenario | result | new median | old median |
| --- | --- | --- | --- |
| D1_dual_fast_blackhole | 200, 1 attempt, v6a (tie-break) | 10.5 ms | 261.0 ms (2 attempts) |
| D2_v4_fast_v6_slow | 200, 1 attempt, v4a | 10.5 ms | 501.0 ms (2 attempts) |
| D3_v6_fast_v4_slow | 200, 1 attempt, v6a | 10.5 ms | 501.1 ms (2 attempts) |
| D4_v4_only / D5_v6_only | 200, 1 attempt | 10.5 ms | 10.2/10.3 ms (unchanged) |
| D6_v4_drop_v6_ok / D7_v6_drop_v4_ok | 200, 1 attempt, dropped family canceled | 10.5 ms | 2000.7 ms |
| D8_v4_servfail_v6_ok | 200, 1 attempt | 10.5 ms | 10.3 ms (unchanged) |
| D9_both_slow (300/400 ms) | 200, 1 attempt, v4a (first-completing family wins) | 300.7 ms | 400.9 ms |
| D10_all_dropped | 504, 0 dials | unchanged | unchanged |
| D11_total_cancel | 502, 0 dials, both lookups canceled | unchanged | unchanged |
| D2w_warm (v6-first control) | 200, 2 attempts [v6a, v4a], v4a at the stagger | 253.3 ms | 2.3 ms (old warm control won first attempt) |
| N1_late_after_winner | 200, 1 attempt, late v6 never dialed, canceled | 10.5 ms | n/a |
| N2a/N2b denied early/late | 200, 1 attempt, 0 denied dials | 25.9/10.5 ms | n/a |
| N3_both_servfail | 502, 0 dials | unchanged | unchanged |
| N4_cancel_inflight | 502, 0 dials, both lookups canceled | ~100 ms | n/a |
| N6/N6b family isolation | 200, own-family dials only | 25.8/10.5 ms | n/a |
| N7_all_denied | 403, 0 dials | unchanged | unchanged |
| N8_tcp6_v4only | 502, 0 dials | unchanged | unchanged |
| N9_multi_order | 200, [v6a, v4a, v6b] at the audited stagger | 511.9 ms | n/a |

Superseded W2/W3 production-path drivers (`TestW2W3DNSMatrix`,
`TestW2W3IncrementalComparison`, `TestW2W3OrderPreservation`,
`TestW3Matrix`, `TestW3Smoke`, `TestW3HE300`) skip with a pointer to the
Route B matrix; `TestW2W3NumericPassthrough` still passes (verified Go
1.26.0 numeric behavior: `LookupIP("ip4","192.0.2.9")` returns the address;
the mismatched family returns a `no suitable address found` DNSError, no
external query).

### Rework (2026-09-08): DNS wake must hold the 250 ms stagger

Review reproduced a timing defect in the new wake path: with the first
dial in flight and no dial failure yet, a second family answering at
150 ms (including a NODATA/failure wake) started the second dial early,
because the start-window check used the failure-acceleration rule (100 ms
since the last start) on every wake instead of the normal 250 ms stagger.
Fix (`6416ca0`): track the earliest next-start moment explicitly
(`nextReadyAt`), mirroring `dialTCPAddresses` window semantics - zero
until the first start, then always measured from the last start at +250 ms,
shortened to +100 ms only by an actual dial failure (a late failure
re-arms the timer to fire immediately). A wake only re-checks candidate
availability and termination; it never shortens or satisfies the window.
Dynamic-candidate semantics are preserved: first family exhausted/failed
while the other is still in flight never ends the race early, and a late
success keeps racing and is dialed as soon as it is admitted after the
window has elapsed.

Deterministic coverage added to the untagged suite (synctest virtual time,
exact ms): late family answer at 150 ms while the first dial is in flight
(second dial at exactly the 250 ms stagger, 260 ms end-to-end); late
family NODATA at 150 ms closing the feed (no early dial, no early
termination - 504 at the 5 s per-attempt cap, 5010 ms end-to-end); first
family's only address failing at 10 ms (race stays open across the 100 ms
failure window and dials the late success at 300 ms).

### Step 3 — regression (Go 1.26.0, forwardproxy owner suite)

```bash
# run at the root of the forwardproxy checkout, branch codex/w2-dns-incremental-dial
GOTOOLCHAIN=go1.26.0 go build ./...              # ok
GOTOOLCHAIN=go1.26.0 go test -count=1 ./...      # ok (3.6 s)
GOTOOLCHAIN=go1.26.0 go test -race -count=1 ./...   # ok (7.8 s)
GOTOOLCHAIN=go1.26.0 go test -race -tags w2w3 -count=1 .   # ok, incl. 22/22 Route B (147.3 s)
git diff --check                                 # clean (both repositories)
```

The untagged suite gains `connect_dial_incremental_test.go` (16 test
functions: skew both directions, dropped family, all-dropped, all-fail,
all-denied, tcp4/tcp6 isolation, late denied, late-after-winner with
goroutine-leak check, cancel-in-flight, interleave order, per-family
order, dedup, pop-rule unit test, and the three rework synctest scenarios
above). Existing Happy Eyeballs tests retain
their exact expected values; dual-stack cases pin the first-arrival family
with a 10 ms virtual `synctest` delay (the legacy merged fixture modeled
this implicitly with list order). The Caddy fork and quic-go fork are not
modified by this change, so their M4 evidence stands; the NaiveProxy
client is unchanged, so the 56-case TCP owner matrix and all M1-M6 markers
stand.

### Audit boundary

Forwardproxy server-runtime change limited to the TCP CONNECT
establishment policy. No NaiveProxy client change, no `NaiveConnection`
TCP data path or padding change, no Caddy/quic-go fork change, no M1-M6
marker change. Deployment not performed (W4 pattern, pending owner start).

### Experiment delivery

The Route B matrix harness, the W2/W3 harness files, the real-resolver
probe, and the synthetic matrix evidence are committed in `53c3a0d`
(`experiments_w2w3_*.go` at the package root behind `//go:build w2w3`,
`experiments/w2w3/realprobe/`, `experiments/w2w3/results/*.jsonl`). Only
the three real-resolver probe outputs containing host-specific resolver
configuration remain untracked; their redacted aggregates are in the W2
section above.

## Server CONNECT upstream submission (2026-09-08)

[klzgrad/forwardproxy PR #12](https://github.com/klzgrad/forwardproxy/pull/12)
submits two focused fixes against upstream `naive` base
`d62c80d3dd2c706b6b87579844d2397bddd18317`:

- `2d704e94264c912c743b872b8ff75576bb0372ab`: send and flush CONNECT 200
  only after target dialing succeeds; return 502 for connection failures and
  504 for timeouts, retaining 403 for ACL denials and padding on errors.
- `69cdb98b12dca24c082801086c7545fc4d199bfd`: propagate the request context
  through lookup and dialing, close connections returned after cancellation,
  and preserve the upstream tunnel context after dialing returns.

Branch: `ssharkkky/forwardproxy:upstream/fix-connect-response`. The PR is open
for review; submission does not mean upstream merge or CI qualification.
It changes only `forwardproxy.go` and two focused test files. It carries no
address-racing scheduler, incremental DNS implementation, native UDP, BBR,
dependency update, product-lock change, or deployment. The single combined
DNS lookup now accepts the request context; its candidate model is unchanged.

Fresh verification used Go 1.22.2 and the upstream dependency graph:

```bash
GOTOOLCHAIN=go1.22.2 GOMAXPROCS=4 go build ./...
GOTOOLCHAIN=go1.22.2 GOMAXPROCS=4 go test -count=1 -timeout=120s ./...
GOTOOLCHAIN=go1.22.2 GOMAXPROCS=4 go test -race -count=1 -timeout=180s ./...
git diff --check d62c80d HEAD
```

All passed. The original upstream full suite passed before editing. New
response regressions failed before the first fix; request-cancellation,
late-success, and expired-DNS-deadline cases failed before the second fix.
The focused tests cover H1/H2/H3 handler behavior; the inherited suite supplies
the existing H1/H2 integration, ACL, authentication, and probe-resistance
coverage. No new H3 wire-level integration result is claimed.

An initial race run was inconclusive; a diagnostic repeat hit TLS failures
while a separate development test process occupied the same fixed fixture
ports. The final full race run passed in an isolated Linux network namespace
with loopback enabled and a Unix-socket DNS relay to the host resolver. No
test-source overlay or changes to the concurrent developer's process were
needed. The relay was stopped after verification.

PR text, tests, and this record contain only synthetic/documentation targets;
known private endpoint and operator-path scans of the submitted changes passed.
Existing deployed artifacts and historical audit conclusions are unchanged.

## CONNECT follow-up W2: DNS and address-order investigation (2026-09-08)

W2 is complete (investigation only; no forwardproxy runtime change). All
experiments sit behind the `//go:build w2w3` tag: the
`experiments_w2w3_*_test.go` files are at the package root because they
must be in `package forwardproxy` to reach the unexported handler/dial
APIs; experiment data lives under `experiments/w2w3/results/*.jsonl`. The
harness and evidence were later committed on the W2 implementation branch
(`53c3a0d`, see the W2 implementation section above). Untagged
`go build ./...` and `go test ./...` are unaffected and pass; `git diff
--check` is clean.

### Resolver/toolchain inventory

The currently deployed server binary (SHA256
`d8d886126fee26a2777248b9081566cb79618d407258a690af8ec3c48749d230`, matching
the online hash in the product lock above) embeds:

```text
$ GOTOOLCHAIN=go1.26.0 go version -m <server binary> | head -1
<server binary>: go1.26.0
build   CGO_ENABLED=0
build   GOOS=linux
build   GOARCH=amd64
build   -tags=nobadger,nomysql,nopgx
build   -trimpath=true
```

The release server therefore uses the Go 1.26.0 **pure-Go (netgo) resolver**;
the cgo/system resolver path is not compiled into the release artifact. The
build is xcaddy v0.4.5 on GitHub ubuntu-22.04 with `go-version: 1.26.0`
(`.github/workflows/m7-server-build.yml`; `M7_TOOLCHAIN.lock`
`GO_VERSION=1.26.0`). The probe host is Linux with the systemd-resolved stub
`/etc/resolv.conf` (`nameserver 127.0.0.53`, `options edns0 trust-ad`,
`search .`) and a standard `/etc/hosts`; the deployment host's resolv.conf
specifics are not in Git, so live-host nameserver/search/ndots behavior
remains unverified.

Verified pure-Go path behavior (Go 1.26.0 source inspection plus the
real-resolver probe below): `/etc/hosts` is consulted before DNS; A and AAAA
sub-queries run in parallel and `LookupIPAddr` **waits for both** before
returning; if any family returns addresses the merged addresses are returned
and per-family errors surface only when every family failed; a dropped
exchange times out after 5 s (default) and is retried once (attempts=2), so a
silently dropped family costs up to 10 s; SERVFAIL and NODATA return
immediately; request-context cancellation aborts in-flight exchanges; results
are ordered by `sortByRFC6724` (RFC 6724/3484) — on the probe host a
dual-stack answer came back IPv6 first. There is no resolver-level cache: each
`LookupIPAddr` is a fresh query.

Verified: linux/amd64 pure-Go (release mode). Untested: cgo/system resolver
(not compiled into the release), deployment-host resolver configuration,
non-Linux server artifacts (none exist).

### Controlled DNS fixture matrix

`TestW2W3DNSMatrix` injects `h.lookupIP` with per-family delay/behavior
fixtures (answer/nodata/error/drop) and drives the full `ServeHTTP` CONNECT
path; the first candidate blackholes until the winner is chosen (250 ms
stagger, 5 s per-attempt timeout, total deadline includes DNS). 30 repeats per
scenario; each run records status, attempt order, winner, and separate
t_dns/t_first/t_conn/t_resp (`experiments/w2w3/results/w2_dns_matrix.jsonl`).
"drop" is modeled as a 2 s resolver timeout to bound runtime; the true cost
of a dropped exchange is measured by the real-resolver probe below.

| Scenario (30 runs each) | Fixture | Status | t_dns p50 | t_conn p50 |
| --- | --- | --- | --- | --- |
| D1_dual_fast_blackhole | both answer 10 ms | 30/30 200 | 10.4 ms | 261.0 ms |
| D2_v4_fast_v6_slow | v4 10 ms / v6 500 ms | 30/30 200 | 501.0 ms | 501.1 ms |
| D3_v6_fast_v4_slow | v6 10 ms / v4 500 ms | 30/30 200 | 501.1 ms | 501.1 ms |
| D4_v4_only | v4 answer / v6 NODATA | 30/30 200 | 10.2 ms | 10.3 ms |
| D5_v6_only | v6 answer / v4 NODATA | 30/30 200 | 10.3 ms | 10.3 ms |
| D6_v4_drop_v6_ok | v4 drop (2 s) / v6 10 ms | 30/30 200 | 2000.7 ms | 2000.8 ms |
| D7_v6_drop_v4_ok | v6 drop (2 s) / v4 10 ms | 30/30 200 | 2000.7 ms | 2000.8 ms |
| D8_v4_servfail_v6_ok | v4 SERVFAIL / v6 answer | 30/30 200 | 10.3 ms | 10.3 ms |
| D9_both_slow | 300/400 ms | 30/30 200 | 400.9 ms | 401.0 ms |
| D10_all_dropped | both drop (2 s) | 30/30 504 | 2000.6 ms | no dial |
| D11_total_cancel | both 500 ms, cancel at 100 ms | 30/30 502 | aborted | no dial |
| D2w_warm (control) | lookup 0 ms | 30/30 200 | 2.3 ms | 2.3 ms |

Every run matched the per-scenario status/attempt/winner invariants (the test
asserts per run). D2/D3 show the first TCP attempt is gated on the full
A+AAAA lookup; D6/D7 show a dropped family gates the dial even when the other
family already answered; D10/D11 confirm the 504/502 mapping and that total
cancellation aborts the lookup before any dial. The warm control (D2w, 0 ms
lookup) bounds non-DNS overhead at about 2 ms.

### Ordering, ACL, and family isolation

`TestW2W3OrderPreservation` + `TestW2W3NumericPassthrough` (all pass,
`ok 2.367s`) assert the exact attempt order per run with every dial refused:

- O1a deny-middle + dedup: resolver order survives the ACL filter and dedup.
- O1b v6-first + deny-v4: per-family order survives; interleave emits
  `v6a, v4a, v6b, v4b` (first-family count one, then alternate).
- O2a/O2b/O2c multi-candidate per family (3×v4, 3×v6, mixed): per-family
  relative order survives interleaving.
- O3a/O3b/O3c `tcp4`/`tcp6` isolation: `tcp4` dials only v4 even when v6 is
  resolver-first; `tcp6` only v6; `tcp4` with no v4 answer returns 502.
- O4a/O4b ACL removes the preferred family: falls back to the other family
  in resolver order.
- O5 combined deny + dedup + multi-candidate: exact order asserted.
- Numeric passthrough: numeric v4/v6 dial directly; a denied numeric is
  rejected before any dial (502).

### Incremental-candidates prototype (isolated)

`TestW2W3IncrementalComparison` runs the current path and an isolated
incremental-candidates prototype (per-family batch stream; ACL check before
dial; dedup; same 250 ms stagger / 5 s per-attempt / total deadline; cancel
on winner) against identical fixtures, 30 repeats each
(`experiments/w2w3/results/w2_incremental.jsonl`):

| Scenario | Current t_conn p50 | Incremental t_conn p50 | Δ p50 |
| --- | --- | --- | --- |
| C1 both fast (winner second) | 260.9 ms | 260.8 ms | ≈ 0 |
| C2 v4 fast / v6 500 ms slow (winner v4) | 500.3 ms | 10.3 ms | +490 ms |
| C3 v4 drop (2 s) / v6 fast (winner v6) | 2001.0 ms | 10.3 ms | +1991 ms |
| C4 v6 drop (2 s) / v4 fast (winner v4) | 2000.8 ms | 10.3 ms | +1991 ms |

Both modes return the same winner with 30/30 HTTP 200 in every scenario. The
partial/incremental benefit is zero when unskewed and equals the slow family's
full cost when skewed or dropped.

### Real-resolver probe (mechanism validation)

`experiments/w2w3/realprobe` points `net.Resolver{Dial: ...}` (the same
pure-Go DNS implementation the release server uses) at a local UDP DNS
fixture with scenario-controlled delays/behaviors, Go 1.26.0, `go build
-race` clean (0 data races):

| Scenario (repeats) | Fixture | p50 | Result |
| --- | --- | --- | --- |
| P1 dual fast (5) | A/AAAA answer at 20 ms | 21.0 ms | 2 addrs, v6 first (RFC 6724) |
| P2 A fast / AAAA slow (5) | 20 ms / 700 ms | 701.3 ms | waits for the slow family |
| P3 A slow / AAAA fast (5) | 700 ms / 20 ms | 701.4 ms | waits for the slow family |
| P4 AAAA dropped (3) | A 20 ms / no AAAA reply | 10005.6 ms | A still returned (partial) |
| P5 A dropped (3) | no A reply / AAAA 20 ms | 10005.8 ms | AAAA still returned (partial) |
| P6 AAAA NODATA (5) | A 20 ms / empty AAAA | 20.7 ms | fast empty answer is free |
| P7 both dropped (2) | no replies | 10001.9 ms | `i/o timeout` error |
| P8 both SERVFAIL (5) | 20 ms | 21.0 ms | `no such host` error |
| P9 cancel (3) | both 700 ms, cancel at 100 ms | 100.9 ms | `operation was canceled` |

A dropped query costs one 5 s per-attempt timeout plus one retry (10 s per
family), distinct from a delayed answer (costs only the delay) and from
SERVFAIL/NODATA (immediate).

### W2 conclusions

**DNS: defer the runtime change (a separate owner change is justified).**
Measured mechanism: `LookupIPAddr` gates the first TCP attempt on the full
A+AAAA lookup, so a slow family adds its full delay (701 ms for a 700 ms
AAAA) and a dropped family adds up to 10 s before any dial starts, even when
the other family answered in milliseconds. Incremental candidates recover
exactly that skew (C2 +490 ms; C3/C4 +1991 ms under the 2 s drop model; ≈ 0
when unskewed). This justifies a separate owner change — incremental A/AAAA
or dialing the first-arriving family, or a per-family DNS timeout — with its
own owner-matrix regression and audit reconsideration; W2 implements no
runtime change. Synthetic delays prove the mechanism only; they do not
establish that current deployment latency is DNS-caused.

**Ordering: retain resolver sorting.** RFC 6724 order (verified IPv6-first on
the probe host) survives ACL filtering + dedup, per-family relative order
survives interleaving, `tcp4`/`tcp6` never dial the other family, ACL
preferred-family removal falls back correctly, and multi-candidate families
are handled. No gap was demonstrated on the supported pure-Go path, so no
re-sorting is introduced.

### Commands and results

```bash
# run at the root of the forwardproxy checkout (then master d50ef3f, clean)
git status -sb    # master d50ef3f, clean
GOTOOLCHAIN=go1.26.0 go version -m <release server binary> | head -1    # go1.26.0, CGO_ENABLED=0, linux/amd64
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW2W3DNSMatrix' -count=1 .    # ok 234.103s
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW2W3OrderPreservation|TestW2W3NumericPassthrough' -count=1 .  # ok 2.367s
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW2W3IncrementalComparison' -count=1 .  # ok 151.23s
cd experiments/w2w3/realprobe
CGO_ENABLED=1 GOTOOLCHAIN=go1.26.0 go build -race -o /tmp/w2w3-realprobe-race . && /tmp/w2w3-realprobe-race results/w2_real_resolver_race.jsonl   # 0 data races
cd ..
GOTOOLCHAIN=go1.26.0 go build ./... && GOTOOLCHAIN=go1.26.0 go test -count=1 ./...   # ok (untagged build/tests unaffected)
git diff --check    # clean
```

The untagged forwardproxy owner suite (`go test ./...`) passes unchanged; the
56-case Naive TCP matrix lives in the untouched NaiveProxy repository and was
not affected by this investigation. W3 (Go Happy Eyeballs comparison) is
complete; see the next section.

## Go Happy Eyeballs comparison (W3) — complete

Baseline: forwardproxy `master` `d50ef3f` (custom scheduler `7307332`),
Go 1.26.0 via `GOTOOLCHAIN=go1.26.0`. Both paths ran with the same 12 s total
deadline, the same candidate addresses (loopback placeholders on port 443:
`127.0.0.11-14`, `2001:db8:30::11-14`), and the same per-candidate behavior
(realized by the kernel for accept/refuse and by a pre-connect hook for
delay/loss/blackhole, inside the dial call so both schedulers observe
identical "attempt pending N ms then success/failure" dynamics). Blackhole
is a pre-connect block until the dial context dies —
scheduler-indistinguishable from a kernel SYN drop. The current path is the
production `dialContextCheckACL` (resolve → ACL pre-filter →
`dialTCPAddresses`) with the W2 lookup fixture; the HE path is an isolated
`net.Dialer{FallbackDelay: 250ms, ControlContext: ACL gate on the actual
numeric destination, Resolver: loopback UDP DNS fixture}` (pure-Go resolver
path, as in the release server), with `tcpDialError` semantics mirrored
(timeout → 504, else 502). Primary comparison uses `FallbackDelay=250ms`
(matching the current scheduler's stagger); Go's 300 ms default is reported
separately (H1/H2/H3/H4/H7/H9 × 10 repeats, path `he300`).

Seeds and budgets were fixed before the matrix: RTT delay 100 ms ± 20 ms
(seed base `2026090881`), flaky loss seed base `2026090882` (per
(scenario, run, candidate) seed, identical on both paths so the same
candidate wins on both); repeat counts per scenario (H4 15, H10 10, H11
10×20 parallel, H18 5×10 parallel, H21 3, all others 20-30 as in the plan);
B1 — on healthy dual-stack scenarios (H1, H5, H6, H8, H16, H17, H20) HE p95
t_conn must lead the current path's p95 by ≤ 100 ms; B4 — post-batch leaks:
0 active target connections, fd/goroutine peaks within baseline + 2, and no
kernel-ESTABLISHED connections to the candidate addresses. Per-run
invariants (status, winner, attempt list, timing bands, ACL-denied counts)
are asserted inside `TestW3Matrix`; `git diff --check` stayed clean in both
repositories and the tracked forwardproxy tree was untouched (experiment
files are untracked, `//go:build w2w3`).

### Matrix results (22 scenarios × both paths; all per-run invariants pass)

t_conn p50/p95 in ms (per-run rows in
`experiments/w2w3/results/w3_matrix.jsonl`, aggregates in
`w3_summary.jsonl`):

| ID | n | status cur/HE | cur p50 | HE p50 | cur p95 | HE p95 | layout |
| --- | --- | --- | --- | --- | --- | --- | --- |
| H1 | 30 | 200/200 | 0.1 | 0.4 | 0.2 | 0.4 | v6-first dual healthy; winner v6a both |
| H2 | 30 | 200/200 | 0.1 | 0.4 | 0.2 | 0.4 | dual healthy v6-first, extra candidates; winner v6a both |
| H3 | 30 | 200/200 | 250.9 | 251.3 | 251.4 | 251.8 | v6 blackhole; v4 wins at the stagger/fallback window |
| H4 | 15 | 200/200 | 501.2 | 6001.1 | 502.2 | 6001.6 | first primary candidate blackhole; v6b (second same-family) is the only healthy target |
| H5 | 30 | 200/200 | 0.2 | 0.4 | 0.2 | 0.4 | v4-only single candidate; winner v4a |
| H6 | 30 | 200/200 | 0.2 | 0.4 | 0.2 | 0.4 | v6-only single candidate; winner v6a |
| H7 | 30 | 502/502 | 100.6 | 0.6 | 101.3 | 0.8 | both refuse; failure pacing |
| H8 | 30 | 200/200 | 109.0 | 109.2 | 119.8 | 120.6 | both delayed 100-119 ms (seeded RTT); winner v6a both |
| H9 | 30 | 200/200 | 38.6 | 39.3 | 289.3 | 289.5 | flaky first candidates (seeded, same seed both paths); winner matches per run |
| H10 | 10 | 504/504 | 5251.2 | 12000.2 | 5251.7 | 12000.7 | both blackhole; per-attempt cap vs full-deadline slices |
| H11 | 10×20 | 200/200 | 501.5 | 6001.1 | 502.3 | 6001.8 | H4 layout under 20 parallel × 10 rounds |
| H12 | 30 | 502/502 | 100.9 | 0.7 | 101.4 | 0.9 | v4b ACL-denied; pre-filter vs dial-time gate |
| H13 | 20 | 200/200 | 0.1 | 0.4 | 0.2 | 0.5 | DNS change between requests; no stale dials |
| H14 | 30 | 502/502 | 0.0 | 0.2 | 0.0 | 0.2 | both families NODATA; 0 attempts |
| H15 | 10 | 502/502 | 1000.5 | 1000.4 | 1001.2 | 1001.0 | request cancel at 1 s |
| H16 | 20 | 200/200 | 280.7 | 280.9 | 281.2 | 281.2 | v6 refuse, v4 ok at stagger; winner v4a |
| H17 | 10 | 200/200 | 310.7 | 310.8 | 311.1 | 311.1 | both refuse-then-ok at 500 ms targeted; winner v6a |
| H18 | 5×10 | 502/502 | 200.5 | 200.4 | 200.5 | 200.5 | 10 parallel, cancel at 200 ms |
| H19 | 10 | 502/502 | 0.0 | 0.2 | 0.0 | 0.2 | `tcp4` with v6-only answer; 0 attempts, no v6 connection |
| H20 | 20 | 200/200 | 500.9 | 501.1 | 501.3 | 501.8 | v4 answer delayed 500 ms in DNS; winner v6a at ~1 ms |
| H21 | 3 | 504/200 | 12000.7 | 10001.9 | 12000.7 | 10001.9 | A sub-query dropped, AAAA answered (as-implemented divergence) |
| H22 | 10 | 502/502 | 0.0 | 0.3 | 0.1 | 0.3 | both families dropped; 502 fast, 0 dials |

### As-implemented differences

1. **H4/H11 (headline gap)**: when the first-listed family's first candidate
   is blackholed and its second candidate is the only healthy target, the
   current scheduler reaches it at 250 + 251 ms (~501 ms p50) while HE waits
   out the primary family's full slice before falling back (~6001 ms p50) —
   a 5.5 s gap on the same winner (v6b), identical under 20-way concurrency
   (H11).
2. **H10 (all blackhole)**: current surfaces 504 at ~5.25 s (250 ms stagger,
   then the 5 s per-attempt cap on the second candidate, attempts run in
   parallel); HE's candidate slices span the full 12 s deadline (504 at
   ~12 s). Current gives 2.3× faster failure feedback.
3. **H7 (all refuse)**: HE advances immediately after definitive failures
   (~0.8 ms); current retains the 100 ms minimum spacing (~100.9 ms). A
   ~100 ms difference on an all-refuse failure path.
4. **H21 (dropped A sub-query, AAAA answered)**: current path — the
   W2-validated wait-for-both lookup model blocks until the 12 s deadline,
   leaving no time to dial (504 at ~12 s). HE path — Go 1.26's resolver
   exhausts two 5 s attempts on the dropped A query, then returns the
   partial AAAA answer at ~10 s and the dial succeeds (200 at ~10 s). This
   divergence is resolver-layer semantics (the production path keeps the
   pre-lookup ACL check and the W2 deadline-timeout mapping), not a
   scheduler property.
5. **H12 (ACL)**: current pre-filters the approved set before any dial (0
   dials to the denied v4b); HE dials the denied candidate once and the
   `ControlContext` gate rejects it before connect (1 denied dial per
   request). Both paths: 0 established connections to the denied candidate.

### Budgets, concurrency, and lifecycle

- B1 passed on all seven healthy dual-stack scenarios: HE p95 leads current
  p95 by at most 0.8 ms (H8); H1/H5/H6 by 0.2 ms; H16/H17 by 0.0 ms; H20 by
  0.5 ms. The ≤ 1 ms absolute differences are the resolver path overhead in
  the HE prototype, within the pre-declared 100 ms headroom.
- Concurrency (H11 20 parallel × 10 rounds; H18 10 parallel with cancel at
  200 ms): peak in-flight dials stayed within budget, per-round status
  invariants held (20/20 and 10/10), and no post-batch leaks.
- Post-batch B4 checks (0 active target connections, fd/goroutine within
  baseline + 2, no kernel-ESTABLISHED to candidates) passed for all 22
  matrix scenarios and the he300 subset. H19 verified `tcp4` family
  isolation end to end (no v6 connection ever accepted).

### HE-300 (Go default FallbackDelay)

H1/H2 winners v6a at ~0.4-0.5 ms; H3 winner v4a at 301.3 ms p50 (the 300 ms
fallback window plus connect, vs 251 ms at 250 ms); H7 502 at ~0.5 ms; H9
winners vary per run (per-run seed, 200/10). H4's slice-semantics gap is
unchanged at 6001.1 ms p50: the 250 → 300 ms delay does not affect the
dominating full-deadline slice wait.

### Decision: retain the current scheduler (`7307332`)

Marker: `W3_RETAIN_SCHEDULER_OK`.

- Happy-path equivalence: on all healthy scenarios the two schedulers
  produce the same winner and t_conn within 1 ms (B1 passed with ≤ 0.8 ms
  p95 headroom used of 100 ms). Replacement buys no latency.
- Failure feedback favors the current scheduler: all-blackhole 5.25 s vs
  12 s (H10), and first-candidate-blackholed same-family recovery 501 ms vs
  6001 ms (H4/H11). Under a 12 s CONNECT budget, HE's per-family slice can
  wait up to the full deadline on the first family before trying the second;
  the current stagger + 5 s per-attempt cap bounds every total-failure path
  to ~5.5 s.
- ACL integration is simpler as implemented: the pre-filter dials zero
  unapproved addresses, while the HE prototype needs a `ControlContext`
  gate on the actual numeric destination (one denied dial per request and a
  wider audit surface) for the same outcome.
- HE's only advantage — immediate advance after definitive refusal (~100 ms
  on all-refuse paths, H7) — does not offset the failure-feedback and ACL
  surface costs. H21's divergence belongs to the resolver layer, where the
  W2-validated wait-for-both + deadline semantics remain in force.
- Maintenance/upstream-review cost: the current scheduler is ~90 lines of
  reviewed, audited code with a small test surface (`connect_dial.go` +
  `connect_dial_test.go`); replacing it with `net.Dialer` would move the
  race into the standard library but push ACL enforcement into a
  `ControlContext` gate plus resolver injection, adding review surface for
  no measured gain. Retaining it keeps the audited boundary unchanged.
- Neither scheduler is a complete RFC 8305 implementation; the current one
  retains its documented 250 ms stagger, 100 ms minimum spacing, and 5 s
  per-attempt cap. Any future scheduler change is a separate owner change
  with its own owner-matrix regression and audit reconsideration.

### Commands and results

```bash
# run at the root of the forwardproxy checkout (then master d50ef3f; tracked tree unchanged, experiment files behind //go:build w2w3)
GOTOOLCHAIN=go1.26.0 go vet -tags w2w3 .                                  # clean
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW3Smoke' -count=1 .     # ok (HE path end-to-end)
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW3Matrix' -count=1 .    # ok, 22/22 scenarios (510.7 s)
GOTOOLCHAIN=go1.26.0 go test -tags w2w3 -run 'TestW3HE300' -count=1 .     # ok (6 scenarios x 10, 66.7 s)
GOTOOLCHAIN=go1.26.0 go build ./... && GOTOOLCHAIN=go1.26.0 go test -count=1 ./...   # ok (untagged owner suite unaffected)
git diff --check    # clean (both repositories; forwardproxy checkout and this repository)
```

## Fast Open re-enablement W4 closeout (2026-09-08)

W4 G0-G5 is complete. Exact lock `e8cc010356` passed combination run
`34155408450` with `PRODUCT_COMBINATION_OK`. Official experimental release
`v150.0.7871.63-5-native-udp-fastopen` supplied the client artifacts and the
pinned server artifact; the three server pins did not change.

The production-delegate matrix emitted `CONNECT_RESPONSE_MATRIX_OK` and
`FASTOPEN_ASYNC_FAILURE_OK`, covered the malformed H2 Location path, all 56
HTTP/HTTPS TCP cases, and native-UDP owner/M5 markers. The changed client
delegate audit boundary is reopened; historical M3-M6 `AUDIT_PASS` results
do not extend automatically.

The same Linux validation client and server ran the declared short A/B:
27/27 HTTP 204 responses with Fast Open enabled (median/mean/max 0.394/0.422/
0.547 s) and 29/29 with it disabled (0.381/0.412/1.105 s). Both runs stayed at
`NRestarts=0`. These samples establish bounded operation under the workload,
not a statistically significant performance difference; the proposed 24-48 hour
observation was removed from the acceptance contract.

The validation client runs `native-udp-client.service` with binary SHA
`cdcff06ca5ecaabf839e298b9c1f298482af763c9c7e1f8c8828b83c218e49df` and rollback
`naive.disabled-20260908` (SHA
`31dddee0a07d89ddb865d0384beec1191fbdd968ac9b7651b14a4bdafb37253d`). The
router runs `/usr/bin/native-udp` SHA
`c9b2f8411b03f64bada9c13846177392104fd9656e4fca3fe0445cda8ce6c145` and rollback
`/var/lib/proxy-private/native-udp.pre-fastopen-20260908` (SHA
`993bdf31785839f0041f1a82ed746f103925c43096daad3bd5fd5ca44f2e5f68`). The
production Caddy binary already matched the candidate server artifact, so it
was not restarted. No sing-box process, binary, or configuration changed.

Candidate archive SHA256 values are Linux x64
`94569e6fceca6e1835c623d510a81b9a2a1689bd4043ab201417741cbab636ca`,
OpenWrt x86_64
`b8aacca64326a9ae5de7f2eaa7fca4c350725d853bf2e0a15f9a075f1e494ca5`,
and server
`6fc8c07c7d8e19b73d6f020fd3dacd87745bbf541beb13564714f179c34a6cea`.

Historical release-4 evidence follows. That release contained client
`b652d34aa5` and forwardproxy `7307332`. Both fixes are pushed to their
owners' `master`. Product server release run `33961210559` and client Build
`33961210543` passed before deployment; the client completed 50/50 jobs,
producing all 46 packages. All 47 client/server package digests and contained
binary hashes were verified. The release includes `release-manifest.json`,
complete `SHA256SUMS` and server module provenance.
The deployed server artifact embeds Go 1.26.0, the locked quic-go
pseudo-version, and `http.handlers.forward_proxy`. The deployment page records
the exact artifacts, rollback files, validation results, and the fact that all
CONNECT-UDP metrics are process-local and reset on restart.

Release qualification commands and results:

```bash
scripts/verify-product-lock.sh
gh workflow run dependency-lock-check.yml --ref master
gh workflow run product-combination.yml --ref master
```

Final lock run `33960919829` and combination run `33962958046` passed. The
combination checked out the exact locked source commits and emitted
`PRODUCT_COMBINATION_OK` after CONNECT response/Fast Open regressions, M1-M3,
56 TCP cases, server normal/race tests and M5 G1-G5, including independent
HTTP/3 application and default certificate-verifier/trust-cleanup markers.
The exact downloaded release server also passed `scripts/test-m4-g5-server.sh`
locally with the explicit hostless certificate fixture, emitting
`M4_G5_SERVER_INTEROP_OK` after real 125-second idle, restart and privacy tests.

CI corrections: `1ca173d3f2` supplies missing `GH_TOKEN` for pin publication
checks; `451ab9504a` supplies `GO_BIN=go` so M5-G2 does not use its historical
placeholder path. Earlier failed runs are not qualification evidence. The
release tag remains `1ca173d3f2b6e82af543ee4d6c98dfd332f1c1bb`, with no `src/`
diff from the locked client. `c6a4511fa1` corrects future server checksum path
prefixes; the current release has a complete basename-only checksum list.

Deployment finished with timestamped rollback binaries and unchanged service
configuration. The router's first attempt rolled back on single-datagram UDP
DNS timeout; retry and two subsequent full probes passed. Further old/new
sampling found DNS timeouts in both versions (10/12 and 9/12 replies),
consistent with the user's confirmed randomly lossy test link. Loss location
was not established. Exact online results, rollback paths and cleanup are in
`current-deployment.md`. No new independent audit or M7-G5 closure is claimed.

All older M7 SHAs, temporary binaries, and benchmark deployments in the
sections below are historical evidence only. Where they conflict with this
section, use the product lock and current deployment manifests.

## Documentation privacy cleanup (2026-09-07)

Public deployment records contained real endpoints and operator-specific
paths. These were replaced with `example.invalid` hosts, documentation-range
addresses, and generic paths in the current tree and affected history.
Real deployment inventory and the replacement rules are retained outside Git.

- The rewrite changed 146 commits, preserving unaffected upstream history.
  An atomic push with an exact lease for each ref updated `master` and the
  `v150.0.7871.63-{2,3,4}-native-udp-m7` tags. The rewritten master checkpoint
  is `c30e486ddc8ede6997adda82f4211d90225b4149`.
- Comparing the old and rewritten master checkpoints found no changes in
  `src/`, `scripts/`, `tests/`, `.github/`, or `release/product.lock.json`.
  Published binaries and their hashes were not changed or reissued.
- Scanning all locally reachable historical blobs under `docs/`,
  `release/manifests/`, and `AGENTS.md` found zero remaining matches for the
  identified sensitive values/path patterns, both in the rewritten mirror
  and the synchronized main checkout. This is a scoped check, not a claim
  that every historical object or external artifact has been audited.
- The same identified endpoint values were not found in the other three
  repositories' current trees, forwardproxy's fork history, or the checked
  quic-go history. Caddy is a shallow checkout; its successful history check
  covered documentation/Markdown only, not a complete server-history audit.
- Upstream PRs #825, #826, and #827 retain the technical validation summaries
  and limitations, but their bodies no longer link to historical deployment
  documents. The checked fork issue/release bodies contained no matches for
  the identified values. All seven published JSON/text/checksum assets found
  across the four forks' releases also passed that same-value scan.

GitHub cleanup is **incomplete**: a request for an old deployment-document SHA
still returned the original sensitive content after the force push, and the
fork's read-only `refs/pull/1/head` and `refs/pull/2/head` still point to old
history. GitHub must remove the affected PR references and cached commit/file
views; edited PR-body history and generated source archives also need review.
A private GitHub Support request was submitted on September 7. The portal
confirmed successful submission and an open ticket; the complete submitted
body was verified against the prepared request. The receipt and ticket URL
are retained outside Git. Platform cleanup remains pending and must be
verified after GitHub responds. Rewriting branches/tags alone does not
establish server-side erasure. Existing clones must synchronize to the
rewritten refs and must not merge or push the old history back.

The source identifiers in existing build manifests, product locks, and
historical test records remain the original build inputs. The corresponding
sanitized source revisions are:

| Original NaiveProxy revision | Sanitized revision | Role |
| --- | --- | --- |
| `c86e73859ea1a65eac467378ba3188edc5145a98` | `61a81d3f138dc8d24aaaf8505a7fddcc86018af7` | Release 4 locked client source |
| `1ca173d3f2b6e82af543ee4d6c98dfd332f1c1bb` | `aed6650e61a256191946baf60d1e63907b9f893d` | Release 4 tag target |
| `742b89aa24131749b62856e5ed9189273a32f26e` | `595a53f34636c9116ffc3f4ae9bf1b194f5ce419` | Release 3 locked client source |

These aliases do not mean existing binaries were built from the new SHAs.
Future source qualification/release work must use sanitized inputs and issue
a new lock and provenance record; do not silently relabel old artifacts.
No runtime regression matrix was repeated for this documentation-only change,
and no historical audit conclusion or milestone completion was changed.

## Fast Open upstream PR submissions (2026-09-06)

### U3 Chromium submission update (2026-09-08)

The SPDY CONNECT invalid-response-header null dereference fix was submitted as
[Chromium CL 8368721](https://chromium-review.googlesource.com/c/chromium/src/+/8368721),
Change-Id `Ic373d1597b8762635652d85fd15b10b21c640d80`. Gerrit readback reports
status `NEW` (awaiting review/merge), current revision
`d66a811998e6906207b7e3dc4f8423b584bf8929`. The submitter reports that its
six-line null guard before dereferencing headers in `DoReadReplyComplete`
matches the fork/PR patch byte for byte; fork `master` already contains the
equivalent fix at `4de6443f5a`.

[NaiveProxy PR #827](https://github.com/klzgrad/naiveproxy/pull/827) was closed
after the maintainer reply linking the Chromium CL. GitHub readback confirms
`CLOSED`, no merge, closed at `2026-09-08T01:58:18Z`. Upstream NaiveProxy is
expected to receive the fix through a future Chromium import after CL merge;
neither merge nor import is claimed complete. Track both before removing the
fork patch.

This update records upstream submission status only. W4 G4/G5 remain complete;
no runtime source, product lock, deployment, or audit result changed, and no
regression rerun is claimed. Public records omit deployment endpoints,
credentials, and private operator paths.

### Original submission evidence

W1's three focused fixes were submitted to `klzgrad/naiveproxy:master` from
separate worktrees based on upstream
`769aaa53c39190fbfd6cfb223f17bb9f9cf9d3e6`. All three were open, non-draft PRs
at submission; the September 8 U3 update below supersedes #827's review status.

| Fix | Source commit(s) | Submitted commit | Upstream PR |
| --- | --- | --- | --- |
| U1: wake buffered body reads after initial headers | `c8ebb943bd` | `69be1a7dc079573f8cf5086dd1afd9c10d66d495` | [#825](https://github.com/klzgrad/naiveproxy/pull/825) |
| U2: complete pending reads on Fast Open response failure | `eeb2b8ddc1` + `153de92c8e` | `bfdd87ce0856c527e2d57ca2f71730a4411cb725` | [#826](https://github.com/klzgrad/naiveproxy/pull/826) |
| U3: propagate invalid H2 CONNECT response headers | `afce211960` | `43b74c3e4a5fe57a5197e069d281a765d07f50ab` | [#827](https://github.com/klzgrad/naiveproxy/pull/827) |

Per the user's instruction, existing documented validation was reused rather
than repeating previous reproductions and full regressions:

- U1's historical deployment record at `5c3a627a2d` reports one timeout in
  eight old-client downloads, eight of eight fixed-client downloads, then
  three complete 15,451,894-byte downloads after replacement. This is field
  comparison evidence, not a deterministic unit-test claim.
- U2's evidence includes the September 4 delayed-502/no-FIN fixture, three passing runs of
  `tests/fastopen_async_failure.sh` (`FASTOPEN_ASYNC_FAILURE_OK`), and the
  subsequent September 5 run retaining a legacy Fast Open test delegate.
- U3 cites the September 4 source review and full 56-case TCP regression.
  Its PR identifies source inspection as the basis for the duplicate-Location
  trigger. A dedicated malformed-H2 reproduction was not run.
  Different duplicate Location values provide a source-level
  conversion-error trigger; this is not newly executed runtime evidence.

The PRs originally linked historical documentation; those links were removed
during the September 7 privacy cleanup above. Their bodies retain the
historical validation summaries and limitations. U2 also references the
related closed upstream PR #808 and identifies the exact controlled HTTP 502
trigger; it does not claim to reproduce that report's transport error.

On September 7, all three PR bodies were updated to the user-approved text
centered on the defect, fix, and validation results. They retain the 1/8
timeout versus 8/8 completion comparison, three controlled failure-test
passes, 56-case TCP results, #808 reference, and U3's source-inspection basis.
Extraction details and repeated no-rerun statements were removed from the PR
bodies; the historical verification boundary remains recorded here. API
readback matched each approved body exactly and confirmed unchanged PR heads
and no identified sensitive values or historical deployment-document links.

New verification was limited to extraction and publication checks:

```bash
# Run in each corresponding upstream worktree.
git diff --check upstream/master HEAD
git diff --stat upstream/master HEAD
# U2: exact final socket source parity with the previously verified fix.
git diff --exit-code 153de92c8e HEAD -- \
  src/net/quic/quic_proxy_client_socket.cc \
  src/net/quic/quic_proxy_client_socket.h
# U3: exact final source parity with the previously verified fix.
git diff --exit-code afce211960 HEAD -- \
  src/net/spdy/spdy_proxy_client_socket.cc
# U1: run each command in the U1 worktree and compare the patch IDs.
git show c8ebb943bd --format= -- src/net/quic/quic_chromium_client_stream.cc | git patch-id --stable
git show HEAD --format= -- src/net/quic/quic_chromium_client_stream.cc | git patch-id --stable
```

Diff/whitespace checks pass; U2/U3 source comparisons have no differences.
Both U1 patch IDs are `13c978f75063ba5c84cb2d1de3cc97a8b5712fc6`.
Each PR contains one commit and only its named runtime file(s), with no UDP,
BBR, release/deployment, or Fast Open policy-disable changes. GitHub PR
queries verified the target branch, exact head, file scope, open/non-draft
state, and empty check rollup at submission. No fresh upstream build/runtime
test or upstream CI pass is claimed. No new audit verdict, product source
change, release-lock update, or deployment occurred.

The [follow-up plan](connect-followup-execution-plan.md) now tracks upstream
review separately; DNS/address-order analysis (W2), Go Happy Eyeballs
comparison (W3), and the separate CONNECT policy assessment remain pending.

## CONNECT follow-up planning record (2026-09-06)

At the initial planning checkpoint, source and RFC review were complete and
PR submission was pending. The submission record above supersedes that W1
state; W2/W3 remain in the [CONNECT follow-up execution plan](connect-followup-execution-plan.md).
The agreed order is existing correctness fixes upstream first, DNS/address
ordering analysis and tests second, and a measured comparison with Go's
built-in Happy Eyeballs third.

Reviewed client fixes: `c8ebb943bd`, `eeb2b8ddc1` + `153de92c8e`, and
`afce211960`. The corresponding paths were also inspected in
`klzgrad/naiveproxy` (default-branch HEAD observed as
`769aaa53c39190fbfd6cfb223f17bb9f9cf9d3e6`); the missing notification and
error-handling paths remain upstream candidates. The upstream
`klzgrad/forwardproxy` `naive` source still sends CONNECT success before
target dialing. These are source observations, not fresh upstream
before/after reproductions or a claim of security exploitability.

The server scheduler at `7307332` uses Go TCP dials on ACL-approved numeric
addresses, with its own address interleaving, staggered starts, winner
selection, and cancellation. It implements RFC 8305 connection-racing
principles, not the complete Happy Eyeballs v2 algorithm. Resolution waits for
`LookupIPAddr`; ordering is inherited from the resolver before ACL filtering
and interleaving. The inspected Go 1.26.0 pure-Go resolver includes RFC 6724
sorting; its built-in TCP Happy Eyeballs races two serial address-family
queues. Cross-platform resolver behavior and comparative performance are
still to be tested.

All four worktrees were checked with `git status -sb` and remain on `master`.
That initial checkpoint recorded a plan only; it added no runtime change,
release lock, deployment, upstream PR, test result, or audit verdict.

## CONNECT issue fixes (2026-09-05, pre-release validation)

Scope: [NaiveProxy #4](https://github.com/ssharkkky/naiveproxy/issues/4) and
[forwardproxy #1](https://github.com/ssharkkky/forwardproxy/issues/1).
Recovered the interrupted Codex session and completed its owner fixes:

- Client `b652d34aa5b8f5b19cdd20511748b7bcac9f58db`: stop enabling H2/H3
  Fast Open from cached padding capability, strip the internal `fastopen`
  override, and wait for the actual CONNECT response. No `NaiveConnection`,
  padding format, UDP protocol, or certificate-verifier change.
- Server `7307332b312f29ce5f5f1cb638e4a5b993e95442`: send `200` only after
  a successful target dial, propagate request cancellation, return `502` for
  connection failures and `504` for timeouts. Race only resolved numeric
  addresses that passed ACL checks, alternating families at 250 ms intervals
  (100 ms minimum after failure), with 5 s per attempt and a total configured
  deadline including DNS. Cancel and close losers; retain upstream tunnel
  contexts after successful establishment. This is the connection-racing
  portion of RFC 8305, not a claim to implement its full DNS scheduling model.
- Caddy `0ea5700f64254ba24e39d57b1febece2fa34927e` and quic-go
  `c308178d8c77061d5e261ce9df37f2bcc0ab22bf` remain unchanged.
- Test repairs: retain delayed QUICHE response objects for the lifetime of
  the fixture backend; the previous stack object expired before its alarm.
  Preserve the F1 regression using a test-only legacy Fast Open delegate.
  Allow `NAIVE_BUILD_DIR` in owner scripts. Forwardproxy `d50ef3f` disables
  test-only automatic redirects to occupied port 80. Client `9545c56f5c`
  separately verifies the server's 128-association `503`, since the SOCKS
  admission probe reaches only the client's independent 32-association cap.

Verified client commands from the repository root:

```bash
export NAIVE_BUILD_DIR="$PWD/src/out/M7Linux"
export CCACHE_DIR="$PWD/src/.host_tool_cache"
export GOTOOLCHAIN=go1.26.0
export GOMAXPROCS=2
tests/connect_response.sh
tests/fastopen_async_failure.sh
tests/masque_g1_smoke.sh
tests/masque_g2_naive_tunnel.sh
tests/masque_g3_basic_auth.sh
tests/masque_g5_lifecycle.sh
tests/socks5_udp_m2.sh
tests/socks5_udp_m3.sh
python3 tests/basic.py --naive="$NAIVE_BUILD_DIR/naive" --server_protocol=https
python3 tests/basic.py --naive="$NAIVE_BUILD_DIR/naive" --server_protocol=http
```

All pass. `CONNECT_RESPONSE_MATRIX_OK` covers delayed H2/H3 responses
`200`, `502`, and `504`, with two sequential CONNECTs and exactly one
completion each. Both exchanges wait approximately 500 ms; errors propagate
as `ERR_TUNNEL_CONNECTION_FAILED` (-111). The fixture leaves streams open,
so completion cannot depend on FIN. Legacy `FASTOPEN_ASYNC_FAILURE_OK`
remains green. TCP: 56/56. Codec/state/association/backend/congestion unit
binaries pass; fuzz marker `M6_G4_CODEC_FUZZ_OK` reports 250,000 iterations
and 11,023 valid cases. Seeded backend lifecycle covers 2,000 iterations.

Server verification: Go 1.26.0 `go test -p 2 -count=1 ./...` and
`go test -race -p 2 -count=1 ./...` pass. The built
`/tmp/naive-connect-caddy-final` passes `scripts/test-m4-g5-server.sh` with
`CADDY_BIN` naming that binary, `GO_BIN` naming Go 1.26.0, and
`M4_CADDY_CONFIG=/tmp/naive-connect-g5.Caddyfile` (local explicit certificate,
hostless listener, no port-80 redirect). Markers include
`M6_H3_TCP_PADDING_INTEROP_OK`, `M4_G5_RESOURCE_LIMIT_OK`, real idle expiry,
restart, privacy, and `M4_G5_SERVER_INTEROP_OK`.
`go vet ./...` is not green: all remaining copylocks and testing-goroutine
warnings were independently reproduced at baseline `4265c663`; this fix
removes the copied-lock receiver on `dialContextCheckACL`.

All M5 product gates also pass with the fixed client and server. Set the
environment above plus the following, then run each gate separately:

```bash
export M5_FORWARDPROXY_DIR=/path/to/forwardproxy
export M5_CADDY_DIR=/path/to/caddy
export M5_CADDY_BIN=/tmp/naive-connect-caddy-final
export M5_EXPECTED_FORWARDPROXY=7307332b312f29ce5f5f1cb638e4a5b993e95442
export M5_EXPECTED_CADDY=0ea5700f64254ba24e39d57b1febece2fa34927e
export M5_EXPECTED_CLIENT=b652d34aa5b8f5b19cdd20511748b7bcac9f58db
export GO_BIN=/path/to/go1.26.0/bin/go
tests/m5/g1_cross_repo_echo.sh
tests/m5/g2_product_matrix.sh
tests/m5/g3_product_security.sh
tests/m5/g4_lifecycle_matrix.sh
tests/m5/g5_production_binary.sh
```

The expectation variables name the runtime revisions; the subsequent
test-only commits listed above do not change those runtime inputs. G1/G2
prove IPv4/IPv6/domain UDP, DNS, concurrency, and the independent HTTP/3
application. G3 includes client and server admission, `503`, authentication,
policy, non-QUIC rejection, and privacy. G4 proves control close, two server
restarts, outer-QUIC recovery, client idle, and no replay. G5 reports
`M5_G5_UNTRUSTED_CERT_REJECTED_OK`, `M5_G4_SERVER_IDLE_RECONNECT_OK`,
`M5_G5_TRUST_CLEANUP_OK`, `M5_G5_DEFAULT_CERT_VERIFIER_OK`,
`M5_G5_PRODUCTION_BINARY_OK`, `M5_G5_H3_DATAGRAM_EVIDENCE_OK`, and
`M5_G5_NO_PADDING_BASELINE_OK`. Interrupted attempts are excluded; the final
G3 and G5 executions completed in a supervised temporary process with the
aggregate `CONNECT_PRODUCT_REGRESSION_OK`. `git diff --check` passes.

Audit impact: the changed CONNECT establishment policy and TCP dialer are
outside the immutable M3-M6 audit ranges. The issue regressions reconsider
those boundaries; they do not extend the historical `AUDIT_PASS` verdicts.
No new independent audit or cross-platform release qualification is claimed,
and deferred M7-G5 remains deferred. Live results and the explicit local
server-build exception to the release lock are in `current-deployment.md`.

## Fast Open re-enablement W4 G2 evidence (2026-09-08)

The production `NaiveProxyDelegate` now restores the learned-padding
`fastopen: 1` request header. The internal CONNECT policy is enabled only
after padding capability is learned; the existing U1/U2/U3 correctness fixes
and the #819 accept-resource backoff remain in the same client. The test-only
legacy delegate is retained only for historical comparison and is not used by
the qualification scripts.

The runner and fixtures provide the following current evidence using the
`src/out/M7Linux` Release-compatible build. Production source remains at
locked client `4de6443f5a`; later test-only commits are identified below:

- `tests/connect_response.sh` passed `CONNECT_RESPONSE_MATRIX_OK`. H2 and H3
  covered delayed `200`, `502`, and `504` responses; cold exchange 1 waited
  about 500 ms, learned-padding exchange 2 completed in about 2–6 ms, and
  exactly one connect callback was observed. Non-2xx cases emitted
  `FASTOPEN_PENDING_READ_ERROR_OK error=-111 callbacks=1`.
- The H2 fixture's `duplicate-location-after-first` scenario passed as
  `CONNECT_RESPONSE_h2_duplicate-location_OK`, proving the malformed response
  reaches the production path after padding has been learned. The U3 guard is
  now at `DoReadReplyComplete` immediately before `response_.headers` is
  dereferenced; `OnHeadersReceived` retains the upstream-style local `rv` and
  `DCHECK_NE(rv, ERR_INCOMPLETE_HTTP2_HEADERS)` without propagating `rv` there.
- `tests/fastopen_async_failure.sh` passed `FASTOPEN_ASYNC_FAILURE_OK` with
  `DELEGATE_MODE production`, delayed non-2xx response, open stream, and the
  pending application read completed exactly once with a negative error.
- Both focused scripts were rerun after the final U3 alignment on 2026-09-08
  with `NAIVE_BUILD_DIR=$PWD/src/out/M7Linux`; they again emitted
  `CONNECT_RESPONSE_MATRIX_OK` and `FASTOPEN_ASYNC_FAILURE_OK`.
- The fixture startup diagnostic in `tests/connect_response.sh` is recorded in
  client commit `b80c15106a`; the same rerun now reports an exited fixture's
  log and build/protocol/scenario context instead of a bare `kill` failure.
- The dedicated H3 body wakeup probe is implemented in client commit
  `0742e35197` and passes `tests/fastopen_body_wakeup.sh` with
  `FASTOPEN_BODY_WAKEUP_OK`; the learned-padding second CONNECT reads and
  verifies the exact `body-wakeup` response body from one pending read.
- Serial `python3 tests/basic.py --server_protocol=http` and `https` runs
  both passed their complete 28-case rows (56 total). Native UDP owner scripts
  `masque_g1_smoke.sh`, `masque_g2_naive_tunnel.sh`,
  `masque_g3_basic_auth.sh`, `masque_g5_lifecycle.sh`, `socks5_udp_m2.sh`,
  and `socks5_udp_m3.sh` also passed on the same candidate build.
- The dedicated `tests/fastopen_cancel.sh` probe passed `FASTOPEN_CANCEL_OK`.
  After a cold delayed 200 response learned padding, two hot CONNECTs completed
  before the delayed response; pending reads were canceled with the active
  callback owner and with the callback owner destroyed before socket close,
  with zero callbacks observed in both cases.

September 8 supplemental commands (not part of the September 4/5 runs):

```bash
NAIVE_BUILD_DIR="$PWD/src/out/M7Linux" tests/fastopen_body_wakeup.sh
NAIVE_BUILD_DIR="$PWD/src/out/M7Linux" tests/fastopen_cancel.sh
```

The H3 body fixture verifies delivery of the expected body after early CONNECT
completion and a pending read. It does not independently assert the internal
body-before-header callback order. The H3 cancellation fixture does not cover
destruction from inside an executing error callback.

Independent scoped review of cancellation-test commit `6b25de1439` returned
`AUDIT_PASS`, with no blocker/high/medium findings. The reviewer independently
built the runner and ran the cancellation command above, observing
`FASTOPEN_CANCEL_OK`. Its non-blocking observation was possible parallel-test
contention on fixed fixture ports. This conclusion is limited to the reviewed
test change; historical M3-M6 `AUDIT_PASS` does not extend automatically to W4.

### Release deployment recheck (2026-09-08)

Before publication, the unpublished W4 commits were sanitized to remove local
operator paths. Their production source and test code are unchanged. The
evidence above retains the original review identifiers; public equivalents are:

| Original local revision | Sanitized revision | Evidence |
| --- | --- | --- |
| `b80c15106a` | `1c7825c51b` | Fixture startup diagnostics |
| `0742e35197` | `953dd5c6c2` | Body wakeup probe |
| `6b25de1439` | `2a094af9ab` | Cancellation probe and scoped review |
| `853e745fb4` | `efb0a3fda7` | Supplemental test ledger |

The unpublished history passed a scan for known real deployment endpoints
and local operator paths. The published release tag and product lock were
not rewritten.

The published release-5 client build `34159149117`, server build `34159149063`,
and product combination `34155408450` were rechecked as successful. Both live
clients and the server process executable match the binary SHA256 values in
the current manifests. The eight post-release commits through `853e745fb4`
change only tests, fixtures, and documentation; no production source update
requires another release or replacement of the identical binaries.

The existing independent deployment probe ran through each client's actual
SOCKS5 listener, with the default production certificate verifier retained:

| Role | TCP requests | UDP DNS queries | Unreachable CONNECT elapsed |
| --- | --- | --- | --- |
| Production client | 8/8 | 4/4 | 5.153 s |
| Validation client | 8/8 | 4/4 | 5.143 s |

Both runs emitted `DEPLOYMENT_TCP_OK`, `DEPLOYMENT_UDP_DNS_OK`, and
`DEPLOYMENT_CONNECT_FAILURE_BOUNDED_OK`. These are individual smoke samples,
not a universal failure-time bound. The validation client also passed four
HTTPS checks (two HTTP 204 and two HTTP 200). No service restart or sing-box
operation was performed during this recheck. Real endpoints and private
operator paths are excluded from this record.

## Fast Open audit fixes F1/F2 and regression (2026-09-04)

The release audit of the Fast Open response-order hotfix `c8ebb943bd`
named two release blockers. Both are now fixed, regressed, and deployed:

### F1 — QUIC Fast Open async failure leaves pending read uncompleted

Root cause: a Fast Open `Connect()` can return OK before the CONNECT
response arrives, after which the app issues a `Read()` on the tunnel
stream. If the response then fails asynchronously (non-200) and the server
does not FIN the stream, the pending read waited for a stream close that
never came.

Fix (`153de92c8e`, `src/net/quic/quic_proxy_client_socket.{h,cc}`):
`FailPendingReadOnFastOpenFailure(rv)` completes the pending read with the
tunnel failure and resets the response stream (`QUIC_STREAM_CANCELLED`);
called from both Fast Open failure branches (`STATE_READ_REPLY_COMPLETE`,
`STATE_PROCESS_RESPONSE_CODE`). The callback may destroy the socket, so it
runs last; the `DoLoop` caller sets `rv = ERR_IO_PENDING` immediately after,
and the loop condition short-circuits before any member is read.

### F2 — SPDY OnHeadersReceived null dereference

Root cause: `SpdyProxyClientSocket::OnHeadersReceived` ignored the return
value of `SpdyHeadersToHttpResponse`; on a malformed CONNECT response,
`response_.headers` stayed null and `DoReadReplyComplete` dereferenced it.

Fix (`afce211960`, `src/net/spdy/spdy_proxy_client_socket.cc`): fail closed
— in the Fast Open path disconnect and let `OnClose` complete any pending
data read; otherwise resume the state machine with the error so the connect
callback observes it. No H2 proxy fixture exists in this repository; verified
by inspection plus the full regression matrix (the changed path is
fail-closed and cannot alter a previously-successful response).

### Regression (deterministic)

`tests/fastopen_async_failure.sh` drives `naive_fastopen_fail_runner` (real
production `NaiveProxyDelegate` + `MockCertVerifier` test context, two
sequential TCP tunnel exchanges over `quic://`) against the masque server
with `--fail_connects` (every CONNECT answered 502 after a 500 ms delay,
without FIN, stream left open). Exchange 1 (no padding state, not Fast Open)
and exchange 2 (Fast Open, application I/O pending when the failure
arrives) must both fail with a negative error inside the 15 s watchdog; a
hang fails the test. Server log must contain exactly two `CONNECT_ACTION
fail_502` lines.

Command (from repository root):

```bash
tests/fastopen_async_failure.sh
```

Result: GREEN, 3 consecutive runs (`FASTOPEN_ASYNC_FAILURE_OK`).

### Full regression matrix (Linux x64 Release, fixed source)

- `tests/basic.sh`: **56/56 PASS** (28 https + 28 http), 0 FAIL.
- `tests/masque_g1_smoke.sh`: GREEN (`MASQUE_G1_SMOKE_OK`).
- `tests/masque_g2_naive_tunnel.sh`: GREEN (`MASQUE_G2_NAIVE_TUNNEL_OK`).
- `tests/masque_g3_basic_auth.sh`: GREEN (`MASQUE_G3_BASIC_AUTH_OK`).
- `tests/masque_g5_lifecycle.sh`: GREEN (`MASQUE_G5_LIFECYCLE_OK`).
- `tests/socks5_udp_m2.sh`: GREEN (`SOCKS5_UDP_M2_OK`).
- `tests/socks5_udp_m3.sh`: GREEN (`SOCKS5_UDP_M3_OK`).
- `tests/fastopen_async_failure.sh`: GREEN (new, above).
- `naive_quic_congestion_test`: GREEN (`M7_G1_CUBIC_NO_TAG_PRESERVED_OK`,
  `M7_G1_QUIC_CONGESTION_PARSER_OK`, `M7_G1_CLIENT_BBR_OK`).
- Unit binaries: `naive_socks5_udp_test`,
  `naive_socks5_server_socket_state_test`, `naive_socks5_udp_association_test`,
  `naive_connect_udp_backend_test`, `naive_socks5_udp_fuzz_test` (250k
  iterations, 11023 valid cases): all GREEN.

`git diff --check` clean.

### Sibling observations (documented, not fixed this cycle)

- Datagram socket: a pending datagram read on the CONNECT-UDP socket is
  already bounded by `OnStreamClosed` plus the idle timeout; no change
  required for this audit.
- IPv6 literal proxy (latent, test-only impact): `url::SchemeHostPort`
  constructed with `CHECK_CANONICALIZATION` (e.g. in
  `DoQuicProxyCreateSession` / `ProxyJob::DoCreateProxySession`) silently
  produces an empty host for an unbracketed IPv6 literal such as `::1`,
  which the resolver then rejects with `ERR_NAME_NOT_RESOLVED` (-105).
  Production is unaffected (the configured proxy is a hostname); tests must
  bracket IPv6 literals or use IPv4 loopback.

### Build and deployment

The following is the historical emergency-build record for the Fast Open
incident. It was superseded by the `v150.0.7871.63-3-native-udp-m7` release
artifacts and current deployment recorded at the top of this ledger.

- OpenWrt x86_64: `out/OpenWrt/naive`, SHA256 `6e3a6655415b0f0e4481e1d524f906aa382d2314c5d96001afd7dfabc4b063b2`,
  deployed to `endpoint-2.example.invalid` (`/usr/bin/native-udp`) with pre-deployment
  backup `native-udp.pre-F1F2-20260904-155351`; Round-2 live validation
  green (details in [`current-deployment.md`](current-deployment.md)).
- Linux x64 validation build: `out/Release/naive`, SHA256
  `d2fbfe24ce1078341237cd45336ba5a8f51a6f377480a5e212d5c4751104c11e`.
- Commits merged to `master`: `153de92c8e` (F1), `afce211960` (F2),
  `742b89aa24` (regression), and `ccea283015` (the initial product-lock pin).

## Overall milestone status

| Milestone | Status | Verified result | Next gate |
| --- | --- | --- | --- |
| M0 — baseline and guardrails | Complete | Stable Chromium 150 tag, development branch, Release build, TCP baseline | None |
| M1 — Chromium integration spike | Complete and independently audited | Real IPv4/IPv6 tunnel, auth echo, lifecycle and NetLog evidence; `agy` returned `AUDIT_PASS` | None |
| M2 — SOCKS5 UDP ingress | Complete, audited, and committed | Codec, handshake, real relay, fake backend, deterministic lifecycle and 56 TCP regressions pass; `agy` returned `AUDIT_PASS`; commit `fe817a87` | None |
| M3 — native UDP client data path | Complete and independently audited | Full client path, controlled interoperability, recovery, all limits/lifecycle cases, complete regressions, three stress runs; `agy` returned `AUDIT_PASS` with zero blocker/high/medium | None |
| M4 — production server path | Complete and independently audited | Reproducible builds, full server/client regressions, independent RFC 9298 matrix, lifecycle, race, privacy, artifact checks, and `AUDIT_PASS`; final server commit `8f044e2`, Caddy `cce894a8` | None |
| M5 — end-to-end MVP | Complete and independently audited | Full product matrix, shipped default-verifier client, lifecycle/no-replay, complete regressions, three fresh-root repetitions, artifact closeout, and `AUDIT_PASS` | None |
| M6 — hardening and release candidate | **Complete**; G0-G6 closed; release candidate qualified | All G0-G6 gates closed; macOS arm64, Linux x64, Windows x64, and Android arm64 platform qualification verified; cross-platform wire gate `13df84bfd9` passes; independent release-candidate audit `AUDIT_PASS` (`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK`); merged to `master` at `fcf3bb36f3` | None |
| M7 — BBR congestion control | **G4 complete; G5 intentionally deferred** | Correctly combined client/server BBR passed the fixed-loss reference parity gate; `M7_G4_PARITY_OK` recorded below. Full regression/audit G5 is outside the requested closeout scope. | None |

M1 is complete as an integration spike. M2 supplies the local SOCKS5 UDP
ingress and retains its test-only echo/no-backend modes. M3 G0–G6 compose
that ingress with the real M1 CONNECT-UDP tunnel in production while keeping
the M2 runner independent, and the independent final audit passed. M6 is
complete; M7 is complete through G4 with G5 intentionally deferred.

### Overall progress estimate

- Milestone count: M0–M6 are complete, 7 of 7 milestones, or 100%.
- The M6 release candidate is qualified: independent audit `AUDIT_PASS` and
  marker `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closed M6; merged to `master`
  at `fcf3bb36f3`. Not yet a production release (see
  `native-udp-release-guide.md`).
- Chromium-driven native UDP client: M1-M3 are independently audited; the M5
  production-context ordering fix `333b7cb253` passed the complete owner matrix
  and is included in the completed M5-G6 audit boundary.
- Production Caddy/`forwardproxy` native UDP server: 100% complete and
  independently audited.
- End-to-end product MVP: 100% complete and independently audited. Release
  hardening is complete (M6 closed; release candidate qualified at `fcf3bb36f3`).

Current remaining planning range:

| Remaining milestone | Estimated effort |
| --- | ---: |
| (none) | 0 |
| **Total remaining** | **0** |

All M0-M6 milestones are complete. The native UDP release candidate is
qualified: the independent G6 audit returned `AUDIT_PASS` and the final marker
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closed M6; the full stack is merged to
`master` at `fcf3bb36f3`.

## Historical M7 implementation evidence and G4 closeout (G5 deferred)

M7 G1-G4 completed under [`m7-execution-plan.md`](m7-execution-plan.md).
The SHAs in this section name the revisions used to close individual gates;
they have since been superseded by the four-repository product lock at the top
of this file. They remain useful audit history, not deployment provenance.

Verified on 2026-09-02:

```text
quic-go fork: go build ./..., go vet ./..., go test ./...                 PASS (in fork worktree)
quic-go fork (current `f84ad47`): go test ./...                          PASS (27 packages)
client `naive_quic_congestion_test` (Release binary via musl loader)       PASS; `M7_G1_CLIENT_BBR_OK`
Caddy fork:   go test ./..., go vet ./..., go build ./cmd/caddy           PASS
forwardproxy: focused M4 G0-G4 tests and go test ./...                    PASS
forwardproxy: go test -race ./...                                         PASS
combined Caddy+forwardproxy build with explicit quic-go replace            PASS
```

The combined server binary linked against the exact quic-go pseudo-version
`v0.59.1-0.20260901171950-f84ad47630af`. The G5 cross-process server script
was not run to completion because the fixed test Caddyfile attempted to bind
`:80`, already occupied on this host; no existing process was disturbed.

The independent scoped server validation found no protocol or CUBIC-path
regression. The former publication/pin blocker is closed by the published
Caddy commit and the two direct forwardproxy pins; a formal M7 scoped
`AUDIT_PASS` has not been run. The full M1-M6/56-case matrices and
cross-platform rows belong to G5, which is intentionally deferred after the
G4-only closeout.

### M7 runtime smoke (2026-09-02)

An isolated deployment under `/var/lib/proxy-private/native-udp-m7-test` was run on the
authorized client/server hosts and then removed. It used separate ports
18443/18444 and separate SOCKS listeners 11080/11081; existing Naive 1080,
web listeners, Hysteria, Xray, frps, and Docker services were not changed.

- BBR and CUBIC HTTP/3 CONNECT-UDP probes each returned 32/32 paced UDP echo
  datagrams (1,200-byte payloads).
- Through each SOCKS listener, three authenticated downloads of the same
  10 MiB file completed with HTTP 200. BBR speeds were 3.66–3.94 MB/s and
  CUBIC speeds 3.68–4.07 MB/s on this clean path; this is a smoke result, not
  the required lossy-path G4 parity evidence.
- A burst test that sent hundreds of datagrams at once reached the intentional
  per-target queue bound (16); paced testing avoided that bound. No protocol
  failure was inferred from the burst result.
- Cleanup verified no `/var/lib/proxy-private/native-udp-m7-test` processes or directories
  remained and removed the temporary UFW rules for ports 18443/18444.

### M7 G4 fixed-loss parity closeout (2026-09-02)

The final G4 run used `endpoint-1.example.invalid` as the client and `endpoint-3.example.invalid` as
the server. It used the frozen `loss` profile (seed `202`, 5% independent
loss in each direction, no added delay) on isolated UDP relay port `18444`.
The temporary Caddy binary was built from Caddy `3bcce47f`, forwardproxy
`c329155`, and quic-go `f84ad47630af`; `go version -m` resolved the exact
quic-go pseudo-version and `caddy list-modules` contained
`http.handlers.forward_proxy`. The client was the G1 Release binary from
NaiveProxy `71dc1dfb13`, configured with `quic-congestion=bbr1` or `cubic`.
Production ports, services, and configurations were not changed.

For each profile, seven 20 MiB downloads and seven 20 MiB uploads were run
through one SOCKS5 listener. CUBIC's 90-second samples are explicitly
right-censored when the transfer did not finish; BBR samples all completed:

```text
TCP download, single connection (curl speed_download)
  BBR:   2,521,306 .. 3,074,087 B/s, median 2,748,313 B/s (2.75 MB/s)
  CUBIC:    55,946 ..    64,144 B/s, median    59,649 B/s (0.060 MB/s)
  ratio: 46.1x; BBR median exceeds the 500 KB/s and 5x G4 thresholds

TCP download, eight parallel connections (batch wall time)
  BBR:   55.722, 56.262, 57.605, 61.732, 62.715, 63.834, 66.272 s
         median aggregate 2,591 KB/s (all 8/8 streams completed)
  CUBIC: 30-second observation batches; median aggregate 59.7 KB/s
         (all streams were short reads, no batch completed the 20 MiB object)

TCP upload, single connection (curl speed_upload)
  BBR:   938,693 .. 1,127,725 B/s, median 1,095,133 B/s; 7/7 HTTP 200
  CUBIC: 106,312 .. 174,034 B/s, median 115,052 B/s; 0/7 completed in 90 s

UDP application probe, 20 paced 1,200-byte echo datagrams per round
  BBR:   20, 18, 17, 19, 17, 18, 16; median 17/20 (85%)
  CUBIC: 20, 16, 16, 17, 18, 19, 16; median 17/20 (85%)
```

The existing Hy2 service was also run through a second isolated shaper
(`18445 -> 8444`) as a comparator: three 20 MiB downloads completed at
2.19–2.40 MB/s, three uploads at 1.21–1.25 MB/s, and seven UDP rounds had a
median 18/20 replies. This is supporting parity evidence, not a Naive gate.

The main shaper recorded `1,608,906` forwarded and `84,510` dropped packets
(5.00% drop); the Hy2 shaper recorded `136,203` forwarded and `7,143`
dropped (4.99%). No paced-probe queue overflow occurred. Point-in-time BBR
snapshots were approximately 6% server CPU and 2% client CPU; CUBIC's
right-censored throughput and the identical shaper policy show no performance
regression, but these CPU readings are observational rather than a dedicated
CPU benchmark.

All temporary clients, Caddy, HTTP/UDP targets, shapers, files, and UFW rules
were removed. Post-cleanup checks found only the production client on `1080`,
production Caddy on `8443`, and Hy2 on `8444`; all three remained active.
Marker: `M7_G4_PARITY_OK`.

### M7 corrected BBR deployment diagnostic (2026-09-02)

The earlier lossy comparisons below did not exercise server-side BBR. Binary
provenance inspection found that the temporary client contained G1 and accepted
`quic-congestion=bbr1`, but the Caddy binary actually serving the comparison
contained `forward_proxy` with upstream `quic-go v0.59.0`. A separate Caddy
binary contained the M7 quic-go fork but did not contain the `forward_proxy`
module. Consequently, the alleged Naive BBR path still used server CUBIC.

The diagnosis was repeated with one combined binary built from exact local
worktrees: Caddy `3bcce47f`, forwardproxy `c329155`, and quic-go `f84ad47`.
`go version -m` and `caddy list-modules` verified all three replacements and
`http.handlers.forward_proxy` before deployment. On the authorized
`endpoint-1.example.invalid` client to `endpoint-3.example.invalid` server path:

```text
5% bidirectional loss, seed 202, 20 MiB download
Naive BBR:   20 MiB in 8.38 s, approximately 2.50 MB/s
Naive CUBIC: 1.95 MiB in 35 s, approximately 55 KB/s (timed out)
Hy2 BBR:     20 MiB in 8.29 s, approximately 2.53 MB/s
```

Server qlog confirmed the mechanism. Correct BBR grew its congestion window
from 40 KiB to 1.22 MiB under 5% loss and ended near 1.17 MiB. CUBIC reached
only 79 KiB and ended near 10 KiB. A clean-path 20 MiB BBR run completed in
about 2.6 seconds and grew the server window to 6.4 MiB. By contrast, qlog from
the wrongly deployed server reduced 40,960 bytes to 28,672 bytes on its first
loss (the CUBIC 0.7 factor) and never exceeded 40 KiB.

This isolates the prior throughput gap to the server deployment artifact, not
the Naive HTTP/3 CONNECT or forwardproxy data path: with the correct BBR
binary, Naive and Hy2 were equal within about 1.1% in this diagnostic. This was
one download sample, not the complete G4 TCP upload/download, UDP, and repeated
median qualification matrix. All isolated listeners, clients, qlogs, shapers,
temporary files, and UFW rules were removed; production services were not
changed.

### M7 G4 fixed-loss comparison (2026-09-02, superseded)

Using `endpoint-1.example.invalid` as the client, `endpoint-3.example.invalid` as the server, and a
userspace UDP shaper with fixed seed `202`, bidirectional 5% loss was applied
to isolated relay ports. Three 10 MiB authenticated downloads were attempted
through each outer-QUIC profile:

```text
BBR   32.5 KB/s, 34.4 KB/s, 54.6 KB/s   median 34.4 KB/s
CUBIC 31.1 KB/s, 31.9 KB/s, 48.9 KB/s   median 31.9 KB/s
```

This result is invalid as a BBR comparison because its server binary used
upstream quic-go CUBIC, as established by the corrected deployment diagnostic
above. The observed 7.8% difference must not be used as M7 performance
evidence.

The UDP application probe did not establish a usable echo association under
this loss profile, so no UDP parity claim is made. The isolated Caddy/client,
shaper processes, directories, and temporary firewall rules were removed after
the run. G4 remains **not qualified** and G5/final audit must not be marked
complete.

### M7 UDP/Hy2 retry (2026-09-02)

The prior failed UDP result was a test-topology/handshake failure: no
CONNECT-UDP association had been established. With Naive BBR and CUBIC pointed
directly at the production Caddy H3 listener (`endpoint-3.example.invalid:8443`), paced
1,200-byte UDP echo probes returned 20/20 for both. The existing Hysteria
service on `:8444` also returned 20/20 through a temporary client on
`endpoint-1.example.invalid`.

Three Hy2 downloads of the 10 MiB test file completed at 3.21–3.62 MB/s. In
the same clean-path retry, one Naive BBR download completed at 2.88 MB/s and
one Naive CUBIC download at 3.19 MB/s. These results are similar in scale and
do not qualify the lossy G4 gate. All temporary clients, echo service,
directories, and firewall rules were removed; production listeners were not
changed.

### M7 5% loss retry with Hy2 (2026-09-02; superseded)

For an additional stress comparison, a fixed-seed userspace shaper applied
5% bidirectional random loss to isolated Naive BBR, Naive CUBIC, and the
existing Hy2 service. The client remained `endpoint-1.example.invalid` and the server
remained `endpoint-3.example.invalid`.

- TCP 10 MiB downloads: Naive BBR samples were approximately 47–58 KB/s;
  Naive CUBIC samples approximately 48–55 KB/s. Hy2 completed at 1.61 and
  1.68 MB/s in two samples.
- UDP echo: Naive BBR and CUBIC each received 0/5 paced probes; Hy2 received
  3/5. This is a loss survivability result, not a throughput claim.

The Naive BBR throughput comparison in this run is invalid because server-side
BBR was not active. The UDP association observations remain topology evidence,
but do not compare BBR implementations. The earlier Naive UDP 0/20 result was
therefore not a protocol-path failure: at 5% loss the Naive CONNECT-UDP
control/association exchange can still
complete, while the already-established Hy2 client retained a usable UDP
forwarding session. UDP payloads themselves are not retransmitted by either
proxy; Hy2's advantage here is its established-session/loss handling rather
than a generic property of UDP. All temporary processes, directories, and
firewall rules were removed after this run.

### M7 true 50% loss retry (2026-09-02; BBR comparison superseded)

The comparison was repeated with explicit `--loss-percent 50` rather than the
5% named profile above. Shaper logs confirmed near 1:1 drop/forward counts on
all three paths. TCP 10 MiB attempts produced:

```text
Naive BBR:   48,898 bytes in 30 s, 1.6 KB/s (timed out)
Naive CUBIC: connection reset after 5.4 s, 0 bytes
Hy2:         1,457,922 bytes in 30 s, 48.6 KB/s (timed out)
```

The Naive row labeled BBR did not have server-side BBR active and is invalid as
a congestion-control comparison. The paced UDP probe (five 1,200-byte packets,
500 ms apart) received `0/5` for Naive BBR, `0/5` for Naive CUBIC, and `0/5`
for Hy2. At true 50% loss,
none of the protocols established a usable UDP association; this is a
survivability result, not a throughput comparison. All test processes,
directories, and temporary UFW rules were removed.

## M1 detailed status

### Foundation completed before G1

- Reused Chromium's RFC 9298 URL construction next to
  `QuicProxyDatagramClientSocket`; target hosts are escaped correctly.
- Added `NaiveQuicProxyStreamRequest` around
  `QuicSessionRequest -> session handle -> RequestStream()`.
- Added dormant `NaiveConnectUdpTunnel`; it acquires a QUIC request stream and
  passes it to `QuicProxyDatagramClientSocket::ConnectViaStream()`.
- Retained the QUIC session handle for the full datagram socket lifetime.
- Added cached preemptive proxy authentication through `HttpAuthController`.
  The first CONNECT-UDP can use credentials already loaded into
  `HttpAuthCache`. Interactive 407 restart is explicitly deferred because it
  requires a fresh QUIC stream and an upper-layer retry loop.
- Kept SOCKS ingress and the existing TCP `NaiveConnection` path untouched.
- Repeated Release builds and the complete existing TCP HTTP/HTTPS/auth/chain
  regression suite successfully after the changes.

### G1 — controlled HTTP/3 CONNECT-UDP endpoint: complete

Implemented and verified:

- `naive_masque_server`: controlled server using the exact QUICHE revision
  vendored by Chromium. It logs redacted Extended CONNECT metadata.
- `naive_masque_probe`: independent QUICHE client that calls
  `MasqueClientSession::SendPacket()` directly; it does not use Naive's tunnel.
- `masque_udp_echo.py`: local UDP echo fixture.
- `masque_g1_smoke.sh`: repeatable build/certificate/start/probe/cleanup test.
- A Chromium-local `quiche_tool_support` GN target exposing only CLI/test
  support; the missing `quic_trace` protobuf is deliberately excluded.

Verified evidence:

```text
READY masque=h3-connect-udp bind=[::]:19661 authority=[::1]:19661
CONNECTED proxy=https://[::1]:19661/... target=127.0.0.1:19001
DATAGRAM_ECHO_OK from=127.0.0.1:19001 bytes=19 payload=g1-connect-udp-echo
CONNECT_HEADERS method=CONNECT protocol=connect-udp scheme=https
  authority=[::1]:19661
  path=/.well-known/masque/udp/127.0.0.1/19001/
RX bytes=19 hex=67312d636f6e6e6563742d7564702d6563686f
G1_MASQUE_SMOKE_OK
```

The long-running developer endpoint was also started on UDP `[::]:9661`, with
the echo target on `127.0.0.1:19000`. Its certificate is temporary test data
under `/tmp`; the repeatable smoke script generates its own certificate and
does not depend on that process remaining alive.

G1 does **not** prove Naive authentication or `NaiveConnectUdpTunnel` runtime
behavior. The independent probe intentionally logged
`proxy_authorization=absent`. Those are G2/G3 gates.

### G2 — real Naive tunnel runner: complete

Goal:

- Add a test-only executable that creates the same real `URLRequestContext` /
  `HttpNetworkSession` dependencies used by Naive.
- Configure a `quic://` proxy pointing at the controlled G1 endpoint.
- Invoke `NaiveConnectUdpTunnel` directly, without SOCKS5 ingress.
- Write one datagram and read its echo through the returned
  `DatagramClientSocket`.
- Emit deterministic state/result output suitable for automation.

Completed G2 substeps:

1. **G2-A:** isolated the smallest real session/context initialization boundary.
2. **G2-B:** compiled a runner through `NaiveConnectUdpTunnel::Start()`.
3. **G2-C:** added asynchronous write/read pumps and timeout handling.
4. **G2-D:** passed the controlled endpoint echo; regression verification is
   recorded below.

Verified evidence:

```text
CONNECT_UDP_URL_CONSTRUCTION_OK
SESSION_READY proxy=[quic://[::1]:19662]
CONNECT_UDP_OK
DATAGRAM_WRITE_OK bytes=20
DATAGRAM_ECHO_OK bytes=20 payload=g2-naive-tunnel-echo
CONNECT_HEADERS method=CONNECT protocol=connect-udp scheme=https
  authority=[::1]:19662
  path=/.well-known/masque/udp/%3A%3A1/19002/
  capsule_protocol=?1 proxy_authorization=absent
G2_NAIVE_TUNNEL_OK
```

The first real run failed with `QUIC_TLS_CERTIFICATE_UNKNOWN`. That exposed a
useful Chromium constraint: `QuicSessionPool` copies `QuicParams` during
`URLRequestContext::Build()`. The test-only self-signed origin must therefore
be installed before `Build()`, not mutated afterward. The runner now follows
that ordering without weakening production certificate verification.

G2 now runs both the QUIC proxy and UDP target on IPv6 loopback. It verifies
the bracketed IPv6 proxy authority and RFC 9298 target variable encoding
(`::1` becomes `%3A%3A1`, without authority-style brackets).

### G3 — cached Basic pre-authentication: complete

The controlled endpoint can require Basic proxy authentication without
logging credentials. A repeatable test verifies three independent client
processes:

- no credentials: rejected, `ERR_TUNNEL_CONNECTION_FAILED`;
- wrong cached credentials: header present but rejected with the same error;
- correct cached credentials: first CONNECT-UDP contains
  `Proxy-Authorization` and datagram echo succeeds.

```text
AUTH_DECISION rejected
AUTH_DECISION rejected
AUTH_DECISION accepted
DATAGRAM_ECHO_OK bytes=21 payload=g3-authenticated-echo
G3_BASIC_AUTH_OK
```

This proves cached/preemptive Basic authentication. It does not claim that a
single tunnel object can recover from a 407 challenge: the current object
returns the rejection and an upper layer would need a fresh request stream.

### G4 — authenticated bidirectional tunnel evidence: complete

G3's accepted case supplies G4's required end-to-end evidence through the
real Naive tunnel: authenticated Extended CONNECT headers, the RFC 9298 target
path, one successful write callback, an identical read callback, and the UDP
echo fixture's peer/byte log. Production SOCKS ingress remains dormant.

### G5 — lifecycle and failure behavior: complete

Completed:

- 407 rejection with absent and wrong credentials returns a deterministic
  error without a datagram write.
- Destroying `NaiveConnectUdpTunnel` while its connected socket has a pending
  `Read()` cancels the callback safely. A 200 ms grace window completes with
  `PENDING_READ_DESTRUCTION_OK`; no callback, UAF or crash occurs.
- Destroying the tunnel while the server deliberately leaves CONNECT pending
  produces `CONNECT_PENDING_DESTRUCTION_OK`; the server proves the request
  reached `CONNECT_ACTION ignored` before the test exits.
- Closing all Chromium QUIC sessions with a pending datagram read, then
  destroying the tunnel, produces `SESSION_SHUTDOWN_DESTRUCTION_OK` without a
  callback after destruction.
- The lifecycle test writes a real Chromium NetLog and asserts both the
  `QUIC_PROXY_DATAGRAM_CLIENT_SOCKET` source and `connect-udp` request evidence.

```text
PENDING_READ_DESTRUCTION_OK
NET_LOG_WRITTEN path=.../pending-read-netlog.json
SESSION_SHUTDOWN_ISSUED
SESSION_SHUTDOWN_DESTRUCTION_OK
CONNECT_PENDING
CONNECT_PENDING_DESTRUCTION_OK
CONNECT_ACTION ignored
G5_LIFECYCLE_OK
```

### Final M1 Chromium API boundary

- `NaiveQuicProxyStreamRequest` is the only Naive-owned adapter into
  `QuicSessionRequest`. It requests `SessionUsage::kProxy`, retains the QUIC
  session handle, requests one HTTP/3 stream, and exposes only the stream plus
  local/peer address and user-agent metadata.
- `NaiveConnectUdpTunnel` composes that stream with
  `QuicProxyDatagramClientSocket::ConnectViaStream()`. Declaration order makes
  the datagram socket die before the retained session handle.
- The CONNECT-UDP socket builds the RFC 9298 default template, optionally uses
  Naive's existing cached `HttpAuthController`, and continues to support the
  generic Chromium caller with no auth controller.
- A 407 is an explicit tunnel failure. Retrying requires a new stream and is
  deferred to an upper-layer association/retry owner; M1 does not imply
  interactive challenge recovery.
- Socket disconnection follows Chromium's `Socket` contract: pending callbacks
  may be cancelled rather than invoked. The M3 association owner destroys its
  tunnel when the session/control association closes.
- No SOCKS command parsing, `NaiveProxy::DoConnect()` branching, UDP relay,
  production server, or TCP data path was added in M1.

## M2 execution ledger — SOCKS5 UDP ingress

At M2 completion, the milestone was intentionally limited to a local SOCKS5
UDP ingress path with an injected fake `DatagramBackend`; production
CONNECT-UDP integration remained deferred. M3 later completed that production
composition. The existing TCP data mover is unchanged.

### M2 architecture

```text
NaiveProxy::DoConnect()
        |
        +-- HTTP / redir
        |      └── existing NaiveConnection path
        |
        └-- SOCKS5 two-stage handshake
                 |
                 +-- CONNECT
                 |      └── success response → existing NaiveConnection
                 |
                 └-- UDP ASSOCIATE
                        ├── non-quic:// → reply 0x01 and close
                        └── quic://
                               ├── bind local UDP relay
                               ├── return real BND.ADDR/BND.PORT
                               └── Socks5UdpAssociation
                                      └── M2 fake DatagramBackend
```

`NaiveProxy::DoConnect()` installs an independently owned pending SOCKS
handshake and immediately resumes accepting. Its request-completion callback
branches on the parsed command before any `NaiveConnection` is constructed.

### M2-G0 — interface freeze and test skeleton

Status: complete.

Completed:

- Added standalone SOCKS5 UDP test target:
  - `naive_socks5_udp_test`
- Added minimal test executable:
  - `tools/naive/naive_socks5_udp_test_bin.cc`
- Verified independent build path without touching NaiveProxy runtime.

Verified marker:

```text
M2_SOCKS5_UDP_TEST_SKELETON_OK
```

- Added the standalone codec target, deterministic SOCKS state-machine target,
  real-loopback integration runner, and independent Python RFC 1928 oracle.
- Defined `Socks5UdpAssociation` and its fake-backend boundary as M2 scope.
- Kept the real M1 `NaiveConnectUdpTunnel` out of the M2 ingress path.
- Avoided a dependency on the unavailable full Chromium `net_unittests` graph.

### M2-G1 — RFC 1928 UDP codec

Status: complete.

Implemented:

- Isolated span-based RFC 1928 codec and structured error model.
- Exact IPv4, IPv6, and domain parsing/serialization.
- Binary and empty payloads, port `0`/`65535`, and 255-byte domains.
- Table-driven fixed-wire, round-trip, boundary, malformed, truncation, RSV,
  FRAG, invalid address and invalid-build cases.
- Dedicated fragment error so the association can account for drops without
  logging destinations or payloads.

Verified marker:

```text
M2_G1_CODEC_OK
```

### M2-G2 — SOCKS5 two-stage handshake

Status: complete.

- Split request parsing from reply writing through `ReadRequest()` and
  `WriteReply()` while retaining legacy one-shot `Connect()` behavior.
- Exposed typed command/reply values and the parsed request endpoint.
- Serialize the caller-provided IPv4 or IPv6 bound endpoint.
- A deterministic scripted `StreamSocket` test covers all-sync, all-async,
  byte-fragmented command reads, partial reply writes, mixed phase modes, and
  cancellation during pending read/write.

Verified marker:

```text
M2_G2_DETERMINISTIC_STATE_MACHINE_OK
```

### M2-G3 — NaiveProxy command branching

Status: complete.

- Allocate the connection ID on entry to `DoConnect()`, before starting any
  independent SOCKS asynchronous operation.
- Keep pending handshakes in an ID-keyed owning map while the main accept loop
  continues.
- CONNECT writes the byte-identical legacy success response and transfers the
  same handshaken socket to the existing `NaiveConnection` path.
- BIND and unknown commands return `0x07`.
- UDP on a non-QUIC chain, or without an installed backend, writes `0x01` and
  closes; unsupported UDP is neither acknowledged with success nor left for
  silent data-path drops.

### M2-G4 — real UDP relay and BND endpoint

Status: complete.

- Freeze the TCP peer before sending success, then bind a UDP relay to the
  concrete local control address and address family with an ephemeral port.
- Return that socket's actual `BND.ADDR/BND.PORT`; bind or address lookup
  failure retains the default `0x01` response and closes.
- Real IPv4 and IPv6 clients send datagrams to the independently decoded reply
  endpoint and receive responses.

### M2-G5 — Socks5UdpAssociation and fake backend

Status: complete.

- Own the handshaken TCP control connection, bound UDP relay and injected
  backend as one association.
- Match the normalized TCP peer IP, enforce a requested source port, or learn
  a wildcard port only after the first valid RFC 1928 packet.
- Drop malformed, fragmented, wrong-port and wrong-IP sources without pinning
  or terminating a healthy association. Fragment warnings are rate-limited at
  powers of two.
- Keep one backend send in flight, serialize relay writes, and cap the response
  queue at 64 datagrams with observable drops.
- Post initial pumps, bound synchronous read loops to 32 operations before
  yielding, and guard synchronous backend callback reentrancy.
- Closing the TCP control channel terminates the relay; idle cleanup shares the
  existing proxy cleanup timer.
- The M2 runner injects a synchronous echo backend only in tests. The
  production binary has no fake backend and therefore cannot expose fake UDP.

### M2-G6 — cleanup and audit

Status: complete and independently audited.

Verification matrix already passing:

- IPv4, IPv6, and domain targets.
- Non-QUIC `0x01` response.
- Correct BND address behavior.
- `FRAG != 0`.
- Invalid and truncated datagrams.
- Spoofed UDP sources.
- TCP control close cleanup.
- Multiple concurrent associations.
- All 56 TCP regression tests.
- Deterministic pending-I/O cancellation and malformed-datagram lifecycle
  stress verification.

Independent audit result:

- One continuing `agy` session inspected the complete implementation and
  independently reran the M1/M2/TCP verification matrix.
- It found no blocker, high, or medium issue and returned `AUDIT_PASS`.
- Full evidence is recorded in `docs/m2-agy-audit.md`.

Verified integration markers:

```text
M2_G2_HANDSHAKE_OK
M2_G2_AUTHENTICATED_UDP_OK
M2_G3_BRANCHING_OK
M2_G4_RELAY_OK
M2_G4_G5_UDP_ASSOCIATION_OK
M2_G5_WRONG_SOURCE_IP_OK
M2_G5_SOURCE_AUTH_OK
M2_G5_ASSOCIATION_OK
M2_G5_CONCURRENCY_OK
M2_G5_LIFECYCLE_OK
M2_G3_NON_QUIC_REJECTION_OK
M2_G3_NO_BACKEND_REJECTION_OK
M2_SOCKS5_UDP_INGRESS_OK
```

## M3 execution ledger — native UDP client data path

Status: complete and independently audited. The real backend is installed in
production `naive`; M2 fake/no-backend behavior remains an independent
regression surface.

The plan was derived from direct inspection of the M1 tunnel and M2 ingress
boundaries, then checked by three independent read-only reviews. The reviews
identified these blockers, all of which M3 subsequently resolved:

- the zero-argument backend factory must receive the exact transient NAK from
  `PendingSocksHandshake`;
- one target needs one generation-safe tunnel owner and continuous read/write
  pumps;
- ordinary target, oversize, and queue failures must not close the whole SOCKS
  association;
- payload ceiling, zero-length datagram versus EOF, callback-stack retirement,
  and URL request context destruction order need explicit tests;
- target, packet, byte, active-association, connect, idle, and cooldown bounds
  must be frozen before full-path load testing;
- the full M3 path used `naive_masque_server` as a controlled compliant
  endpoint; production Caddy/`forwardproxy` was completed separately in M4.

Sequential gates:

```text
G0  backend context/NAK, contracts, constants, scripted tunnel seam
G1  cancellation-safe single-target backend
G2  target routing, bounds, cooldown, and failure isolation
G3  real M1 adapter and production composition
G4  controlled IPv4/IPv6/domain/DNS/auth/multi-target interoperability
G5  lifecycle, recovery, resource pressure, and observability
G6  full regression plus independent agy AUDIT_PASS
```

Final aggregate marker: `M3_NATIVE_UDP_CLIENT_OK`.

Planning estimate: 12–20 person-days for an audited M3, approximately 2–4
elapsed weeks for one engineer plus an agent. The first single-target real path
is not considered milestone completion.

### M3-G0 — backend contract, context, and test seam

Status: complete.

Completed:

- Replaced the zero-argument backend factory with an immutable per-association
  context carrying the association id, non-owning session pointer, exact
  transient NAK, selected proxy chain, NetLog source, traffic annotation,
  10-second connect timeout, and target idle timeout.
- Passed `PendingSocksHandshake::network_anonymization_key` directly into that
  context before the successful UDP ASSOCIATE reply. The M2 runner now rejects
  empty/non-transient keys and validates the remaining production-style
  context inputs.
- Froze target identity as SOCKS address type plus host plus port, so domains
  and numerically equivalent IP literals remain separate routes.
- Froze admission/no-replay comments and v1 limits: 32 targets, 16 queued
  datagrams per target, 128 queued datagrams and 256 KiB per association, 32
  synchronous pump operations, 10-second connect timeout, 1-second cooldown,
  and 256 active UDP associations per NaiveProxy.
- Added the narrow `NaiveConnectUdpTargetTunnel` seam for scripted and future
  production M1 adapters, including explicit open-state, zero-length-read, and
  safe-payload-limit queries.
- Added the standalone `naive_connect_udp_backend_test` and the cumulative
  `tests/socks5_udp_m3.sh` entry point.

Verified markers:

```text
M3_G0_BACKEND_CONTRACT_OK
M3_G0_TEST_SKELETON_OK
```

### M3-G1 — cancellation-safe single-target backend

Status: complete.

Completed:

- Lazily creates one fixed-target tunnel on the first admitted datagram and
  queues payloads while CONNECT-UDP is pending.
- Serializes writes, retains every `IOBuffer` through pending callbacks, checks
  exact byte counts, and clears ambiguous queued data without replay after a
  short write or transport failure.
- Keeps one read armed on an open tunnel, immediately rearms after delivery,
  and yields after 32 synchronous completions.
- Uses the live tunnel payload ceiling before every write; oversize payloads
  are observable policy drops rather than association-fatal errors.
- Distinguishes an empty UDP datagram from EOF through the scripted tunnel
  contract, preserves the original SOCKS endpoint on responses, and supports
  callback-triggered backend destruction without rearming or UAF.
- Defers target destruction out of connect/read/write callback stacks and
  orders tunnel destruction before retained pending-I/O buffers.

Deterministic coverage includes synchronous and asynchronous connect/read/
write, same-target reuse, byte equality, pending destruction, short write,
EOF, synchronous and asynchronous zero-length datagrams, live oversize drop,
and receive-callback destruction.

Verified marker:

```text
M3_G1_SINGLE_TARGET_OK
```

### M3-G2 — target routing, limits, and failure isolation

Status: complete.

Completed:

- Routes by address type plus host plus port with one generation-tagged tunnel
  owner per target. Interleaved responses retain their original endpoint and
  cannot cross routes.
- Enforces 32 live/cooldown targets, 16 queued datagrams per target, 128 queued
  datagrams and 256 KiB per association. A busy target is never evicted to
  admit a new one.
- Enforces a default 256 active UDP-association cap per `NaiveProxy`, counting
  both established associations and successful-reply-pending reservations.
  The listener's existing `concurrency` value governs session prewarming, not
  connection count, so a separate conservative hard cap is required. A
  deterministic cap/release test uses an injected lower limit and verifies
  exact SOCKS reply `0x01` at capacity.
- Adds connect deadlines, independent target idle eviction, and cooldown
  tombstones. Cooldown blocks per-packet reconnect storms; a later new packet
  after expiry creates a fresh tunnel.
- Converts connect/read/write/session failure into target-scoped retirement.
  Other targets and the SOCKS association remain active. Queued data from an
  ambiguous failure is cleared and never replayed into the fresh tunnel.
- Resets live idle deadlines on admitted open-target traffic and successful
  reads/writes, caps synchronous read work at 32 operations, and resumes via a
  posted task.

Deterministic tests cover IPv4/domain-distinct routing primitives, interleaved
targets, same-target reuse, target/packet/byte/association caps, connect
timeout, idle eviction, cooldown suppression and expiry, fresh-tunnel creation,
no replay, failure isolation, and synchronous pump yield.

Verified markers:

```text
M3_G2_MULTI_TARGET_LIMITS_OK
M3_G2_FAILURE_ISOLATION_OK
M3_G2_ACTIVE_ASSOCIATION_LIMIT_OK
```

### M3-G3 — real M1 adapter and production composition

Status: complete.

Completed:

- Added a production target adapter that owns `NaiveConnectUdpTunnel` and
  forwards the backend context's session, complete proxy chain, exact NAK,
  NetLog source, traffic annotation, and fixed target without substituting a
  second QUIC implementation.
- Added defensive production-factory checks for a live session, non-empty
  transient NAK, positive timeouts, a valid non-direct chain, and every proxy
  hop being `quic://`. The SOCKS handshake keeps its independent eligibility
  check.
- Installed the real factory in `naive_proxy_bin.cc`. The production binary
  still uses the default certificate verifier; only the separate controlled
  runner installs `MockCertVerifier` before building its context.
- Added live Chromium queries for stream state, empty-datagram evidence, and
  safe payload ceiling. The ceiling uses QUICHE's HTTP/3 datagram size after
  quarter-stream-id overhead, then removes the RFC 9298 Context ID byte.
- Treats a closed live stream or zero live ceiling as a target failure rather
  than misclassifying it as an oversize policy drop.
- Corrected declaration order so every proxy/backend/tunnel is destroyed
  before its URL request context/session and resolver.
- Added `naive_socks5_udp_m3_runner`, which shares the exact production
  factory and proves graceful proxy-before-context destruction.
- Verified exact SOCKS `0x01` plus EOF for direct, HTTPS/H2, valid mixed, and
  no-backend configurations. Verified real IPv4 echo and cached Basic auth via
  SOCKS5, the M3 backend, M1 tunnel, RFC 9298 CONNECT-UDP, and H3 DATAGRAM.

Verified markers:

```text
M3_G3_DIRECT_REJECTION_OK
M3_G3_H2_REJECTION_OK
M3_G3_MIXED_CHAIN_REJECTION_OK
M3_G3_NO_BACKEND_REJECTION_OK
M3_G3_IPV4_ECHO_OK
M3_G3_AUTH_ECHO_OK
M3_G3_PRODUCTION_WIRING_OK
```

The G3 gate also reran all M1 scripts, the complete M2 suite, the cumulative
M3 entry point, all 56 existing TCP cases, and `git diff --check`.

### M3-G4 — controlled full-path interoperability

Status: complete.

Completed:

- Ran the exact production backend/factory through a real SOCKS5 UDP relay,
  Chromium CONNECT-UDP/H3 DATAGRAM, the controlled QUICHE endpoint, and local
  deterministic UDP fixtures.
- Verified IPv4 and IPv6 literals, a domain target without local resolution,
  a deterministic DNS query/response, multiple targets in one association,
  and four concurrent associations.
- Verified the cached Basic credential path separately from the transient NAK
  contract and required the controlled server's accepted-auth evidence.
- Replaced fixed fixture ports with dynamically reserved loopback ports and
  widened runner deadlines to remove slow-host and parallel-test flakiness.
- Closed the post-G3 privacy audit finding: QPDCS now records write byte counts
  without payload buffers, CONNECT-UDP request lines are redacted, and the
  common HTTP/3 header logger redacts the RFC 9298 target path even at NetLog
  `kEverything` capture level.
- Closed the corresponding transport-lifecycle code gap: underlying QUIC
  stream closure now completes a pending QPDCS datagram read and permits the
  M3 owner to retire that target promptly. G5 adds explicit reconnect evidence.

Verified markers:

```text
M3_G4_IPV4_OK
M3_G4_IPV6_OK
M3_G4_DOMAIN_OK
M3_G4_DNS_OK
M3_G4_AUTH_OK
M3_G4_MULTI_TARGET_OK
M3_G4_CONCURRENT_ASSOCIATIONS_OK
M3_G4_NETLOG_REDACTION_OK
```

### M3-G5 — lifecycle, recovery, limits, and observability

Status: complete.

Completed:

- Added a real full-path session-shutdown case. Chromium closes the active
  QUIC session while QPDCS has a pending read; the close callback retires only
  that target, the one-second cooldown expires, and a later packet creates a
  second CONNECT-UDP tunnel. The UDP fixture receives the pre- and post-close
  payload exactly once each.
- Added a real target-idle case using a test-runner-only timeout override. The
  first target is evicted independently of the SOCKS association and a later
  packet creates a fresh tunnel.
- Verified absent and wrong cached Basic credentials each produce two explicit
  server-side 407 rejections while the SOCKS association remains open. The
  accepted cached credential case remains the independent G4 positive path.
- Verified control close with CONNECT pending, active backend destruction with
  a pending target read, and deterministic backend destruction with pending
  connect/read/write callbacks. Production QPDCS writes are currently
  synchronous, so the pending target-write branch is exercised through the
  narrow scripted tunnel contract rather than claimed as a real network state.
- Verified a 200 ms controlled connect timeout, cooldown, retry on a later
  packet, idle eviction, zero-length datagram echo, and four oversized drops
  followed by a healthy small datagram on the same target.
- Revalidated the 32-target, 16-packet-per-target, 128-packet, 256 KiB,
  256-active-association, 32-operation pump, and 64-response-queue limits. The
  response-pressure test proves exactly 64 queued responses are sent and two
  excess responses are dropped without terminating the association.
- Added `NAIVE_CONNECT_UDP_BACKEND_COUNTER`. It logs only association id,
  non-sensitive reason, and cumulative count at powers of two. A real
  `kEverything` NetLog proves oversize events occur at counts 1, 2, and 4 and
  contain neither UDP destination nor payload.
- Kept all timeout shortening and self-signed certificate handling inside the
  controlled test runner; production defaults and verification are unchanged.

Verified markers:

```text
M3_G5_DETERMINISTIC_LIFECYCLE_OK
M3_G5_SESSION_RECONNECT_OK
M3_G5_IDLE_RECONNECT_OK
M3_G5_ZERO_OVERSIZE_OK
M3_G5_BACKEND_DESTRUCTION_OK
M3_G5_PENDING_CONNECT_CLOSE_OK
M3_G5_CONNECT_TIMEOUT_OK
M3_G5_AUTH_MISSING_OK
M3_G5_AUTH_WRONG_OK
M3_G5_AUTH_FAILURES_OK
M3_G5_RECONNECT_OK
M3_G5_LIFECYCLE_OK
M3_G5_LIMITS_OK
```

### M3-G6 — complete regression, stress, and independent audit

Status: complete.

Verified:

- Rebuilt the complete named Release target set and reran all M1 scripts, the
  full M2 entry point, the full M3 entry point, all 56 TCP HTTP/HTTPS/auth/chain
  cases, and `git diff --check`.
- Repeated `tests/socks5_udp_m3.sh` three consecutive times against fresh
  controlled endpoints. Every run produced `M3_G5_RECONNECT_OK`,
  `M3_G5_LIFECYCLE_OK`, `M3_G5_LIMITS_OK`, and
  `M3_G0_TEST_SKELETON_OK` with exit code zero.
- Inspected `c6ec957f..578e3992`: no `NaiveConnection` TCP data-path change,
  second QUIC stack, UoT/private framing, Caddy/M4 implementation, production
  certificate bypass, generated artifact, or accidental tracked test output
  entered M3.
- Started a fresh Gemini 3.1 Pro High `agy -p` review with permissions prompts
  disabled and a 30-minute window. The reviewer read the actual diff rather
  than relying on this ledger, independently reran the complete required
  matrix, and returned `AUDIT_PASS` with zero blocker, high, or medium
  findings.
- The sole low observation was that the frozen 32-target v1 cap may be small
  for aggressive multi-target clients; the reviewer confirmed bounded graceful
  drops make it acceptable for v1.

Final marker: `M3_NATIVE_UDP_CLIENT_OK`.

Durable report: [`m3-agy-audit.md`](m3-agy-audit.md).

## M4 production server path

Status: complete and independently audited. Detailed gates and contracts are in
[`m4-execution-plan.md`](m4-execution-plan.md).

Read-only source inspection recorded these exact reference snapshots:

- Naive `forwardproxy` branch `naive`, commit
  `d62c80d3dd2c706b6b87579844d2397bddd18317`;
- Caddy `v2.11.2`, commit
  `ffb6ab0644f24c5ee6542aca6bd59b7a1b0a8f91`;
- quic-go `v0.59.0`, commit
  `7659dd8e0fa06b41290ad29af323d93d673c6b36`.

The historical source facts that defined the runtime M4 gates were:

- baseline `forwardproxy` rejected H2/H3 CONNECT whenever `:scheme` or `:path`
  was present, so RFC 9298 needed an explicit Extended CONNECT branch before
  legacy TCP CONNECT;
- authentication, probe resistance, and normal-site routing already executed
  before CONNECT dispatch and must remain common to TCP and CONNECT-UDP;
- the baseline authorization/dial helper accepted only TCP, so target
  authorization/resolution had to be factored from transport dialing rather
  than bypassed for UDP;
- the declared `forwardproxy` dependencies were Caddy v2.8.4/quic-go v0.44.0,
  but its release workflow used floating `xcaddy@latest`, while the inspected
  Caddy v2.11.2 used quic-go v0.59.0 and Go 1.25; G0 had to freeze one exact
  reproducible tuple;
- Caddy v2.11.2 constructed `http3.Server` without `EnableDatagrams: true`;
- quic-go v0.59.0 exposed `http3.HTTPStreamer` and stream-level
  `SendDatagram`/`ReceiveDatagram`, but real Caddy middleware visibility and
  writer unwrapping still had to be runtime-proven in M4-G1;
- quic-go owns HTTP/3 quarter-stream-id framing, while the server handler must
  decode/prepend RFC 9298 Context ID `0` around the UDP payload.

The completed sequence was G0 build/contract freeze, G1 Caddy capability spike, G2 strict
protocol/policy layer, G3 bounded UDP association, G4 production integration,
G5 independent server interoperability, and G6 reproducible closeout plus
independent `agy` audit. All gates are verified and the final milestone marker
is recorded below.

### M4-G0 — reproducible server baseline: complete

- Production server fork: `https://github.com/ssharkkky/forwardproxy`, branch
  `codex/native-udp-server`; local gate commit `bf092e6`.
- Caddy patch fork: `https://github.com/ssharkkky/caddy`, branch
  `codex/enable-h3-datagrams`, still at the clean v2.11.2 base for G0.
- Locked Go `1.25.12` archive SHA-256
  `fa2c88bbcf64bd3b2aef355f026cfec6d3a4a01c132f999c8f8c964eb767164f`,
  xcaddy `v0.4.5`, Caddy `v2.11.2`, quic-go `v0.59.0`, and the exact base
  commits recorded above.
- Replaced the floating `xcaddy@latest` workflow and old Caddy v2.8.4 module
  graph with the pinned tuple.
- Modernized the pre-existing test topology to distinct `*.localhost` names;
  this preserves Host/SNI separation while avoiding non-portable macOS
  `127.x.y.z` loopback aliases. Existing TCP, auth, ACL, upstream, PAC, and
  probe-resistance tests all pass on Caddy v2.11.2.
- Froze the v1 protocol/result/resource baseline and explicit no-private-queue,
  no-replay rule. The standalone script emits `M4_G0_SERVER_BASELINE_OK`.
- Two clean pinned builds were byte-identical with SHA-256
  `5b2d40b134e9b340e8fa9a9384c44d2b871bb43915fd485734be9003016b611d`.

G0 did not claim a Caddy H3 Datagram patch or production CONNECT-UDP handler.
M4-G1 subsequently proved that runtime boundary before protocol/relay work.

### M4-G1 — real Caddy H3 Datagram capability: complete

- Caddy commit `2002a520` enables `http3.Server.EnableDatagrams`; the first
  real handshake correctly failed with H3 SETTINGS error because Caddy's
  shared QUIC listener had already been created without the matching QUIC
  transport parameter.
- Caddy commit `2ff83e69` also enables Datagrams on that shared QUIC listener.
  This closes both required RFC 9297 negotiation layers rather than faking a
  server SETTINGS value.
- Forwardproxy commit `121f097` pins the patched Caddy pseudo-version/build
  replacement and adds a G1-only capability fixture plus the production-safe
  response-writer unwrapping seam.
- A real `quic-go` client traversed the complete Caddy route/middleware chain,
  observed `EnableExtendedConnect` and `EnableDatagrams`, proved incoming
  `r.Proto == connect-udp`, unwrapped through standard `Unwrap()` to
  `http3.HTTPStreamer`, and round-tripped both a binary payload and a valid
  zero-length H3 Datagram.
- Marker `M4_G1_CADDY_H3_DATAGRAM_OK`, the complete legacy forwardproxy suite,
  focused Caddy package tests, and the patched production Caddy build passed.
- Patched Caddy build SHA-256 at this gate:
  `9b8f5b62c80313264fb028e4b8f05fcb1a8c2434c60f1957089af0d0d6845269`.

G1 contains no target parser, ACL bypass, UDP socket, or production relay.
Those start only after G2 freezes strict protocol and policy behavior.

### M4-G2 — strict RFC 9298 protocol and policy: complete

- Forwardproxy commit `f9b40f6` adds an explicit H3 `connect-udp` branch
  before legacy TCP CONNECT and rejects other Extended CONNECT protocols with
  the frozen unsupported status.
- The strict default URI-template parser accepts IPv4, percent-encoded IPv6,
  ASCII/IDNA domains, and ports 1–65535. It rejects queries, fragments,
  missing/extra segments, encoded slash/backslash, double encoding, zones,
  userinfo, bracketed variables, bad labels, and invalid ports.
- The RFC 9298 application codec accepts only canonical QUIC-varint Context ID
  `0`, preserves a valid empty payload, and rejects truncated, noncanonical,
  or unsupported contexts.
- Target resolution/authorization is factored from TCP dialing. TCP and future
  UDP use the same domain rules, per-resolved-IP ACL evaluation, allowed-port
  list, deduplication, and context-aware DNS lookup; legacy TCP status/error
  mapping and the complete old suite remain green.
- UDP-facing policy errors are generic and do not contain the target. The
  frozen `400/403/501/502` protocol/policy mapping and unsupported upstream
  mode have deterministic tests.
- Marker `M4_G2_PROTOCOL_POLICY_OK` and the cumulative G0–G2 plus full legacy
  server suite pass.

G2 deliberately returned `501` after a valid authorized request reached the
association boundary. M4-G3 replaced that final stub with the bounded UDP
association; no packet was silently accepted before the data path existed.

### M4-G3 — bounded production UDP association: complete

- Forwardproxy commit `1b6d04b` replaces the valid-request `501` stub with one
  connected UDP socket per fixed-target CONNECT-UDP stream. Resolution and
  policy approval select a concrete IP before the HTTP `200`; the target is
  not re-resolved after authorization.
- The two pumps decode/prepend canonical Context ID `0`, preserve valid empty
  UDP payloads, retain one in-flight datagram per direction with no
  forwardproxy-owned packet queue, yield every 32 datagrams, and never retry
  an ambiguous write.
- A live quic-go `DatagramTooLargeError` is an observable drop rather than a
  stream reset. Other UDP/H3 failures close only that association.
- Per-handler 256 and per-client 32 active-association caps return the frozen
  pre-success `503`. Double-safe release, request/stream cancellation,
  two-minute production idle expiry, connected-socket closure, and pump join
  prevent resource leaks.
- Deterministic tests verify bidirectional byte equality, malformed-context
  drop, no replay, idle shutdown, cap/release behavior, and cancellation. Race
  coverage passed for association, admission, and real production-path tests.
- A real independent H3 client traversed the actual forwardproxy Handler to a
  local IPv4 UDP echo target and back for both non-empty and zero-length
  payloads. Marker: `M4_G3_UDP_ASSOCIATION_OK`.
- The complete legacy forwardproxy TCP/auth/ACL/upstream/PAC/probe-resistance
  suite remains green.

### M4-G4 — production auth/policy/privacy integration: complete

- Forwardproxy commit `15c07ab` preserves the legacy TCP branch and routes
  valid CONNECT-UDP only after shared authentication and probe-resistance
  processing. It also corrects the H3-specific authority interaction so a
  missing or wrong credential returns `407` when probe resistance is disabled,
  while enabled probe resistance still matches the ordinary hidden-site path.
- A real H3 matrix verifies IPv4, IPv6, domain resolution, missing/wrong/correct
  Basic credentials, malformed `400`, ACL/allowed-port `403`, unsupported
  upstream `501`, and probe-resistance passthrough against a reference Caddy
  route.
- CONNECT-UDP keeps its original URI available to the downstream camouflage
  route but redacts it before returning to outer Caddy logging. Handler counters
  log only association id, generic reason, count, and byte count at powers of
  two; tests search for target, path, payload, and credential leakage.
- `scripts/test-m4.sh`, the complete legacy forwardproxy suite, and
  `go test -race ./...` pass. The race build emitted only the previously noted
  harmless macOS `LC_DYSYMTAB` linker warning.
- Marker: `M4_G4_FORWARDPROXY_INTEGRATION_OK`.

### M4-G5 — pinned production-server interoperability: complete

- Forwardproxy commit `7243519` adds an independent quic-go RFC 9298 command
  that does not import NaiveProxy or forwardproxy test helpers, plus a real
  Caddy binary orchestration script and production-style Caddyfile.
- The locked build script now puts the verified Go 1.25.12 binary first in the
  `xcaddy` child PATH and inspects the final binary's embedded Go version. This
  closed a discovered gap where the earlier script verified 1.25.12 but could
  let `xcaddy` select a different system Go.
- Caddy commit `cce894a8` replaces its debug-level reflected raw module config
  with non-sensitive topology counts. The prior raw reflection exposed the
  recoverable double-Base64 form of Basic credentials; the final standalone
  log scan rejects both encoded forms as well as the target path/domain and
  payload sentinels.
- The standalone matrix verifies IPv4, IPv6, domain, deterministic DNS,
  zero-length, a 1024-byte safe payload, local oversize rejection followed by
  a healthy datagram, eight simultaneous streams, cancellation recovery, and
  negotiated Extended CONNECT plus H3 Datagram APIs.
- Thirty-two live associations are admitted from one client; the 33rd receives
  `503`, and released capacity is reusable. QUIC keepalive isolates and proves
  the unchanged two-minute production association idle expiry, after which a
  fresh stream on the same connection succeeds.
- A finite one-second Caddy grace period proves active H3 association
  cancellation during process shutdown, followed by restart smoke success.
  Three further complete matrix runs, the full legacy suite, race tests, and
  Caddy HTTP package tests remain green.
- Markers include `M4_G5_H3_DATAGRAM_EVIDENCE_OK`,
  `M4_G5_IDLE_EXPIRY_OK`, `M4_G5_RESOURCE_LIMIT_OK`,
  `M4_G5_SHUTDOWN_RESTART_OK`, `M4_G5_SERVER_LOG_PRIVACY_OK`, and final
  `M4_G5_SERVER_INTEROP_OK`.

### M4-G6 — release closeout and independent audit: complete

- Two separate locked builds are byte-identical. Both have SHA-256
  `d31fa3c8b0897b12ee799305a5aba10e23434fa0153398e44d82bb8ba4d82ba4`,
  and embedded build metadata reports Go 1.25.12.
- The standalone `M4_G5_SERVER_INTEROP_OK` matrix passed again using one of
  those exact G6 binaries.
- Uncached `go test -count=1 ./...` and `go test -race -count=1 ./...` pass in
  forwardproxy. The race link emits only the previously recorded macOS
  `LC_DYSYMTAB` warning. Uncached `go test -count=1 ./...` passes across the
  complete patched Caddy repository.
- The full M1 script set, M2, M3, aggregate `M3_NATIVE_UDP_CLIENT_OK`, and all
  56 TCP cases pass from the unchanged client checkout.
- Diff and artifact inventory confirms that M4 changed only documentation in
  this repository, 22 intended files in forwardproxy, and three intended Caddy
  files. No M4-generated binary, log, capture, certificate, credential, or
  destination-bearing output is tracked. Caddy's pre-existing tracked test
  key/certificate fixtures are outside the M4 diff.
- A separate user-run Antigravity (`agy`) session then inspected the committed
  NaiveProxy documentation range `fa7a1c2dfa..9ec8fff82c`, forwardproxy range
  `d62c80d..7243519`, and Caddy range `ffb6ab06..cce894a8`. It returned
  `AUDIT_PASS` with zero blocker, high, or medium findings.
- The audit's one low finding identified `.github/workflows/build.yml` still
  selecting Caddy `2ff83e69` instead of privacy-fixed `cce894a8`. The local
  build path and `go.mod` were already correct. Forwardproxy commit `8f044e2`
  closes the finding by updating that single CI ref; runtime source is
  unchanged.
- The durable audit record is [`m4-agy-audit.md`](m4-agy-audit.md).

Final marker: `M4_NATIVE_UDP_SERVER_OK`.

M4 is complete. M5 now owns the full SOCKS5-to-Naive-client-to-production-
Caddy product matrix, including product-level reconnect claims.

## M5 execution baseline — end-to-end MVP

Status: complete and independently audited. The
active plan is
[`m5-execution-plan.md`](m5-execution-plan.md).

M5 inherits these frozen inputs:

- NaiveProxy's audited M3 production data path, closeout `2bb83aec`;
- forwardproxy `8f044e2`, whose runtime implementation was audited at
  `7243519` and whose final commit closes the audit's CI-only pin finding;
- Caddy `cce894a8` and the locked M4 Go/xcaddy/quic-go build tuple.

The M5 sequence is:

```text
G0  dynamic topology, reversible trust fixture, independent H3 probe contract
G1  first M3-client-to-production-M4-server IPv4 echo
G2  IPv4/IPv6/domain/DNS/multi-target/concurrency/HTTP3 application matrix
G3  authentication, policy, malformed input, failure isolation, privacy
G4  control close, idle, server restart, QUIC reconnect, no replay
G5  shipped naive + default certificate verifier, wire evidence, no-padding baseline
G6  full M1-M5/server regressions, artifacts, independent AUDIT_PASS
```

Planning inspection found two tasks not fully represented in the earlier
5-8-day estimate: a safe trusted-certificate fixture for the shipped `naive`
binary and an independent SOCKS5-UDP-backed HTTP/3 application client. The M5
planning range is therefore 6–10 person-days.

### M5-G0 — topology, trust, harness, and evidence contract: complete

- Added `tests/socks5_udp_m5.sh` as the cumulative M5 entry point. It verifies
  the exact M3/M4/Caddy revisions, unchanged client runtime source, clean
  server worktrees, binary metadata, and isolated temporary Go caches.
- Added a dynamic loopback topology allocator and deterministic tests. The
  proxy uses one dynamically selected port available to both TCP and UDP;
  echo, DNS, and HTTP/3 fixtures use distinct dynamic UDP ports.
- Froze the macOS production trust strategy around Chromium's user-domain
  `TrustStoreMac`. The default contract check is read-only. A controlled
  `--exercise` run returned `before=1 during=0 after=1`, proving a temporary
  trust root is removed completely; the later G5 production run must use that
  path rather than a certificate-bypass flag.
- Added a dedicated Go 1.25/quic-go 0.59 module for the independent HTTP/3
  probe. G0 freezes the SOCKS5 UDP `net.PacketConn`, retained TCP control
  lifetime, and IPv4/IPv6/domain target identity; G2 adds runtime transport.
- Froze revisions, marker names, privacy sentinels, capture exclusions, and
  size/timing-only traffic fields in `tests/m5/contract.json`.
- `M5_G0_PRODUCT_CONTRACT_OK`, all M1 scripts, M2/M3 aggregates,
  `M3_NATIVE_UDP_CLIENT_OK`, all 56 TCP cases, `M4_G5_SERVER_INTEROP_OK`,
  forwardproxy normal/race tests, and focused Caddy HTTP tests passed.

No cross-repository product echo is claimed in G0. The deterministic M3 runner
may be used for the broad matrix because it shares the production
backend/factory, but final M5 evidence also requires `out/Release/naive` with
`CertVerifier::CreateDefault()`. A production certificate-bypass switch is
forbidden.

### M5-G1 — first audited-client-to-production-server echo: complete

- Added a dynamic production Caddyfile in forwardproxy commit `d922441`; this
  changes no audited runtime source and retains forwardproxy `8f044e2` as the
  M4 runtime base and Caddy `cce894a8` as the exact server dependency.
- Added `tests/m5/g1_cross_repo_echo.sh`. It owns a fresh temporary root,
  starts the pinned Caddy binary with Basic authentication and H3 Datagrams,
  starts the M3 runner with the real production factory, negotiates SOCKS5
  UDP, and sends one IPv4 datagram to a dynamic loopback echo target.
- The response was byte-identical. Runner evidence proved production-factory
  eligibility and destruction order; NetLog proved a redacted
  `QUIC_PROXY_DATAGRAM_CLIENT_SOCKET` send and redacted CONNECT-UDP request;
  server access/lifecycle logs proved an authenticated target-redacted `200`.
- Privacy scans found no payload, target path, plain credential, password, or
  Base64 credential in client/server evidence. Cleanup left no G1 process or
  temporary root.
- The focused G1 path passed once, then passed three consecutive fresh-root
  repetitions. The cumulative `tests/socks5_udp_m5.sh` now emits both the G0
  contract and `M5_G1_CROSS_REPO_ECHO_OK`.

### M5-G2 — addressing, DNS, multiplexing, and HTTP/3 application: complete

- Added an independent RFC 1928 `net.PacketConn` in the M5 Go module. It
  performs no-auth or RFC 1929 negotiation, sends UDP ASSOCIATE, consumes the
  real BND relay endpoint, retains the control channel, filters unexpected
  relay sources, and preserves IPv4/IPv6/domain identity in both directions.
- Added codec/malformed and live fake-SOCKS tests for empty/binary payloads,
  no-auth, username/password, target identity, and control-owned cleanup.
- Added a dual-stack controlled quic-go HTTP/3 origin and runtime mode to the
  independent probe. The probe uses the SOCKS PacketConn as quic-go's packet
  transport, sends a real HTTP/3 GET through a domain-form target, verifies
  the exact H3 response, and closes the inner QUIC connection. Its inner
  self-signed certificate is verified through an explicit temporary root pool;
  Naive's outer proxy certificate behavior remains unchanged.
- The product UDP matrix verifies IPv4, IPv6, domain response framing,
  deterministic DNS, binary and zero-length datagrams, a 1200-byte safe
  payload, four 4096-byte oversize drops followed by healthy traffic, three
  interleaved targets, and four isolated concurrent SOCKS associations.
- All required markers passed in three fresh-root G2 runs. The cumulative M5
  entry point is green, `go test`, `go test -race`, `go vet`, formatting,
  cleanup, privacy scans, and all three repository `diff --check` boundaries
  pass.

Verified markers:

```text
M5_G2_IPV4_IPV6_DOMAIN_OK
M5_G2_DNS_OK
M5_G2_ZERO_OVERSIZE_OK
M5_G2_MULTI_TARGET_OK
M5_G2_CONCURRENT_ASSOCIATIONS_OK
M5_G2_HTTP3_APPLICATION_OK
M5_G2_PRODUCT_MATRIX_OK
```

### M5-G3 — authentication, policy, malformed input, and isolation: complete

- forwardproxy test-only policy fixtures end at `88ac298`; audited runtime
  source remains based at `8f044e2` and Caddy remains `cce894a8`;
- Correct, missing, and wrong upstream Basic credentials were exercised
  through production Caddy. Local SOCKS success, missing method, and wrong
  password were verified independently.
- Product policy variants returned `403` for port/ACL denial, `502` for DNS
  failure, `501` for unsupported upstream mode, and `503` for the 33rd active
  association. Permitted traffic and a replacement association remained
  healthy.
- Malformed/truncated input, nonzero `FRAG`, invalid address type, bad RSV,
  spoofed source port/IP, and a malformed burst were dropped; a later valid
  datagram on the same association passed.
- Direct, HTTPS/H2, mixed, and unavailable native-UDP backends returned SOCKS
  `0x01`. Default NetLog and server logs contained no target, MASQUE path,
  payload, password, Basic encoding, or double encoding.
- All four required G3 markers passed in focused and cumulative runs. M1-M3,
  all 56 TCP cases, forwardproxy normal/race, Caddy HTTP, and every repository
  `diff --check` remain green; the production M4 idle/restart/privacy suite
  passed immediately before this test-only gate.

### M5-G4 — lifecycle, restart, reconnect, idle, and no replay: complete

- `tests/m5/g4_lifecycle_matrix.sh` verifies idle/open/pending SOCKS control
  closure, two production-Caddy restart cycles, a forced outer QUIC session
  close with two independent targets, and the real 30-second production client
  target-idle boundary;
- unique payload counts prove pre-failure delivery exactly once, deliberately
  ambiguous datagrams zero times, and recovery only from a later fresh
  datagram. A healthy unrelated target remains isolated across session close;
- the real two-minute production server idle is composed into G5's one
  privileged trust window. The 125-second probe observed server
  `idle_expired`, created fresh state, and emitted
  `M5_G4_SERVER_IDLE_RECONNECT_OK`;
- the focused non-privileged matrix passed during development, passed again
  after removing its duplicate trust path, and leaves no process or temporary
  root;
- markers: `M5_G4_CONTROL_CLOSE_OK`, `M5_G4_SERVER_RESTART_OK` (twice),
  `M5_G4_QUIC_RECONNECT_OK`, `M5_G4_CLIENT_IDLE_RECONNECT_OK`,
  `M5_G4_IDLE_RECONNECT_OK`, and `M5_G4_NO_REPLAY_OK`.

### M5-G5 — shipped binary, trust, wire evidence, and baseline: complete

- The untrusted shipped-`naive` negative path failed before any CONNECT-UDP
  `200`. The trusted positive used a temporary user-domain root in the current
  user's login keychain and `CertVerifier::CreateDefault()`; cleanup removed
  its trust/certificate and verified the same server certificate untrusted
  again.
- The first positive attempt exposed that production `naive_proxy_bin.cc`
  configured forced QUIC origins after `URLRequestContextBuilder::Build()`.
  Chromium had already copied `QuicParams`, so a locally trusted root was
  accepted by the default verifier but rejected by the QUIC proof verifier.
  Commit `333b7cb253` moves only the existing QUIC configuration before Build.
- After the fix, shipped `out/Release/naive` passed authenticated IPv4 echo,
  deterministic DNS, the independent SOCKS5-UDP HTTP/3 application request,
  ordinary TCP SOCKS, and the 125-second production server-idle/reconnect case.
- Production NetLog contains the QUIC proxy datagram source and redacted
  CONNECT-UDP evidence; server logs contain redacted association lifecycle.
  Size/timing evidence contains only the four frozen encrypted-shape fields
  across echo, DNS, and HTTP/3 windows. Native UDP v1 adds no padding layer.
- The complete M1-M3 target/script matrix, `M3_NATIVE_UDP_CLIENT_OK`, all 56
  TCP cases, and `git diff --check` pass after `333b7cb253`.
- markers: `M5_G5_UNTRUSTED_CERT_REJECTED_OK`,
  `M5_G5_DEFAULT_CERT_VERIFIER_OK`, `M5_G5_PRODUCTION_BINARY_OK`,
  `M5_G5_H3_DATAGRAM_EVIDENCE_OK`, and
  `M5_G5_NO_PADDING_BASELINE_OK`.

### M5-G6 — regressions, artifact closeout, and independent audit: complete

- Rebuilt every named M1-M3 Release target and reran all M1 scripts, M2, M3,
  `M3_NATIVE_UDP_CLIENT_OK`, and all 56 TCP cases.
- Rebuilt Caddy from the pinned Go 1.25.12/xcaddy 0.4.5, Caddy `cce894a8`,
  forwardproxy `8f044e2` runtime plus test-only M5 fixtures, and quic-go 0.59.0
  inputs. That binary passed cumulative M5 G0-G5.
- G1-G4 then passed three consecutive additional fresh-root repetitions.
  Each produced `M5_G6_FRESH_ROOT_OK`; every run included control close, two
  Caddy restarts, outer-QUIC reconnect, client idle, and no replay.
- Forwardproxy cumulative/legacy, standalone `M4_G5_SERVER_INTEROP_OK`,
  uncached normal/race, and focused Caddy HTTP tests pass. The only race build
  diagnostic is the already recorded harmless macOS `LC_DYSYMTAB` warning.
- The first all-in-one closeout invocation stopped after those completed
  client/product repetitions because the new runner launched the standalone
  server script outside its Go module. No product test failed. The cwd-only
  harness fix is `d1aee3663f`; the corrected server half was rebuilt and run
  independently to `M5_G6_SERVER_REGRESSIONS_OK`.
- Three-repository diff/status checks, M5 diff extension/path scan, exact
  process-name scan, and login-keychain certificate scan pass. Only unrelated
  `.DS_Store` and `src/tmp/` remain untracked.
- local marker: `M5_G6_LOCAL_REGRESSIONS_OK`.

- A continuing read-only Gemini 3.1 Pro High `agy -p` review inspected
  NaiveProxy `cd9a676df9..eaf172d971`, forwardproxy runtime `8f044e2` plus
  M5-only fixtures through `2b2a8ea`, and exact Caddy `cce894a8`.
- It confirmed the RFC 9298/H3 DATAGRAM-only path, unchanged TCP data path,
  pre-Build QUIC configuration, default verifier, trust cleanup, independent
  H3 application probe, lifecycle/no replay, privacy baseline, G6 evidence,
  and dependency pins.
- In the same review session it independently reran
  `tests/m5/g1_cross_repo_echo.sh`; the command exited `0` with all five G1
  byte/auth/H3/privacy/aggregate markers and left no review-created change.
- Findings: zero blocker, high, medium, or low. Verdict: `AUDIT_PASS` with
  `Zero blocker, high, or medium findings.`
- Durable report: [`m5-agy-audit.md`](m5-agy-audit.md).

Final marker: `M5_NATIVE_UDP_MVP_OK`. M5 is complete; M6 now owns network
impairment, PMTU, soak, fuzz/sanitizer expansion, multi-platform qualification,
and release readiness.

## M6 execution baseline — hardening and release candidate

Status: complete (all G0-G6 gates closed; release candidate qualified; independent audit `AUDIT_PASS`). The plan was
[`m6-execution-plan.md`](m6-execution-plan.md).

M6 inherits the independently audited M5 boundary and keeps production client,
server, and Caddy source frozen unless a later hardening gate exposes a
reproducible defect. Its sequence is:

```text
G0  release contract, audited inputs, environment and platform evidence states
G1  safe inner payload ceiling and PMTU behavior
G2  deterministic loss/reordering/delay/jitter/bandwidth profiles
G3  resource pressure, reclamation and long-running soak
G4  fuzz, sanitizer, race and randomized lifecycle hardening
G5  macOS/Linux/Windows/Android qualification
G6  full release checklist, clean builds and independent AUDIT_PASS
```

### M6-G0 — release contract and environment readiness: complete

- Commit `80d37395a6` adds the G0-G6 plan, machine-readable contract, five
  contract tests, and a read-only environment/baseline runner.
- The contract freezes exact M5/client/server/Caddy/toolchain inputs, the
  RFC 9298/H3 DATAGRAM-only protocol, no replay/no padding decisions,
  fail-closed blocker policy, duration tiers, and forbidden artifacts.
- Platform claims remain separate: macOS arm64 is verified as G0 environment-
  ready; Linux x64, Windows x64, and Android arm64 are explicitly `not run`.
  They remain required for G5 and are not implied by the current-host pass.
- G2's default impairment mechanism is a future non-privileged seeded
  user-space UDP shaper. Available `pf`/`dnctl` facilities are optional and no
  administrator permission or trust mutation was used by G0.
- The exact Chromium-matched Clang revision, Python 3, Ninja, Go 1.25.12,
  xcaddy 0.4.5, four existing Release binaries, all three Git boundaries, M5
  audit verdict, and source-drift constraints passed.
- An incremental build of `naive`, `naive_socks5_udp_test`,
  `naive_connect_udp_backend_test`, and `naive_socks5_udp_m3_runner` reported
  `ninja: no work to do.`
- No production source, sibling repository, certificate trust, or generated
  private artifact changed. Only the pre-existing excluded `.DS_Store` and
  `src/tmp/` entries remain untracked.

Verified commands:

```bash
cd /path/to/naiveproxy
python3 tests/m6/contract_test.py
./tests/m6/g0_contract.sh
ninja -C src/out/Release naive naive_socks5_udp_test \
  naive_connect_udp_backend_test naive_socks5_udp_m3_runner
git diff --check
```

Verified markers:

```text
M6_G0_RELEASE_CONTRACT_OK
M6_G0_PLATFORM_CONTRACT_OK
M6_G0_M5_BASELINE_OK
M6_G0_TOOLCHAIN_OK
M6_G0_CONTRACT_OK
```

Next: M6-G1 must measure the live payload ceiling and PMTU-change behavior
before choosing the release policy. The existing 1200-byte success and
4096-byte oversize probes are inherited evidence, not the final G1 ceiling.

### M6-G1 — inner payload ceiling and PMTU behavior: complete

Commit `1870779147` is the first narrow test-only step. It extends the existing
production-backend deterministic target and proves that the backend:

- admits a payload exactly at the tunnel's current ceiling without truncation;
- drops ceiling-plus-one before calling tunnel `Write()`;
- re-queries the live ceiling before a later write after it shrinks;
- resumes exact delivery after the ceiling is restored; and
- counts two admitted/sent and two oversize-dropped datagrams without making
  the association fatal.

Verified command and marker:

```bash
ninja -C src/out/Release naive_connect_udp_backend_test
src/out/Release/naive_connect_udp_backend_test
# M6_G1_LIVE_CEILING_UNIT_OK
./tests/socks5_udp_m3.sh
# M3_NATIVE_UDP_CLIENT_OK
```

This is G1a evidence only. It does not establish the release payload value or
claim a real PMTU change; G1b-G1d remain open.

Hardening observation: the first cumulative M3 invocation stopped in its
pre-existing idle-reconnect segment before the final marker. With no source or
configuration change, an immediate full rerun exited `0` through
`M3_NATIVE_UDP_CLIENT_OK`. The successful rerun is regression evidence, but the
single transient is retained as a G3/G4 flake/soak investigation input rather
than being silently counted as a repeated qualification pass.

#### M6-G1b1/G1c — live product ceiling and PMTU fixture: complete

Commit `9c72a7da08` adds a black-box SOCKS5 UDP payload probe, a test-only
production Caddy fixture with trust installation disabled, and a user-space
outer-QUIC UDP shaper. It changes no production client/server source and does
not modify system trust.

Verified behavior:

- Three independent production-backend/production-Caddy roots measured the
  same live inner ceiling for IPv4, IPv6, and domain targets: 1314 bytes.
- Each root repeated exact-ceiling delivery three times, rejected 1315 bytes
  three times, then delivered later healthy traffic on the same association.
- The PMTU fixture interprets IPv6 minimum PMTU 1280 correctly as an outer UDP
  payload ceiling of 1232 bytes after 40-byte IPv6 and 8-byte UDP headers.
- Under that ceiling, the 1200-byte candidate inner payload produced a
  1225-byte outer QUIC packet and passed. The 1314-byte inner payload produced
  a 1345-byte outer packet and was dropped by the shaper.
- Restoring the outer ceiling delivered a fresh 1314-byte datagram; the
  ambiguous dropped datagram was not replayed. A separate IPv6 target and
  SOCKS association stayed healthy during the lower-PMTU interval.
- Three fresh PMTU roots passed with size-only shaper evidence. Default NetLog
  and production server logs contained no target path or credential. An early
  development run using `--net-log-everything` correctly exposed configured
  credentials to its local diagnostic artifact and was rejected by the
  privacy gate; the final harness deliberately uses default NetLog and the
  temporary diagnostic root was deleted.

Verified commands:

```bash
./tests/m6/g1_live_ceiling.sh
M6_G1_PROBE_MODE=pmtu ./tests/m6/g1_live_ceiling.sh
python3 tests/m6/contract_test.py
git diff --check
```

Verified markers:

```text
M6_G1_LIVE_PRODUCT_CEILING_OK bytes=1314
M6_G1_LIVE_CEILING_PRIVACY_OK
M6_G1B_LIVE_CEILING_OK
M6_G1_PMTU_SAFE_PAYLOAD_OK bytes=1200
M6_G1_PMTU_ISOLATION_OK
M6_G1_PMTU_NO_REPLAY_OK
M6_G1C_PMTU_RECOVERY_OK
M6_G1C_PMTU_OK
```

G1b2 completed through shipped `src/out/Release/naive` with the default
verifier inside one explicitly authorized user-domain trust window. The initial
`./tests/m6/g1_finalize.sh` invocation emitted the untrusted negative marker,
measured 1314 bytes for IPv4/IPv6/domain, emitted the shipped/default-verifier
markers, and removed the temporary trust before a test-harness TCP cwd bug
stopped its later regression phase. No product or trust failure occurred.

The non-mutating negative-only path passed at `182267a1ca`: shipped `naive`
used its default verifier, the untrusted temporary root could not establish a
CONNECT-UDP association, and the run ended with
`M6_G1B2_UNTRUSTED_CERT_REJECTED_OK` and `M6_G1B2_NEGATIVE_ONLY_OK`. This does
not satisfy the positive ceiling or trust-cleanup half of G1b2.

The closeout runner was corrected to invoke the TCP matrix from `src/` with a
relative script path. With the shipped phase already completed and trust
cleanup verified, `M6_G1_SKIP_SHIPPED=1 ./tests/m6/g1_finalize.sh` ran three
complete repetitions of live ceiling, lowered/restored PMTU, the cumulative M3
client suite, all 56 TCP cases, uncached forwardproxy tests, and focused Caddy
HTTP tests. Each repetition emitted `M6_G1D_REGRESSION_RUN_OK`; the command
ended with `M6_G1_PAYLOAD_PMTU_OK`. The separate payload policy remains a
candidate until G5 platform records are resolved.

### M6-G2 — deterministic network impairment: complete

Commit `028d3984d4` extends the test-only UDP shaper with named, seeded loss,
reordering, delay/jitter, and bandwidth scheduling; adds aggregate-only shaper
logs; and composes the production M3 backend, pinned M4 Caddy/forwardproxy,
generic UDP echo, DNS, and the independent quic-go HTTP/3 application probe.
No production runtime source changed.

Frozen profiles:

| Profile | Seed | Contract |
| --- | ---: | --- |
| delay | 101 | 20ms delay, 5ms jitter |
| loss | 202 | 5% seeded outer-packet loss |
| reorder | 303 | 20% seeded reordering with 30ms extra delay |
| bandwidth | 404 | 256 Kbit/s per-direction serialization |
| combined | 505 | 2% loss, 10% reordering, 10ms delay/5ms jitter, 512 Kbit/s |

Each profile verifies:

- 20 unique application datagrams, zero duplicate/corrupt/cross-target
  responses, and a protocol-aware delivery floor for unreliable profiles;
- DNS success within a bounded retry budget;
- an independent HTTP/3 request/response over a separate CONNECT-UDP target;
- SOCKS control close stops the closed association under impairment;
- removing the profile restores fresh traffic on the original association;
- no ambiguous datagram replay and healthy H3 target isolation;
- profile-specific shaper actions and default-NetLog/server privacy.

The committed three-run matrix passed 15 independent product roots. Delay and
bandwidth delivered 20/20 application datagrams in every run. Loss delivered
17-18/20, reorder 15-17/20, and combined 17-19/20; all had zero duplicates,
all DNS/H3 operations completed, and all removal/recovery checks passed. The
variation is expected for unreliable DATAGRAM delivery because the seeded
shaper sees the real runtime's packet sequence; the deterministic contract is
the profile/seed and safety outcome, not an invented lossless UDP count.

Verified commands and final markers:

```bash
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tests/m6 \
  python3 tests/m6/test_udp_shaper.py
./tests/m6/g2_network_matrix.sh
# M6_G2_FRESH_ROOT_MATRIX_OK run=1
# M6_G2_FRESH_ROOT_MATRIX_OK run=2
# M6_G2_FRESH_ROOT_MATRIX_OK run=3
# M6_G2_NETWORK_IMPAIRMENT_OK
```

G2 artifacts are temporary and deleted on success/failure/signal. Committed
profile evidence is limited to profile, seed, direction, size, action, reason,
elapsed time, and aggregate counts; it excludes endpoints, paths, credentials,
and packet bytes.

### M6-G3 — resource pressure and soak: complete

Commit `325c77b95a` added the bounded association/churn/resource harness. The
smoke evidence already passed with 256 active client associations, rejection
of the 257th, reuse of 64 released slots, 101 waves, 1616 product datagrams,
and recovery of runner/Caddy file descriptors.

The first unshortened qualification body's pressure work on the pre-fix runtime
also passed: 5634
waves, 90144 product datagrams, the same 256/257th/64 admission-reuse boundary,
runner RSS 13376 -> 18496 KiB and FD 10 -> 17, and Caddy RSS 41968 -> 41152
KiB and FD 12 -> 13. The wrapper then failed without
`M6_G3_STRESS_SOAK_OK` because its long-running shell had started before the
G4 allowlist change and reached an inconsistent post-probe branch. That run is
not counted as a pass and was invalidated by the subsequent Caddy race fix.
Commit `8ab48dbee1` replaces the non-portable BSD `sed` allowlist expressions;
a one-second stress smoke completed through privacy and harness markers.

A later post-fix 3600-second body on Caddy `dd9a89c1` completed 5391 waves and
86256 product datagrams. Its runner RSS was 13376 -> 19776 KiB with a
18864-KiB sampled peak; runner FDs were 10 -> 17 with a 526-FD admission-test
peak. Caddy RSS was 42592 -> 42768 KiB with a 42480-KiB sampled peak, and
Caddy FDs were 12 -> 13. Admission, churn, resource recovery, privacy, and
harness markers passed, but the wrapper emitted only the explicit-duration
smoke marker rather than `M6_G3_STRESS_SOAK_OK`. It therefore remains useful
diagnostic evidence and is not counted as qualification. Commit `5257e2757f`
records the pre-run explicit-duration decision directly; its one-second
override regression passed.

The clean command below then completed on Caddy `dd9a89c1` and forwardproxy
lock `e9663e4`:

```bash
env -u M6_G3_DURATION_SECONDS M6_G3_TIER=qualification \
  ./tests/m6/g3_stress_soak.sh
# M6_G3_ASSOCIATION_CAP_REUSE_OK active=256 rejected=1 reused=64
# M6_G3_RESOURCE_PEAK process=runner rss_kib=19904 fd=526
# M6_G3_RESOURCE_PEAK process=caddy rss_kib=44240 fd=13
# M6_G3_RESOURCE_SAMPLE process=runner rss_before_kib=13392 rss_after_kib=18832 fd_before=10 fd_after=17
# M6_G3_RESOURCE_SAMPLE process=caddy rss_before_kib=42672 rss_after_kib=40144 fd_before=12 fd_after=13
# M6_G3_CHURN_OK waves=5787 datagrams=92592
# M6_G3_RESOURCE_RECOVERY_OK
# M6_G3_STRESS_HARNESS_OK
# M6_G3_TIER_OK tier=qualification duration_seconds=3600
# M6_G3_STRESS_SOAK_OK
```

The final root had no crash, hang, stale process, capacity leak, privacy leak,
or replay. Runner RSS remained bounded and runner FDs returned from the
526-FD admission-test peak to 17; Caddy RSS decreased across the soak and its
FD count ended at 13.

The G3 harness covers association admission, churn, post-close resource
recovery, and bounded RSS/FD deltas. M5-G4 remains the inherited evidence for
server restart, idle expiry, control close, outer-session shutdown, and no
replay; G2's loss profile remains the inherited outer-QUIC impairment evidence.

### M6-G4 — fuzz, sanitizer, race, and lifecycle hardening: complete post-fix

Commit `5893f97f6e` adds a deterministic client codec-fuzz executable, the
seeded asynchronous lifecycle schedule, a fail-closed ASan/UBSan configuration,
and the frozen-budget cross-repository runner. It also extends the machine
contract with the exact seeds, iteration counts, lifecycle count, and Go fuzz
duration. The runner does not accept a shortened Go fuzz duration as a final
pass; diagnostic short runs must be invoked outside the gate runner.

Verified command and evidence:

```bash
./tests/m6/g4_sanitizer_fuzz.sh
# M6_G4_CODEC_FUZZ_OK seed=20260720 iterations=1000000 valid=44027
# M6_G4_CODEC_FUZZ_OK seed=9298 iterations=1000000 valid=43374
# M6_G4_CODEC_FUZZ_OK seed=1928 iterations=1000000 valid=43459
# M6_G4_RELEASE_CODEC_FUZZ_OK
# M6_G4_SEEDED_LIFECYCLE_OK iterations=2000
# M6_G4_ASAN_UBSAN_OK
# M6_G4_GO_RACE_FUZZ_OK
# M6_G4_SANITIZER_FUZZ_OK
```

The first full attempt exposed a race in pinned Caddy's automatic
HTTPS/certmagic startup and stopped without a pass marker. Although a second
run passed, the finding was real: `caddytls.TLS.CaddyModule()` used a value
receiver and copied mutable app state while `Start()`/`keepStorageClean()` was
writing it. Caddy commit `dd9a89c11194dcb806d845233995ef040f096464` changes the
module registration and receiver to pointer semantics; forwardproxy build-lock
commit `e9663e4` pins it. Caddy module ordinary/race tests, forwardproxy local-
Caddy full race three times, M4 owner integration, G5 server smoke, and build
reproduction passed. The final G4 runner was rerun on this new runtime and
passed. The previous marker is retained as pre-fix evidence only; the post-fix
marker is the release evidence.

Post-fix command and final evidence:

```bash
./tests/m6/g4_sanitizer_fuzz.sh
# M6_G4_RELEASE_CODEC_FUZZ_OK
# M6_G4_SEEDED_LIFECYCLE_OK iterations=2000
# M6_G4_ASAN_UBSAN_OK
# M6_G4_GO_RACE_FUZZ_OK
# M6_G4_SANITIZER_FUZZ_OK
```

The post-fix run used Caddy `dd9a89c1` and forwardproxy lock `e9663e4`; runner
commit `c58cc49b19` creates a temporary Go modfile replacing Caddy with that
worktree, so the race/fuzz evidence cannot silently use the old module cache.
No race was reported. The macOS linker warning is unchanged and non-fatal.

### M6-G5a — platform evidence contract: complete

Commits `9869f1d6d1` and `09af3795c7` add the fail-closed platform record and
its contract runner, and split G5 into
separately attributable macOS arm64, Linux x64, Windows x64, Android arm64,
and final interoperability sub-gates. All four records remain `not run` until
their exact OS/architecture, three repository revisions, commands, and markers
are populated. Contract validation passes, but this is record readiness only;
it is not platform qualification evidence.

```bash
./tests/m6/g5_platform_contract.sh
# M6_G5_PLATFORM_STATE id=macos-arm64 state=not run
# M6_G5_PLATFORM_STATE id=linux-x64 state=not run
# M6_G5_PLATFORM_STATE id=windows-x64 state=not run
# M6_G5_PLATFORM_STATE id=android-arm64 state=not run
# M6_G5_PLATFORM_CONTRACT_OK
```

### M6-G5b — macOS arm64 qualification: complete

`tests/m6/g5_macos_qualification.sh` composes an exact-revision macOS arm64
Release build, the shipped-client product smoke, and focused G2-G4 gates. Its
positive path is fail-closed unless
`M6_G5_TEMPORARY_TRUST_AUTHORIZED=1` is set for that invocation. The M5
shipped-product fixture retains its historical default revisions while
accepting explicit M6 revisions from this wrapper.

The non-mutating negative preflight passed against NaiveProxy `faf6da23fc`,
forwardproxy `e9663e4`, and Caddy `dd9a89c1`:

```bash
M6_G5_NEGATIVE_ONLY=1 ./tests/m6/g5_macos_qualification.sh
# M6_G5B_MACOS_BUILD_OK
# M5_G5_UNTRUSTED_CERT_REJECTED_OK
# M5_G5_NEGATIVE_ONLY_OK
# M6_G5B_MACOS_NEGATIVE_ONLY_OK
```

This proved build readiness and default-verifier rejection without modifying
trust. The later explicitly authorized positive command used one temporary
user-domain trust window and removed it before the non-privileged focused
gates:

```bash
M6_G5_TEMPORARY_TRUST_AUTHORIZED=1 \
  ./tests/m6/g5_macos_qualification.sh
# M6_G5B_MACOS_BUILD_OK
# M6_G5B_MACOS_PRODUCT_OK
# M6_G2_NETWORK_IMPAIRMENT_OK
# M6_G3_STRESS_SMOKE_MATRIX_OK
# M6_G4_SANITIZER_FUZZ_OK
# M6_G5B_MACOS_ARM64_OK
```

The original run reported untrusted/trusted default-verifier behavior, UDP
echo, DNS, an independent HTTP/3 application, ordinary TCP SOCKS, the real
125-second server idle/reconnect boundary, H3 Datagram wire evidence, and the
no-padding baseline. All five focused impairment profiles passed; the 60-second
stress root completed 101 waves/1616 datagrams and reclaimed capacity; the
full frozen G4 codec/ASan/UBSan/race/Go-fuzz budget passed again. The temporary
CA was removed and later gates required no trust mutation. The TCP claim is
superseded by the corrected forced-SOCKS evidence below; the remaining markers
retain their historical value but do not close the current platform row.

The first machine record bound `verified` macOS arm64 evidence to macOS 26.5.2
build 25F84, NaiveProxy `350fc4e694c3dece134e5aa110ed24f307733e16`,
forwardproxy `e9663e4bd7222fd3ec3bd516c71e23fd5d482188`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. This did not qualify Linux,
Windows, or Android, and it no longer satisfies the corrected G5 contract.

The loopback TCP parity command used `curl --proxy` while `NO_PROXY` included
loopback, so the successful macOS response could be direct. Adding
`--noproxy ''` reproduced the real defect on macOS and Linux: a successful
CONNECT-UDP response cached proxy padding as `None`; the later fast-open TCP
CONNECT sent unpadded bytes while forwardproxy expected Variant1. The narrow
owner fix `baa7f2dd0845aa4cb55e39b4cc67c9b6a59b6285` advertises the negotiated
padding capability on CONNECT-UDP responses without padding UDP DATAGRAM
payloads. A controlled M3 production-path runner then completed a real SOCKS
TCP request and logged `negotiated padding type: Variant1`.

The corrected requalification completed on macOS 26.5.2 build 25F84, arm64,
at NaiveProxy `d402f9261c6ff3fe92bbd699e57051bccef2d61e`, forwardproxy
`f14924cdedc93c28a2b92c8120538ea5beee28fb`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. The negative-only preflight and
the explicitly authorized full run both passed. The full run verified shipped
UDP echo, DNS, independent HTTP/3 application, forced-SOCKS TCP, default
verifier rejection/acceptance/cleanup, idle/reconnect, H3 Datagram evidence,
all five impairment profiles, the 60-second 101-wave/1616-datagram stress
root, and the complete ASan/UBSan/race/fuzz budget. Final marker:

```text
M6_G5B_MACOS_ARM64_OK
```

The machine-readable platform record now marks `macos-arm64` verified; Linux,
Windows, and Android remain `not run`.

### M6-G5e — Android arm64 qualification: complete

GitHub Actions run `29743425559` built the Android arm64 Naive binary and
plugin APK at NaiveProxy `58a7ac9821d9b01d5ab95f154e0eeff33fb4ea84` on
Ubuntu 22.04.5, then verified the packaged AArch64 ELF and exported SagerNet
plugin provider boundary:

```text
M6_G5E_ANDROID_ARM64_ELF_OK
M6_G5E_ANDROID_PLUGIN_PACKAGE_OK
M6_G5E_ANDROID_ARM64_BUILD_READY
```

Runtime qualification then ran on a physical Android arm64 device (Android 16,
`Android-build-redacted`, arm64-v8a, ADB) against the LAN M5
Caddy/forwardproxy server (Caddy `dd9a89c1`, forwardproxy `964281a9`) with
the shipped NDK Release `naive` built at NaiveProxy
`474a1e4b0aeb9c64e6d0083eaddd205c887bf608` (31 commits ahead of the GitHub
build; UDP-hardening and Windows G5d work). A temporary per-process CA
(`SSL_CERT_FILE`) authorized the direct-`naive` runs, and a temporary
system-trust-store installation of the same test CA authorized the host-app
runs; both trust inputs were removed before closeout.

Verified on-device: shipped UDP echo, the 1314-byte live-ceiling and
1200-byte safe payloads, DNS, zero/oversize limits, control close, the
125-second server-idle reconnect (server `idle_expired` plus a fresh
association in the Caddy log), an independent quic-go HTTP/3 application
probe through CONNECT-UDP, forced-SOCKS TCP parity expecting
`m5-production-tcp-ok`, and QUIC proxy-datagram NetLog evidence. The
untrusted A/B (test CA removed from the system trust store, no
`SSL_CERT_FILE`) rejected the self-signed proxy certificate with a QUIC
handshake failure, no CONNECT-UDP success reached the server, and the echo
probe timed out. The NekoBox (`moe.nb4a`) host app ran the SagerNet naive
plugin node with `tun0` up, a real-web CONNECT returning HTTP 200 through
the plugin, and traffic continuing across `am freeze`/`am unfreeze`.

```text
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_DEFAULT_CERT_VERIFIER_OK
M5_G5_PRODUCTION_ECHO_OK
G5E_PAYLOAD_1314_OK
G5E_PAYLOAD_1200_OK
M3_G5_ZERO_OVERSIZE_OK
M3_G4_DNS_OK
M5_G2_HTTP3_APPLICATION_OK
M5_G5_PRODUCTION_TCP_OK
M5_G4_CONTROL_CLOSE_OK
M5_G4_SERVER_IDLE_RECONNECT_OK
M5_G5_H3_DATAGRAM_EVIDENCE_OK
M6_G5E_HOST_APP_NEKOBBOX_OK
M6_G5E_LIFECYCLE_FREEZE_OK
M6_G5E_ANDROID_ARM64_OK
```

The machine-readable `android-arm64` record is now `verified`, closing the
final open G5 platform row. With macOS, Linux, Windows, and Android all
verified and the G5f wire gate passing, the G5 exit is satisfied and
`M6_G5_PLATFORM_QUALIFICATION_OK` is recorded.

### M6-G5c — Linux x64 qualification: complete

The shipped-product fixture now selects the platform default verifier without
a bypass: macOS uses its temporary user-domain root, native Linux reads a
temporary per-process `SSL_CERT_FILE`, and Windows uses the temporary user Root
store. Every positive path removes its trust input and starts shipped `naive`
again to require `M5_G5_TRUST_CLEANUP_OK` from an untrusted connection.

`tests/m6/g5_linux_qualification.sh` requires native `Linux x86_64`, exact
NaiveProxy/forwardproxy/Caddy revisions, Go 1.25.12, and a reproducibly built
server. It composes shipped-client echo, DNS, independent H3, TCP, trust,
server idle, focused impairment, lifecycle pressure, and server regressions.
The pinned `.github/workflows/m6-platform-qualification.yml` job runs on an
actual GitHub-hosted Ubuntu x64 runner and retains only aggregate marker and
revision evidence. Local macOS checks validate its shell/static contract but
are not counted as Linux runtime evidence.

The first native-x64 run, GitHub Actions `29727010346`, completed the full
Naive Release/native-UDP build and pinned Caddy/forwardproxy build, then
stopped before product traffic because Caddy's automatic HTTPS redirect tried
to bind privileged Linux port 80. This is fixture portability rather than a
runtime defect. Forwardproxy fixture commit `444667f` adds
`auto_https disable_redirects`; the HTTPS/H3 listener and production module
stack are unchanged. That failed run is build evidence only and is not counted
as G5c runtime qualification.

The second native-x64 run, GitHub Actions `29729141865`, built NaiveProxy and
the pinned production server successfully, then passed shipped UDP echo, DNS,
and the independent HTTP/3 application probe. Its forced TCP parity request
timed out, exposing the cross-protocol padding-cache defect described above;
it did not emit `M6_G5C_LINUX_X64_OK` and is not qualification evidence. The
next runner pins forwardproxy qualification head `f14924cd` (runtime fix
`baa7f2dd`) and forces SOCKS independently of `NO_PROXY`.

### M6 forwardproxy qualification fixture correction — verified

The failed hostname-site parity result was independently reduced to a Caddy
route-matching issue, not an HTTP/3 response-header serialization defect.
Ordinary CONNECT uses the target as its `:authority`, so a site address such
as `https://m5-proxy.localhost:<port>` prevents the request from reaching the
forwardproxy handler. CONNECT-UDP uses the proxy authority and therefore hid
the fixture error. Forwardproxy qualification commit `f14924cd` changes
`tests/m5/Caddyfile-trusted` to an explicit-certificate `https://:<port>` TLS
listener without a Host matcher and adds
`scripts/test-m6-hostless-forward-proxy.sh`.

Verified with the rebuilt Caddy binary and the independent quic-go client:

```text
M6_H3_TCP_PADDING_INTEROP_OK
M4_G5_BINARY_SMOKE_OK
M6_HOSTLESS_TCP_UDP_INTEROP_OK
```

The same qualification head passed owner `go test ./...`, `go test -race ./...`,
the complete `scripts/test-m4.sh`, and the complete
`scripts/test-m4-g5-server.sh` matrix, including idle expiry, restart,
resource, and log-privacy markers. The original platform qualification scripts
and M5 shipped-product default pin used
`f14924cdedc93c28a2b92c8120538ea5beee28fb`. Current/future runs advance to
test-only qualification head `964281a9797efd9a4c953f6273c73e397e777864`;
the runtime padding fix remains separately identified as `baa7f2dd`.

The corrected native run, GitHub Actions `29754432052`, job `88393013948`,
completed on Ubuntu 22.04.5 LTS x86_64 at NaiveProxy
`f7e206a308404d8324e609bc0463f3b7dc7734e6`, forwardproxy
`f14924cdedc93c28a2b92c8120538ea5beee28fb`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. It reproduced the Release client
and pinned server, then passed shipped UDP echo, DNS, independent HTTP/3,
forced-SOCKS TCP, default-verifier rejection/acceptance/cleanup, server idle,
all five impairment profiles, the 60-second pressure matrix, and server
regressions. The redacted artifact ended with:

```text
M6_G5C_LINUX_PRODUCT_OK
M6_G2_NETWORK_IMPAIRMENT_OK
M6_G3_STRESS_SMOKE_MATRIX_OK
M6_G5C_LINUX_SERVER_OK
M6_G5C_LINUX_X64_OK
```

The machine-readable platform record now marks `linux-x64` verified. Windows
and Android remain `not run`.

### M6-G5d — Windows x64 qualification: complete

GitHub Actions run `30013662603` at NaiveProxy `d958cc8017` reproduced the
Windows Release client and pinned Caddy/forwardproxy server, then stalled in
the temporary user-root installation phase. Its original watchdog terminated
only the MSYS shell while a native child survived. Commit `4b2d50833d` split
the trust phases, added process-tree termination, and raised the outer job
budget; the next run proved that process-tree cleanup exits promptly.

Run `30060226705` at `4b2d50833d` again reproduced both binaries and passed the
real shipped-client untrusted-certificate rejection. It then stopped at
`temporary-trust-store-check`. Retained redacted evidence showed
`certutil -user -addstore` reporting success, followed by an exact SHA-1 store
query returning `NTE_NOT_FOUND`; MSYS process-state polling then remained
blocked until the 20-minute product watchdog. No positive product marker or
G5d pass marker was emitted, so neither run is Windows runtime evidence.

Commit `a620d7da7d` removes `certutil` and its MSYS PID polling from the Windows
fixture. Installation, exact-thumbprint presence, and removal now use
synchronous .NET `X509Store("Root", CurrentUser)` operations. The real shipped
`naive.exe` still owns the meaningful trust proof: untrusted failure, positive
traffic after temporary trust, and failure again after exact cleanup. Local
non-mutating verification passed:

```text
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_NEGATIVE_ONLY_OK
M6_G5_PLATFORM_CONTRACT_OK
```

Shell syntax and `git diff --check` also passed. At that point the
`windows-x64` machine record remained `not run`, and `a620d7da7d` was only a
candidate pending a native rerun.

The native rerun `30064158390` at NaiveProxy `2bc4e1e7b8` superseded that
candidate. It again reproduced the Windows Release client and pinned server,
and the shipped-client negative phase passed. The .NET operation then hung in
`temporary-trust-install`; the product watchdog fired after 20 minutes, and
the protected Root-store operation prevented prompt process-tree teardown
until the 180-minute job limit cancelled the run. This proves the unstable
boundary is unattended mutation of `CurrentUser\Root`, not the choice between
`certutil` and .NET APIs. The run emitted no positive product or G5d marker.

Chromium's Windows `TrustStoreWin` source provides a narrower supported local
trust path: it reads `LocalMachine\TrustedPeople` and treats self-signed
server certificates there as trusted leaves, while intentionally excluding
`CurrentUser\TrustedPeople`. Commit `d5875a05d3` follows that boundary. The
Windows proxy fixture now uses a one-day self-signed server leaf, installs and
removes only its exact thumbprint in machine `TrustedPeople`, and retains the
real shipped `naive.exe` negative/positive/cleanup-negative proof. It never
modifies a Root store and does not change `CertVerifier::CreateDefault()`.

The workflow now runs a three-minute native TrustedPeople install/check/remove
preflight before the approximately 50-minute Chromium build. Local Shell,
workflow-YAML, 19-test contract, `git diff --check`, and non-mutating shipped-
client negative checks pass. `windows-x64` remains `not run` until the new
native workflow passes through `M6_G5D_WINDOWS_X64_OK`.

Run `30084029306`, job `89451910893`, at NaiveProxy `f32990a75a`,
forwardproxy `f14924cd`, and Caddy `dd9a89c1` validated the new trust boundary:
the three-second TrustedPeople preflight passed, the Windows Release client
and pinned server rebuilt, the shipped default verifier rejected the
untrusted leaf, and trusted UDP echo plus DNS emitted
`M5_G5_PRODUCTION_ECHO_OK` and `M3_G4_DNS_OK`. The independent HTTP/3
application probe then reported `timeout: no recent network activity` while
its SOCKS target was the custom domain `m5-h3.localhost`; no H3, TCP,
lifecycle, cleanup-negative, product, or G5d final marker was emitted.

Candidate test-fixture commit `a2e06cc21a` removes that Windows-specific
resolver dependency. Windows now sends the exact `localhost` name as an RFC
1928 domain target while retaining `m5-h3.localhost` as the inner HTTP/3 TLS
identity and explicit fixture CA. Other platforms keep the audited target
unchanged. The H3 probe output is captured separately so failures emit only
the existing redacted client/server lifecycle logs rather than a raw target
URL. Local verification passed:

```text
19 M6 contract tests
M5 Go tests
Shell syntax checks
WORKFLOW_YAML_OK
M5_G5_UNTRUSTED_CERT_REJECTED_OK
M5_G5_NEGATIVE_ONLY_OK
git diff --check
```

This is a candidate harness correction, not Windows runtime evidence. G5d
remains fail-closed until a fresh native run emits every required marker and
ends with `M6_G5D_WINDOWS_X64_OK`.

Run `30097890690`, job `89496376267`, at NaiveProxy `35136124ed` proved the
H3 resolver correction. In addition to the earlier trust, build, rejection,
UDP echo, and DNS evidence, it emitted:

```text
M5_G2_HTTP3_CONNECTION_CLOSED
M5_G2_HTTP3_APPLICATION_OK
M5_G5_PRODUCTION_TCP_OK
```

The next control-close assertion received Python `ConnectionResetError` with
Windows error `10054` (`WSAECONNRESET`) after sending to the already closed
local UDP relay. The assertion already accepted POSIX timeout and an explicit
`ConnectionRefusedError`; no application datagram was delivered. Candidate
test-oracle commit `3c3db3885c` accepts only `winerror == 10054`, only when the
caller has explicitly allowed a refused closed relay. Other reset errors and
unexpected packets still fail. Five behavior tests cover timeout, allowed and
disallowed 10054, another reset, and unexpected data. The test is also part of
the three-minute Windows preflight so future regressions fail before the
Chromium build.

Local candidate verification passed:

```text
5 Windows UDP error-semantics tests
19 M6 contract tests
WORKFLOW_YAML_OK
M2_SOCKS5_UDP_INGRESS_OK
Shell syntax checks
git diff --check
```

No production source changed in this gate. G5d is **complete**: the fresh native
Windows run passed control close, idle/reconnect, trust cleanup, and server
tests, emitting `M6_G5D_WINDOWS_X64_OK` (GitHub Actions full run `30167583501`).

The `3c3db3885c` preflight test was later found insufficient: it injected a
synthetic `ConnectionResetError` subclass with a hand-written `winerror`
attribute, so the exact numeric guard could pass without exercising a real
Winsock exception. Run `30102201100` at `9951aedfae` entered the full product
gate but did not produce a G5d marker; it was cancelled after the failed
product path remained in cleanup. It is not qualification evidence.

Commit `5bcdd97107` replaces that synthetic boundary. An explicitly opted-in
closed-relay assertion now accepts the `ConnectionResetError` type without
depending on runtime-specific numeric attributes; all other connection errors
and unexpected packets still fail. The Windows-only sixth test sends a real
UDP packet to an actual closed loopback port, captures the native exception,
and passes that object through the production test oracle. A new
`windows-x64-preflight` workflow input runs this probe and the TrustedPeople
lifecycle without building Chromium or Caddy.

GitHub Actions run `30107431553`, job `89528169413`, passed on native Windows
Server 2022 at `5bcdd97107` in 29 seconds:

```text
Ran 6 tests in 0.018s
OK
M6_G5D_WINDOWS_UDP_ERROR_SEMANTICS_OK
M5_WINDOWS_TRUSTED_LEAF_INSTALL_OK
M5_WINDOWS_TRUSTED_LEAF_CHECK_OK
M5_WINDOWS_TRUSTED_LEAF_REMOVE_OK
M6_G5D_WINDOWS_TRUST_PREFLIGHT_OK
M6_G5D_WINDOWS_PREFLIGHT_ONLY_OK
```

Local verification also passed the 19 M6 contract tests, workflow YAML and
Shell checks, Python compilation, `git diff --check`, and the complete
`M2_SOCKS5_UDP_INGRESS_OK` regression. No production source changed. This
preflight closes the specific synthetic-test gap but does not qualify G5d;
the full Windows product marker remains required.

Full Windows run `30107604684`, job `89528773330`, then passed the build,
trusted-leaf preflight, shipped product echo/DNS/H3/TCP path, control-close
oracle, and trust cleanup. It reached the final forwardproxy owner suite and
failed because legacy test fixtures assumed arbitrary `*.localhost` names
resolve on Windows; probe-resistance error handling also dereferenced a nil
response after the setup failure. This was a later, independent test-fixture
boundary, not a recurrence of the Winsock fix or a product-path failure.

Forwardproxy test-only commits `9b40eeb5cede209143bba47fce3b05060d7e1bce`
and `964281a9797efd9a4c953f6273c73e397e777864` now:

- dial local proxy sockets through `127.0.0.1` while preserving logical
  Host/SNI identities;
- use explicit loopback target identities that do not require wildcard DNS;
- wait for successful TLS handshakes instead of sleeping a fixed 500 ms; and
- fail probe-resistance setup errors directly rather than dereferencing nil.

Local `go test -count=1 ./...` and `go test -race -count=1 ./...` pass at the
new head. Native Windows fast run `30167351024`, job `89702525017`, passed the
complete owner suite in 2m17s and emitted:

```text
ok github.com/caddyserver/forwardproxy 3.740s
M6_G5D_WINDOWS_FORWARDPROXY_TESTS_OK
```

No production source changed. Current/future qualification scripts pin
`964281a9797efd9a4c953f6273c73e397e777864`; historical macOS/Linux records
retain the exact `f14924cd` revision they actually verified.

Full native Windows run `30167583501`, job `89703137849`, completed in 59m43s
on Microsoft Windows Server 2022 `10.0.20348.5386` x86_64 at NaiveProxy
`3ed7cbc3defa48010d82cfab57ae1870873eaef5`, forwardproxy
`964281a9797efd9a4c953f6273c73e397e777864`, and Caddy
`dd9a89c11194dcb806d845233995ef040f096464`. The Release client build took
52m35s, the pinned server build 1m42s, and the complete runtime gate 3m46s.
The redacted evidence includes default-verifier rejection, temporary
TrustedPeople acceptance and removal, UDP echo, DNS, independent HTTP/3,
forced-SOCKS TCP, control close, server/client idle reconnect, H3 DATAGRAM,
no-padding baseline, and the final forwardproxy server suite. Final markers:

```text
M6_G5D_WINDOWS_PRODUCT_OK
M6_G5D_WINDOWS_SHIPPED_CLIENT_OK
M6_G5D_WINDOWS_SERVER_OK
M6_G5D_WINDOWS_X64_OK
```

The machine-readable `windows-x64` row is now `verified`. Android arm64 real-device qualification (G5e) is also complete (`cfb42328ac`), so all
four G5 platform rows are verified.

### M6-G5f — cross-platform wire interoperability: complete

Commit `13df84bfd9` adds a pinned cross-platform gate. The macOS arm64
Chromium/Naive production backend connects over real QUIC/H3 to the exact
forwardproxy/Caddy server built as a Linux arm64 ELF and executed in a Lima
2.1.1 Alpine 3.23.3 VM. The VM image digest, Go/xcaddy/Caddy/forwardproxy
inputs, guest architecture, and client source ancestry are all checked before
traffic starts. This is wire-interoperability evidence, not a substitute for
the native Linux x64 or Android arm64 platform rows.

Verified markers:

```text
M6_G5F_UDP_OK
M5_G2_HTTP3_APPLICATION_OK
M6_G5F_LINUX_ARM64_SERVER_OK
M6_G5F_HTTP3_APPLICATION_OK
M6_G5F_TCP_OK
M6_G5F_PRIVACY_OK
M6_G5F_MACOS_CLIENT_LINUX_SERVER_OK
```

The gate uses only the test runner's local certificate verifier; default
certificate-verifier evidence remains owned by each native G5 platform row.
Temporary keys, logs, cross-built binaries, and guest processes are removed by
the runner. The final `M6_G5_PLATFORM_QUALIFICATION_OK` marker was withheld
until the Android arm64 record was verified; G5e closed that row, so the
marker is now recorded in the G5e section.

### M6-G6 — release-candidate closeout and independent audit: complete

Date: 2026-08-29 (Asia/Shanghai)

The release matrix runner `tests/m6/g6_release_matrix.sh` reproduces the
release from clean inputs in one pass against the exact release pins: client
runtime `474a1e4b0aeb9c64e6d0083eaddd205c887bf608` (HEAD `73a0afe80d` is
docs-only; `git diff 474a1e4b0a..HEAD -- src/net` is empty), forwardproxy
`964281a9` (audited runtime base `8f044e27`, build lock `e9663e4`), Caddy
`dd9a89c1` (rebuilt per run into the matrix tmp root by
`forwardproxy/scripts/build-naive-caddy.sh` with Go `1.25.12` / xcaddy
`v0.4.5`; `go version -m` on the binary is checked for `go1.25.12`), Go
`1.25.12`, quic-go `0.59.0`, gn `2407 (3357c4f51b1a)` at the DEPS pin.

Gate order: release pin checks -> 11 Release ninja targets (no-op against
the frozen tree) -> M1-M3 matrix -> 56-case Naive TCP owner matrix -> Caddy
RC rebuild + toolchain pin check -> pre-seeded isolated Go module cache
for the shipped-binary gate -> `tests/m5/g5_production_binary.sh` on the RC
pins -> three clean-root M5 product repetitions -> M6 G2 network-impairment
matrix (5 profiles x 3 clean-root runs) -> M6 G3 3600 s qualification soak
-> M6 G4 sanitizer/fuzz -> forwardproxy owner/legacy/privacy/race
regressions -> Caddy `modules/caddyhttp` regression -> hygiene (3x `git
diff --check`, forbidden-generated-artifact scans over the three M6 diff
ranges, untracked allow-list).

Harness fixes landed during G6 (test-only; no gate weakened):

- `tests/m6/g1_live_ceiling.sh` and `tests/m6/g1_shipped_ceiling.sh`:
  `expected_client` advanced from the G2-era pin `17c717793c` to the frozen
  RC `474a1e4b0a`. The five M6 runtime commits (`a5d33cb3f9`,
  `d6f95e9f61`, `cc2208ec87`, `2ec711012e`, `474a1e4b0a`) changed
  `src/net` after the G2-era pin, so the stale pin rejected the RC tree.
- The G6 runner pre-seeds a fresh per-run isolated Go module cache
  (`M6_GO_CACHE_ROOT`) before the G2/G3 stages build the h3-origin /
  socks-h3-probe fixtures, the same fix pattern as the G5 shipped-binary
  gate, protecting probes from a half-extracted shared module directory.
- The G6 runner hygiene stage filters `git status --porcelain` to untracked
  (`??`) lines before the allow-list comparison; modified tracked files (the
  G6 change itself) were previously counted as unexpected.

Matrix execution: the first G6 matrix run (`matrix-run6`, log retained under
`/tmp/g6/`) passed every test stage through server regressions on the
pre-fix runner; its final hygiene stage exposed the untracked-filter harness
bug above, not a product finding. The final run (`matrix-run7`) completed
every stage green on the fixed runner, exiting `0` with:

```text
M6_G6_PRODUCT_REPETITION_OK run=1
M6_G6_PRODUCT_REPETITION_OK run=2
M6_G6_PRODUCT_REPETITION_OK run=3
M6_G2_NETWORK_IMPAIRMENT_OK
M6_G3_STRESS_SOAK_OK
M6_G4_SANITIZER_FUZZ_OK
M6_G6_LOCAL_RELEASE_CHECKLIST_OK
```

plus the M1-M3 marker set (`G1_MASQUE_SMOKE_OK`, `G2_NAIVE_TUNNEL_OK`,
`G3_BASIC_AUTH_OK`, `G5_LIFECYCLE_OK`, `M2_SOCKS5_UDP_INGRESS_OK`,
`M3_NATIVE_UDP_CLIENT_OK`), 56/56 Naive TCP `TEST PASS` cases, the full
shipped-binary marker set including `M5_G5_PRODUCTION_BINARY_OK` and
`M5_G5_UNTRUSTED_CERT_REJECTED_OK`, all three clean-root repetitions, the
forwardproxy `M4_G0`-`M4_G5` server marker sets plus
`M6_H3_TCP_PADDING_INTEROP_OK`, `go test ./...` and `go test -race ./...`
ok, and Caddy `go test ./modules/caddyhttp` ok. The machine contract test
suite (`python3 tests/m6/contract_test.py`) passes 19/19.

G3 qualification soak (`M6_G3_TIER_OK tier=qualification
duration_seconds=3600`): 97,056 datagrams over 6,066 churn waves;
association cap 256 with 1 rejection and 64 reuses; resource peaks runner
RSS 20,064 KiB / fd 526 and caddy RSS 43,648 KiB / fd 12; resource
recovery verified; zero duplicate datagrams under every G2 profile
(`M6_G2_NO_REPLAY_OK` per profile).

G4 evidence: three seeded release codec fuzz runs of 1,000,000 iterations
(seeds `20260720`, `9298`, `1928`; valid corpus 44,027 / 43,374 / 43,459
entries) with `M6_G4_RELEASE_CODEC_FUZZ_OK`; 2,000-iteration seeded
lifecycle; ASan/UBSan build and run `M6_G4_ASAN_UBSAN_OK`; Go server fuzz
budget runs PASS; race suites clean; `M6_G4_SANITIZER_FUZZ_OK`.

Exact-pin artifact inventory (SHA-256, macOS arm64 Release tree; the four
contract-required release targets are marked `*`):

| Artifact | SHA-256 |
| --- | --- |
| `naive` `*` | `fb37dfc7f4132e751fb0057dcf6be366e5c79c90c1afa16c579609f2193d7973` |
| `naive_connect_udp_backend_test` `*` | `5715bb9c907ec33cded198ff23c5600af18c6eefd2ea9cfb99b049e0c9fab9ee` |
| `naive_connect_udp_runner` | `9a53ef5b3a6037efd98fd71061458a9d5ddcae09ebd4cc524b14081901a0e3b9` |
| `naive_socks5_udp_test` `*` | `9ce9cdd2c54c8fa647568012acb99b8a6879b3faa83a75cd74401c5007834f65` |
| `naive_socks5_udp_fuzz_test` | `c76fd3fbf9e6aa0938b16f0006939521669021994ff4daeeefabd5b6a7030cac` |
| `naive_socks5_udp_m3_runner` `*` | `50e0223df5df68459ec3f7fdc178352790fb4e337d7a19669f4ae4cba3471a72` |
| `naive_socks5_udp_runner` | `035f9e175a3079d5cff9190e63a645c6ae7cb50c4d43e55d451b21834eb2d7a2` |
| `naive_socks5_udp_association_test` | `d5e07760e6d07af8520072598d264a5c737867336be8fe266377d7f4a6d2d46a` |
| `naive_socks5_server_socket_state_test` | `f12407d3f1a81e67885554fedab857f0971543435cbea10d67eb5985b86e8e96` |
| `naive_masque_client` | `78dfd4d9d33d6d172e93408be24f6fecb1830c2147152043d3ad0909051ee186` |
| `naive_masque_probe` | `8f7ae94edd4e8dc2b28c35e37208b0178763fc523ec56e7280d7733d57f1ee58` |
| `naive_masque_server` (test fixture) | `772983e729b9c3d8f9b57090c1a7f8ea51c503905f413b5f5bee0cf8d88e7de0` |
| Caddy RC binary (per-run rebuild) | `28b638d12e612f82aaf12fbdef7302030ec5e67f73908944fa4055a9c6328bb6` |

The Android arm64 artifacts on the NAS were re-verified at the same SHAs as
the G5e record: `naive`
`e205d1b76de73416a752117ff26ead42cee1330ea9e3ace3d2090509b81c791b`, plugin
APK `aee57c2b26a76ae2881f0335d0075055e105d97b14136ff818fc745ec6f28e7a`.

Post-run cleanup verified: no test processes (caddy/masque/naive/echo/
shaper/probe), no test listener ports (8443, 8500-8503, 19661-19664), no
matrix tmp roots, and no G5 test CA remaining in the login keychain.

Maintainer approval (G6 item 4): the project owner approved the Chromium
API boundary (sole adapter `NaiveQuicProxyStreamRequest` over
`NaiveConnectUdpTunnel` + `ConnectViaStream()`, preemptive auth via
`HttpAuthController`, 407 as fresh-stream failure, pre-`Build()` QUIC
origins per `333b7cb253`, M6 hardening confined to Naive's UDP backend, TCP
path and `CertVerifier::CreateDefault()` untouched) and the frozen payload
policy (1200 B baseline, 1314 B host ceiling, drop-oversize without
truncation, no replay, no padding) on 2026-08-29.

Independent audit (G6 item 5): a non-interactive `agy` session (Gemini
3.1 Pro High, `agy --model gemini-3.1-pro-high -p`, permission prompts
disabled, 45-minute print timeout) performed a defensive release-quality,
read-only review over the exact M6 ranges: client
`eaf172d971..73a0afe80d` (M6 runtime commits `a5d33cb3f9`,
`d6f95e9f61`, `cc2208ec87`, `2ec711012e`, `474a1e4b0a`, plus platform
qualification and documentation commits), forwardproxy
`8f044e27..964281a9` (including build lock `e9663e4`), Caddy
`cce894a8..dd9a89c1` (scoped post-fix audit of the TLS module race fix
`dd9a89c1`), and the G6 harness change set (new G6 runner, M5
environment overrides, M6 ceiling pin advances). The reviewer
operated read-only, inspected the Git ranges, critical sources, and the
recorded evidence (matrix logs, markers, pins), and re-verified that the
M5 scripts' defaults remain the audited pins (no gate weakened).

Findings: blocker 0, high 0, medium 0, low 2.

- LOW (client): commit `17c717793c` adds a
  `notify_proxy_delegate_of_response` constructor parameter at
  `quic_session_pool.cc:1925`, a technical `QuicSessionPool` change that
  is purely an API adaptation: the parameter defaults to `true`, so all
  pre-existing streams keep today's behavior, and it is passed `false`
  only for UDP datagram streams so UDP responses cannot pollute the TCP
  padding capability cache. No security risk. Owner acknowledged with
  mitigation on 2026-08-29: TCP data path and padding behavior stay
  covered by the 56-case TCP owner matrix, `M6_H3_TCP_PADDING_INTEROP_OK`,
  and `M5_G5_NO_PADDING_BASELINE_OK`, all green in the G6 matrix.
- LOW (client, informational): the M6 UDP association hardening
  (transient relay error tolerance `d6f95e9f61`, zombie-target eviction
  `a5d33cb3f9`) introduces no vulnerability or replay vector; ambiguous
  datagrams are dropped without replay.

Verdict:

```text
AUDIT_PASS
Zero blocker, high, or medium findings.
```

M6-G6 exit: `M6_G6_LOCAL_RELEASE_CHECKLIST_OK` plus independent
`AUDIT_PASS` is recorded, and the final marker
`M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` closes M6.

## Canonical M5 verification commands

```bash
cd /path/to/naiveproxy
./tests/m5/g4_lifecycle_matrix.sh
./tests/m5/g5_production_binary.sh
./tests/socks5_udp_m5.sh
./tests/m5/g6_finalize.sh
```

`g4_lifecycle_matrix.sh` is non-privileged. The G5 and cumulative commands
install one short-lived user-domain root after an explicit macOS confirmation;
their traps remove the trust/certificate and verify the endpoint is untrusted
again. `M5_G5_STOP_AFTER_NEGATIVE=1` runs only the non-mutating negative path.

## Canonical server verification commands

```bash
cd /path/to/naive-forwardproxy-m4
GO_BIN=/path/to/naive-m4/go1.25.12/bin/go \
XCADDY_BIN=/path/to/naive-m4/bin/xcaddy \
CADDY_SOURCE_DIR=/path/to/caddy-naive-udp-m4 \
  ./scripts/build-naive-caddy.sh /tmp/naive-m4-caddy
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  ./scripts/test-m4.sh
GO_BIN=/path/to/naive-m4/go1.25.12/bin/go \
CADDY_BIN=/tmp/naive-m4-caddy \
  ./scripts/test-m4-g5-server.sh
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  go test -race ./...

cd /path/to/caddy-naive-udp-m4
PATH=/path/to/naive-m4/go1.25.12/bin:$PATH \
  go test ./modules/caddyhttp
```

## Canonical client verification commands

```bash
cd src
ninja -C out/Release naive naive_masque_server naive_masque_client \
  naive_masque_probe naive_connect_udp_runner naive_socks5_udp_test \
  naive_socks5_server_socket_state_test naive_socks5_udp_association_test \
  naive_socks5_udp_runner naive_connect_udp_backend_test \
  naive_socks5_udp_m3_runner
../tests/masque_g1_smoke.sh
../tests/masque_g2_naive_tunnel.sh
../tests/masque_g3_basic_auth.sh
../tests/masque_g5_lifecycle.sh
../tests/socks5_udp_m2.sh
../tests/socks5_udp_m3.sh
../tests/basic.sh out/Release/naive
git diff --check
```

Expected markers:

- `G1_MASQUE_SMOKE_OK`
- `G2_NAIVE_TUNNEL_OK`
- `G3_BASIC_AUTH_OK`
- `G5_LIFECYCLE_OK`
- `M2_G1_CODEC_OK`
- `M2_G2_DETERMINISTIC_STATE_MACHINE_OK`
- `M2_G4_G5_UDP_ASSOCIATION_OK`
- `M2_G2_AUTHENTICATED_UDP_OK`
- `M2_G3_NO_BACKEND_REJECTION_OK`
- `M2_SOCKS5_UDP_INGRESS_OK`
- `M3_G0_BACKEND_CONTRACT_OK`
- `M3_G0_TEST_SKELETON_OK`
- `M3_G1_SINGLE_TARGET_OK`
- `M3_G2_MULTI_TARGET_LIMITS_OK`
- `M3_G2_FAILURE_ISOLATION_OK`
- `M3_G2_ACTIVE_ASSOCIATION_LIMIT_OK`
- `M3_G3_DIRECT_REJECTION_OK`
- `M3_G3_H2_REJECTION_OK`
- `M3_G3_MIXED_CHAIN_REJECTION_OK`
- `M3_G3_NO_BACKEND_REJECTION_OK`
- `M3_G3_IPV4_ECHO_OK`
- `M3_G3_AUTH_ECHO_OK`
- `M3_G3_PRODUCTION_WIRING_OK`
- `M3_G4_IPV4_OK`
- `M3_G4_IPV6_OK`
- `M3_G4_DOMAIN_OK`
- `M3_G4_DNS_OK`
- `M3_G4_AUTH_OK`
- `M3_G4_MULTI_TARGET_OK`
- `M3_G4_CONCURRENT_ASSOCIATIONS_OK`
- `M3_G4_NETLOG_REDACTION_OK`
- `M3_G5_DETERMINISTIC_LIFECYCLE_OK`
- `M3_G5_SESSION_RECONNECT_OK`
- `M3_G5_IDLE_RECONNECT_OK`
- `M3_G5_ZERO_OVERSIZE_OK`
- `M3_G5_BACKEND_DESTRUCTION_OK`
- `M3_G5_PENDING_CONNECT_CLOSE_OK`
- `M3_G5_CONNECT_TIMEOUT_OK`
- `M3_G5_AUTH_MISSING_OK`
- `M3_G5_AUTH_WRONG_OK`
- `M3_G5_AUTH_FAILURES_OK`
- `M3_G5_RECONNECT_OK`
- `M3_G5_LIFECYCLE_OK`
- `M3_G5_LIMITS_OK`
- `M3_NATIVE_UDP_CLIENT_OK`
- exit code `0` from `tests/basic.sh`
- `ninja: no work to do` or a successful link

The final M3 local verification on 2026-07-19 passed every named M1/M2/M3
target and script, all 56 existing TCP HTTP/HTTPS/auth/chain cases, three
consecutive M3 lifecycle stress runs, and `git diff --check`. The independent
M3 `agy` audit reran the required matrix and returned `AUDIT_PASS`; see
`docs/m3-agy-audit.md`.

Historical M1 and M2 independent audits also returned `AUDIT_PASS` with no
blocking, high, or medium findings. Their audit histories and deferred
low-priority observations remain in `docs/m1-agy-audit.md` and
`docs/m2-agy-audit.md`.

## Frozen boundaries

- TCP behavior must remain unchanged.
- UDP v1 is allowed only over `quic://` / HTTP/3.
- Native UDP means RFC 9298 CONNECT-UDP plus HTTP/3 DATAGRAM, never a custom
  UDP-over-stream fallback.
- The QUICHE endpoint is an M1 interoperability fixture, not the production
  server architecture.
- M2's fake backend is test-only. Production uses the audited M3 adapter and
  target backend to reach the real M1 CONNECT-UDP tunnel.
- M5 deterministic runs may use the M3 runner's test-only certificate verifier,
  but milestone completion requires a separate shipped-`naive` smoke through
  the default certificate verifier. Do not add a production bypass.
- M5 application evidence requires an independent SOCKS5-UDP-backed HTTP/3
  client; generic UDP echo does not satisfy that gate by itself.
- UDP padding remains intentionally out of v1 until traffic-shape measurements
  justify a separate unreliable-datagram design.

## Working-tree state

M1 has been committed as `e11a7733` (`Complete native UDP M1 foundation`) on
`codex/native-udp-foundation`. M2 has been committed as `fe817a87` (`Complete
SOCKS5 UDP ingress M2`) after all local gates and the independent audit passed.
The reviewed M3 execution plan is committed as `8720c912` (`Plan native UDP M3
execution`). G0, G1, and G2 are committed as `83904eb8`, `4541f756`, and
`1bd5789e`; G3 production composition is committed as `c2352710`. G4
interoperability and the post-G3 privacy/lifecycle audit fixes are committed as
`4927d06a`. G5 lifecycle/recovery/limits work is complete and verified in
`578e3992`. G6 regressions, stress verification, and independent audit
closeout is commit `2bb83aec`. M4-G0–G5 are committed in the separate server
fork through `7243519`; the post-audit CI-pin closure is `8f044e2`. Caddy's
three audited patches end at `cce894a8`. M4's local verification and audit
evidence are recorded in `4ec0f8bb9a`. The M5 G0–G6 execution plan now exists;
M5-G0 is commit `014cdc4761`; M5-G1 is main commit `67caa8f131` plus the
forwardproxy fixture commits `d922441` and `88ac298`. M5-G2 is commit
`eeccb7cb30`; M5-G3 is `fbdd8af531`. The shipped-client QUIC context ordering
fix is `333b7cb253`; M5-G4/G5 harness and evidence are `c73b5a486f`; the G6
runner is `4a395a7f4e` with module-cwd correction `d1aee3663f`; the independent
audit covers the local closeout `eaf172d971` and returns `AUDIT_PASS`.
Generated `.DS_Store` and `src/tmp/` entries remain unrelated and must not be
included in future feature commits.
M6-G6 closes the branch with harness commit `2430df7acc` (release matrix
plus harness pin fixes) and closeout record `e96cfbd0cc` (status ledger,
release guide, payload policy), both pushed to origin.

## Historical post-M7 CONNECT-UDP admission tuning — superseded deployment

On 2026-09-03 CST, the production forwardproxy admission caps were raised to
accommodate the router's single sing-box UDP fan-out: handler-wide active
associations from 256 to 512, and per-source-public-IP active associations
from 32 to 128. The narrow forwardproxy change is commit `25b4cd6`; its full
`go test ./...` suite passed with Go 1.25.12 in the isolated build tree.

The deployed Caddy binary was rebuilt with the locked M7 inputs (Caddy
`3bcce47`, quic-go `f84ad47630af`, Go 1.25.12, xcaddy 0.4.5) and installed as
`/var/lib/proxy-private/caddy-naive-udp`, SHA256
`fe6de99fee5d3502644cc7c4d0944318e4b78bedbf78ff47158261e1ccef9ad9`.
`native-udp-caddy.service` restarted successfully with `NRestarts=0` and
8443/8444 listeners intact. The previous binary remains at
`/var/lib/proxy-private/caddy-naive-udp.pre-limit-20260902-2345` for rollback.
This binary and rollback statement were superseded by the standard artifact
deployment recorded in [`current-deployment.md`](current-deployment.md).
