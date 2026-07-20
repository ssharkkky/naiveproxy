# Native UDP v1 Payload and PMTU Policy

Last updated: 2026-07-20 (Asia/Shanghai)

Status: **candidate**. The policy is derived from completed M6-G1a/G1b1/G1c
measurements, but it is not frozen for release until the shipped-client G1b2
gate and three-run G1d closeout pass.

## Application contract

- Applications should keep each SOCKS5 UDP payload at or below **1200 bytes**
  when they require the native-UDP v1 release baseline.
- The 1200-byte value is the inner application payload. It excludes the SOCKS5
  UDP header, HTTP/3 Datagram Context ID, QUIC framing, UDP, and IP headers.
- An empty UDP payload is valid and must remain distinguishable from EOF.
- Native UDP is unreliable. Delivery, ordering, and duplication handling remain
  application responsibilities even below 1200 bytes.

The 1200-byte baseline is chosen because current black-box evidence shows that
it produces an outer QUIC UDP payload below 1232 bytes, the IPv6 minimum PMTU
of 1280 minus the 40-byte IPv6 and 8-byte UDP headers. On the qualified macOS
path, a 1200-byte inner payload produced a 1225-byte outer packet and survived
the lowered 1232-byte ceiling.

## Values above the baseline

The current macOS production-backend measurement is 1314 bytes for IPv4, IPv6,
and domain targets. This is a live HTTP/3 Datagram ceiling observation, not a
portable application guarantee.

- Payloads from 1201 bytes through the tunnel's current live ceiling are
  best-effort. They may succeed on a path with sufficient PMTU and may be lost
  on a smaller or black-holed path.
- Payloads above the current live HTTP/3 Datagram ceiling are dropped locally
  before tunnel `Write()`. They are never silently truncated.
- A later live-ceiling reduction applies to each subsequent write. Restoring
  the ceiling permits new traffic; an ambiguous earlier datagram is not
  replayed.

Applications that need larger logical messages must supply their own
application-level segmentation and recovery while keeping each UDP payload
within their chosen bound. Native UDP v1 does not implement SOCKS fragmentation
or private reassembly.

## Required transport behavior

The release implementation must preserve all of the following:

- RFC 9298 CONNECT-UDP over HTTP/3 DATAGRAM with Context ID `0`;
- no DATA/Capsule fallback, UDP-over-stream, UoT, or second QUIC stack;
- no IP or SOCKS fragmentation/reassembly added by Flux/Naive code;
- no automatic retry or replay after an ambiguous datagram write/session
  failure;
- no cross-target or cross-association delivery;
- healthy traffic on the same and unrelated associations can recover after an
  oversize drop or path restoration.

## Observability and privacy

Oversize and transport failures may increment redacted, rate-limited counters
and NetLog state/error events. Logs and retained evidence must not contain UDP
payloads, destination hosts/ports, RFC 9298 request paths, credentials, or
recoverable encodings of those values.

UDP payload padding remains out of native UDP v1. The existing Naive TCP
padding protocol is not applied to HTTP/3 DATAGRAM payloads, and adding an
unreliable-datagram padding protocol requires a separate design and audit.

## Freeze criteria

This document becomes the frozen v1 release policy only after:

1. shipped `naive` with `CertVerifier::CreateDefault()` repeats the 1314-byte
   current-host measurement and rejects 1315 bytes without truncation;
2. temporary user-domain CA trust is removed and the certificate is verified
   untrusted again;
3. the complete G1 ceiling/PMTU/client/TCP/server regression set passes three
   times; and
4. each G5 platform either verifies this 1200-byte baseline or remains
   explicitly unqualified/disabled for native UDP.

The final G1 marker is `M6_G1_PAYLOAD_PMTU_OK`.
