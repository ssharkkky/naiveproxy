# Native UDP v1 Release Guide

Last updated: 2026-07-20 (Asia/Shanghai)

Status: **release-candidate draft**. M6 is incomplete. This guide describes the
intended v1 operator contract but must not be used to claim production support
until `M6_NATIVE_UDP_RELEASE_CANDIDATE_OK` and the required platform record are
present.

## Configuration contract

Native UDP is selected by a SOCKS5 client issuing `UDP ASSOCIATE` to a Naive
SOCKS listener. Existing SOCKS5 `CONNECT`, HTTP proxying, redirects, TCP data
movement, and TCP padding retain their existing paths.

The complete selected upstream chain must use `quic://`. A non-QUIC or mixed
chain rejects `UDP ASSOCIATE` during the SOCKS handshake with reply `0x01`
(general SOCKS server failure). Native UDP does not silently fall back to UoT,
HTTP/2, or a reliable stream.

The server must be the pinned Caddy/forwardproxy build with HTTP/3 Datagram and
RFC 9298 CONNECT-UDP enabled. Client and server authentication, ACL, allowed
ports, and DNS/IP policy remain active for UDP targets.

## Compatibility boundary

| Item | Native UDP v1 contract |
| --- | --- |
| Local ingress | SOCKS5 `UDP ASSOCIATE` with compliant relay BND.ADDR/BND.PORT |
| Upstream | all-`quic://` Naive chain only |
| Wire protocol | RFC 9298 CONNECT-UDP + HTTP/3 DATAGRAM, Context ID `0` |
| Destinations | IPv4, IPv6, and domain names |
| Fragmentation | SOCKS `FRAG != 0` rejected; no reassembly |
| Reliability | unreliable datagrams; no automatic replay |
| Recommended payload | 1200-byte inner payload baseline; see the payload policy |
| UDP padding | not implemented in v1 |
| TCP behavior | unchanged existing Naive path and padding |

Platform support is attributable, not inferred. Consult
`tests/m6/platform_qualification.json` in the exact release revision. A row
that is `not run`, `blocked`, or `failed` is not supported by the M6 release
claim, even if another platform passes.

## Certificate and authentication behavior

The shipped client uses `CertVerifier::CreateDefault()`. Test-only mock
verifiers and certificate-bypass switches are not present in the production
binary. Production deployments must use a certificate trusted by the target
platform's normal verifier.

Configured proxy credentials may be sent preemptively from Chromium's auth
cache. A failed 407 does not restart the same CONNECT-UDP object; a new tunnel
attempt is required. Logs must not retain authorization values or recoverable
encodings.

## Payload and PMTU behavior

Follow [`native-udp-payload-policy.md`](native-udp-payload-policy.md). The v1
application baseline is 1200 bytes only after that document changes from
`candidate` to frozen. The measured 1314-byte current-host ceiling is not a
cross-platform guarantee.

Payloads above the live HTTP/3 Datagram ceiling are dropped locally without
truncation. Payloads below that ceiling can still be lost by the network. A
datagram with an ambiguous send/session outcome is not replayed.

## Observability and privacy

Permitted operational evidence is aggregate and redacted:

- association ID, state transition, close reason, and relative time;
- direction, packet size, connection age, and aggregate count;
- queue/capacity/oversize/transport error classes;
- process RSS, file-descriptor/handle count, and platform/toolchain version.

Do not log payloads, target hosts/ports, RFC 9298 request paths, credentials,
certificate private keys, packet captures, or decrypted traffic. Default
NetLog may be used for the audited markers; `--net-log-everything` can expose
sensitive configuration and is not release evidence.

## Troubleshooting

### UDP ASSOCIATE returns `0x01`

Verify that every selected proxy hop is `quic://`, that native UDP is enabled
in the exact release binary, and that the active-association cap has not been
reached. The v1 implementation deliberately rejects HTTP/2 and mixed chains.

### SOCKS association succeeds but no UDP response arrives

Check certificate trust, proxy authentication, ACL/allowed-port policy, target
reachability, and path MTU. UDP loss is not automatically converted to a
reliable stream. Retry at the application layer with a fresh datagram and do
not assume an earlier ambiguous packet was delivered.

### HTTP/3/QUIC application traffic fails while small echo succeeds

Confirm the application's datagram size stays within its chosen PMTU bound,
DNS and SNI select the expected target, and the independent HTTP/3 application
probe passes through the same SOCKS relay. Generic echo alone is insufficient
application evidence.

### Capacity or idle behavior

The client has bounded association, target, queue, and queued-byte limits.
Idle targets and server associations expire and may create a new CONNECT-UDP
association on later traffic. Persistent monotonic RSS/FD/handle growth is a
release-blocking defect, not expected behavior.

## Upgrade procedure

1. Record the current client/server binaries, configuration, and dependency
   revisions before changing them.
2. Verify the release's NaiveProxy, forwardproxy, Caddy, Go, xcaddy, and
   quic-go pins against the M6 status ledger.
3. Upgrade a client/server canary pair first. Confirm TCP parity, untrusted and
   trusted certificate behavior, UDP echo, DNS, and an independent HTTP/3
   application.
4. Confirm redacted lifecycle/oversize counters and the 1200-byte policy on the
   target platform before broader rollout.
5. Preserve the previous binary pair until the soak and platform-specific
   runtime checks remain healthy.

## Rollback procedure

Native UDP has no private backward-compatible fallback. Roll back the client
and Caddy/forwardproxy as a tested pair to the previous pinned revisions, or
route clients through a non-QUIC upstream so UDP ASSOCIATE fails explicitly
while ordinary TCP behavior remains available. Do not introduce UoT or disable
certificate verification as an emergency workaround.

After rollback, verify:

- ordinary TCP SOCKS/HTTP behavior;
- UDP ASSOCIATE is either provided by the selected known-good pair or rejected
  with `0x01`;
- no temporary CA, test process, credential, private key, NetLog, qlog, or
  packet capture remains; and
- the prior configuration and dependency pins match the rollback record.

## Known v1 limitations

- no UDP over HTTP/2, mixed proxy chains, TUN, CONNECT-IP, VPN mode,
  CONNECT-UDP-LISTEN, or SOCKS fragmentation;
- no UDP payload padding or application behavior shaping;
- no transparent replay/reliability layer;
- current 1314-byte live-ceiling evidence is host/path specific;
- interactive 407 recovery requires a fresh tunnel attempt;
- platform support remains limited to rows independently verified in the M6
  platform record.

## Release evidence

The operational source of truth is [`native-udp-status.md`](native-udp-status.md).
The release is not complete until all required G1-G6 markers, platform records,
clean rebuilds, inherited TCP/server regressions, artifact cleanup, and the
independent audit are recorded there.
