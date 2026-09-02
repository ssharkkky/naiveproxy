# Product Release SOP

This SOP publishes a matching native-UDP client and server. It applies to the
NaiveProxy client, the `ssharkkky/forwardproxy` server module, the patched
`ssharkkky/caddy`, and the `ssharkkky/quic-go` BBR fork.

## Source of truth

`release/product.lock.json` records the four immutable source commits, the
server toolchain, the product version, and the CUBIC defaults. A release is
valid only when the lock is committed before the build starts. No workflow may
use `latest` or an unpinned branch for a release artifact.

The forwardproxy repository contains the corresponding `M7_TOOLCHAIN.lock` and
`scripts/build-m7-caddy.sh`. The product workflow checks out those exact pins
and verifies the resulting module graph and Caddy modules.

## Normal release flow

1. Merge code and dependency changes into their owning repository only after
   that repository's tests pass.
2. Update `release/product.lock.json` with full commit SHAs and set the channel
   to `experimental`, `rc`, or `stable`.
3. Run `scripts/verify-product-lock.sh` and the product combination workflow.
4. Create a release candidate tag. The existing client workflow builds and
   uploads the Linux, Windows, macOS, Android, and OpenWrt client artifacts;
   `product-server-release.yml` builds and uploads the pinned Caddy bundle.
5. Review the generated manifest, SHA256 files, Go module provenance, and test
   markers. Promote the same candidate to stable without rebuilding it.

Push builds are CI artifacts only. A GitHub Release is the only source of
official prebuilt packages.

## Dependency updates

Update one owner at a time and do not merge automatically:

- **NaiveProxy/Chromium:** run the full client matrix, 56 TCP cases, and the
  native-UDP interoperability probes. Re-run the server combination tests.
- **quic-go:** rebase the fork from the new upstream tag, replay the BBR patch,
  run `go test ./...`, `go vet ./...`, and race tests, then update Caddy's pin.
- **Caddy:** rebase the H3 Datagram/BBR patch, check every production
  `quic.Config` constructor, and run Caddy plus forwardproxy tests.
- **forwardproxy:** keep Caddy and quic-go fixed, run legacy/privacy/race and
  native-UDP tests, then update the server lock.

After each update, regenerate the product lock and run the complete combination
workflow. If a QUIC API, wire behavior, default, or privacy boundary changes,
stop and open a compatibility review rather than forcing the update through.

## Release gates

A stable artifact requires:

- exact four-repository provenance and reproducible build checks;
- CUBIC default regressions green;
- BBR TCP and UDP reference-path checks green;
- native-UDP echo, DNS, and independent HTTP/3 application probe green;
- forwardproxy legacy, privacy, and race suites green;
- SHA256, SBOM, and build provenance attached to the release.

Until M7 G5 and its audit are complete, publish only `experimental` or `rc`
artifacts. The current M7 G4 result is not a stable-release qualification.

## Rollback

For a runtime rollback, set `NAIVE_QUIC_CONGESTION=cubic` and restart the
server. For a binary rollback, restore the previous product lock and its
recorded client/server artifacts. Keep the last verified CUBIC candidate
available at all times.
