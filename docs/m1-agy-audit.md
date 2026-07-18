# M1 Independent `agy` Audit Record

Date: 2026-07-18 (Asia/Shanghai)

Scope: the complete uncommitted M1 native-UDP integration spike, including
Chromium seams, Naive-owned tunnel/request adapters, controlled QUICHE tools,
G1/G2/G3/G5 automation, NetLog evidence, documentation, and TCP regression
risk. Production SOCKS5 UDP ingress was explicitly out of scope because it is
M2 work.

The reviewer ran in one continuing `agy` conversation with permission prompts
disabled. It inspected the diff and independently executed:

```text
ninja: naive, naive_masque_server, naive_masque_client,
       naive_masque_probe, naive_connect_udp_runner
G1_MASQUE_SMOKE_OK
G2_NAIVE_TUNNEL_OK
G3_BASIC_AUTH_OK
G5_LIFECYCLE_OK
56 existing TCP HTTP/HTTPS/auth/chain cases
git diff --check
```

## Review history

The first review returned `AUDIT_FAIL` based on a proposed bracketed IPv6
`target_host`. Reproduction showed that change caused the controlled endpoint
to return HTTP 400. RFC 9298 section 3 defines the template variable as an
`IPv6address` and requires percent-encoding its colons without adding
authority-style brackets. The implementation retained the correct
`%3A%3A1` target path, strengthened IPv6 proxy-authority construction, and
added a real IPv6 proxy plus IPv6 UDP echo test.

The same reviewer conversation re-read the normative requirement, inspected
the corrected diff and documentation, and reran the complete suite. It
withdrew the finding and returned:

```text
AUDIT_PASS
No remaining blocking, high, or medium severity findings.
```

## Deferred low-priority observations

- The four standalone MASQUE scripts use distinct default ports with
  environment overrides. Dynamic port allocation should be considered before
  parallel CI execution.
- M2/M3 must use the same empty/transient `NetworkAnonymizationKey` for auth
  cache preload and tunnel creation if partitioning behavior changes.

Neither observation blocks the M1 integration-spike exit criteria.
