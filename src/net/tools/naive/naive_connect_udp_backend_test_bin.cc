// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/log/net_log_with_source.h"
#include "net/tools/naive/naive_connect_udp_datagram_backend.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive_connect_udp_backend_test", "");

struct ScriptedTunnelState {
  int start_result = net::ERR_IO_PENDING;
  int read_result = net::ERR_IO_PENDING;
  int write_result = net::ERR_IO_PENDING;
  size_t max_payload_size = 1200;
  bool open = false;
  bool last_read_was_datagram = false;
  int destructions = 0;
  int cancelled_callbacks = 0;
  std::vector<uint8_t> written;
  std::vector<uint8_t> read_payload;
  scoped_refptr<net::IOBuffer> read_buffer;
  int read_buffer_length = 0;
  scoped_refptr<net::IOBuffer> write_buffer;
  int write_buffer_length = 0;
  net::CompletionOnceCallback start_callback;
  net::CompletionOnceCallback read_callback;
  net::CompletionOnceCallback write_callback;
};

class ScriptedTunnel final : public net::NaiveConnectUdpTargetTunnel {
 public:
  explicit ScriptedTunnel(std::shared_ptr<ScriptedTunnelState> state)
      : state_(std::move(state)) {}

  ~ScriptedTunnel() override {
    for (net::CompletionOnceCallback* callback :
         {&state_->start_callback, &state_->read_callback,
          &state_->write_callback}) {
      if (*callback) {
        ++state_->cancelled_callbacks;
        callback->Reset();
      }
    }
    state_->read_buffer.reset();
    state_->write_buffer.reset();
    ++state_->destructions;
  }

  int Start(net::CompletionOnceCallback callback) override {
    if (state_->start_result == net::ERR_IO_PENDING) {
      state_->start_callback = std::move(callback);
      return net::ERR_IO_PENDING;
    }
    state_->open = state_->start_result == net::OK;
    return state_->start_result;
  }

  int Read(net::IOBuffer* buffer,
           int buffer_length,
           net::CompletionOnceCallback callback) override {
    if (state_->read_result == net::ERR_IO_PENDING) {
      state_->read_buffer = base::WrapRefCounted(buffer);
      state_->read_buffer_length = buffer_length;
      state_->read_callback = std::move(callback);
    } else if (state_->read_result >= 0) {
      CHECK_LE(state_->read_payload.size(),
               static_cast<size_t>(buffer_length));
      if (!state_->read_payload.empty()) {
        buffer->first(state_->read_payload.size())
            .copy_from(state_->read_payload);
      }
    }
    return state_->read_result;
  }

  int Write(net::IOBuffer* buffer,
            int buffer_length,
            net::CompletionOnceCallback callback) override {
    state_->written.assign(buffer->span().begin(),
                           buffer->span().begin() + buffer_length);
    if (state_->write_result == net::ERR_IO_PENDING) {
      state_->write_buffer = base::WrapRefCounted(buffer);
      state_->write_buffer_length = buffer_length;
      state_->write_callback = std::move(callback);
    }
    return state_->write_result;
  }

  bool IsOpen() const override { return state_->open; }
  bool LastReadWasDatagram() const override {
    return state_->last_read_was_datagram;
  }
  size_t MaxPayloadSize() const override { return state_->max_payload_size; }

 private:
  const std::shared_ptr<ScriptedTunnelState> state_;
};

void CompleteStart(const std::shared_ptr<ScriptedTunnelState>& state,
                   int result) {
  CHECK(state->start_callback);
  state->open = result == net::OK;
  std::move(state->start_callback).Run(result);
}

void CompleteWrite(const std::shared_ptr<ScriptedTunnelState>& state,
                   int result) {
  CHECK(state->write_callback);
  state->write_buffer.reset();
  state->write_buffer_length = 0;
  std::move(state->write_callback).Run(result);
}

void CompleteRead(const std::shared_ptr<ScriptedTunnelState>& state,
                  std::vector<uint8_t> payload,
                  bool was_datagram = true) {
  CHECK(state->read_callback);
  CHECK_LE(payload.size(), static_cast<size_t>(state->read_buffer_length));
  if (!payload.empty()) {
    state->read_buffer->first(payload.size()).copy_from(payload);
  }
  state->last_read_was_datagram = was_datagram;
  state->read_buffer.reset();
  state->read_buffer_length = 0;
  std::move(state->read_callback).Run(static_cast<int>(payload.size()));
}

int failures = 0;

void Expect(bool condition, std::string_view description) {
  if (!condition) {
    std::cerr << "FAILED: " << description << "\n";
    ++failures;
  }
}

void RunUntilIdle() {
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();
}

uint64_t NextLifecycleRandom(uint64_t* state) {
  *state ^= *state << 13;
  *state ^= *state >> 7;
  *state ^= *state << 17;
  return *state;
}

net::Socks5UdpBackendContext MakeContext(
    unsigned int association_id = 31,
    base::TimeDelta connect_timeout = base::Seconds(10),
    base::TimeDelta target_idle_timeout = base::Seconds(30)) {
  const net::ProxyChain chain = net::ProxyChain::FromSchemeHostAndPort(
      net::ProxyServer::SCHEME_QUIC, "proxy.test", 443);
  CHECK(chain.IsValid());
  return net::Socks5UdpBackendContext(
      association_id, reinterpret_cast<net::HttpNetworkSession*>(0x1), chain,
      net::NetworkAnonymizationKey::CreateTransient(),
      net::NetLogWithSource(), kTrafficAnnotation, connect_timeout,
      target_idle_timeout);
}

net::Socks5UdpDatagram DatagramFor(net::Socks5UdpEndpoint endpoint,
                                   std::vector<uint8_t> payload) {
  return net::Socks5UdpDatagram{
      .destination = std::move(endpoint),
      .payload = std::move(payload),
  };
}

net::Socks5UdpDatagram Datagram(std::vector<uint8_t> payload) {
  return DatagramFor(net::Socks5UdpEndpoint{
                         .type = net::Socks5UdpAddressType::kDomain,
                         .host = "target.test",
                         .port = 443,
                     },
                     std::move(payload));
}

void CompleteReadError(const std::shared_ptr<ScriptedTunnelState>& state,
                       int result) {
  CHECK_LT(result, 0);
  CHECK(state->read_callback);
  state->open = false;
  state->last_read_was_datagram = false;
  state->read_buffer.reset();
  state->read_buffer_length = 0;
  std::move(state->read_callback).Run(result);
}

void TestFrozenContracts() {
  static_assert(net::Socks5UdpBackendLimits::kMaxTargets == 32);
  static_assert(
      net::Socks5UdpBackendLimits::kMaxQueuedDatagramsPerTarget == 16);
  static_assert(
      net::Socks5UdpBackendLimits::kMaxQueuedDatagramsPerAssociation == 128);
  static_assert(
      net::Socks5UdpBackendLimits::kMaxQueuedPayloadBytesPerAssociation ==
      256 * 1024);
  static_assert(
      net::Socks5UdpBackendLimits::kMaxSynchronousPumpOperations == 32);
  static_assert(
      net::Socks5UdpBackendLimits::kMaxActiveAssociationsPerProxy == 256);
  static_assert(net::Socks5UdpBackendLimits::kDefaultConnectTimeout ==
                base::Seconds(10));
  static_assert(net::Socks5UdpBackendLimits::kFailedTargetCooldown ==
                base::Seconds(1));

  const net::Socks5UdpEndpoint domain{
      .type = net::Socks5UdpAddressType::kDomain,
      .host = "192.0.2.1",
      .port = 443,
  };
  const net::Socks5UdpEndpoint address{
      .type = net::Socks5UdpAddressType::kIpv4,
      .host = "192.0.2.1",
      .port = 443,
  };
  Expect(!(net::Socks5UdpTargetKey(domain) ==
           net::Socks5UdpTargetKey(address)),
         "wire address type is part of target identity");
}

void TestImmutableContextAndSkeleton() {
  const net::ProxyChain chain = net::ProxyChain::FromSchemeHostAndPort(
      net::ProxyServer::SCHEME_QUIC, "proxy.test", 443);
  CHECK(chain.IsValid());
  const net::NetworkAnonymizationKey nak =
      net::NetworkAnonymizationKey::CreateTransient();
  const net::NetLogWithSource net_log;
  auto* fake_session = reinterpret_cast<net::HttpNetworkSession*>(0x1);
  net::Socks5UdpBackendContext context(
      17, fake_session, chain, nak, net_log, kTrafficAnnotation,
      base::Seconds(7), base::Seconds(23));
  auto tunnel_state = std::make_shared<ScriptedTunnelState>();
  tunnel_state->start_result = net::OK;
  tunnel_state->write_result = 1;
  bool exact_context_seen = false;
  net::NaiveConnectUdpDatagramBackend backend(
      context,
      base::BindRepeating(
          [](const net::NetworkAnonymizationKey* expected_nak,
             bool* exact_context_seen,
             std::shared_ptr<ScriptedTunnelState> tunnel_state,
             const net::Socks5UdpBackendContext& actual_context,
             const net::Socks5UdpEndpoint&)
                       -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
                     *exact_context_seen =
                         actual_context.network_anonymization_key ==
                         *expected_nak;
                     return std::make_unique<ScriptedTunnel>(
                         std::move(tunnel_state));
                   },
          &nak, &exact_context_seen, tunnel_state));
  backend.Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  const int result = backend.Send(
      net::Socks5UdpDatagram{
          .destination = net::Socks5UdpEndpoint{
              .type = net::Socks5UdpAddressType::kDomain,
              .host = "target.test",
              .port = 53,
          },
          .payload = {0x01},
      },
      net::CompletionOnceCallback());
  Expect(result == net::OK, "G0 skeleton honors synchronous admission");
  Expect(exact_context_seen,
         "scripted tunnel factory receives the exact association NAK");
  Expect(context.association_id == 17, "context retains association id");
  Expect(context.session == fake_session, "context retains session pointer");
  Expect(context.proxy_chain == chain, "context retains proxy chain");
  Expect(context.network_anonymization_key == nak,
         "context retains exact NAK");
  Expect(context.net_log.source() == net_log.source(),
         "context retains NetLog source");
  Expect(context.traffic_annotation == kTrafficAnnotation,
         "context retains traffic annotation");
  Expect(context.connect_timeout == base::Seconds(7),
         "context retains connect timeout");
  Expect(context.target_idle_timeout == base::Seconds(23),
         "context retains target idle timeout");
}

void TestScriptedTunnelSeam() {
  auto state = std::make_shared<ScriptedTunnelState>();
  auto tunnel = std::make_unique<ScriptedTunnel>(state);
  int start_calls = 0;
  int start_result = net::ERR_UNEXPECTED;
  Expect(tunnel->Start(base::BindOnce(
             [](int* calls, int* observed_result, int result) {
               ++*calls;
               *observed_result = result;
             },
             &start_calls, &start_result)) == net::ERR_IO_PENDING,
         "scripted start can pend");
  state->open = true;
  std::move(state->start_callback).Run(net::OK);
  Expect(start_calls == 1 && start_result == net::OK && tunnel->IsOpen(),
         "scripted async start completion is observable");

  auto write_buffer = base::MakeRefCounted<net::IOBufferWithSize>(3);
  constexpr std::array<uint8_t, 3> kPayload = {'u', 'd', 'p'};
  write_buffer->span().copy_from(kPayload);
  int write_calls = 0;
  Expect(tunnel->Write(
             write_buffer.get(), 3,
             base::BindOnce([](int* calls, int) { ++*calls; },
                            &write_calls)) == net::ERR_IO_PENDING,
         "scripted write can pend");
  Expect(state->written == std::vector<uint8_t>({'u', 'd', 'p'}),
         "scripted write captures exact payload");
  std::move(state->write_callback).Run(3);
  Expect(write_calls == 1, "scripted async write completes once");

  auto read_buffer = base::MakeRefCounted<net::IOBufferWithSize>(8);
  int read_calls = 0;
  int read_result = net::ERR_UNEXPECTED;
  Expect(tunnel->Read(
             read_buffer.get(), read_buffer->size(),
             base::BindOnce(
                 [](int* calls, int* observed_result, int result) {
                   ++*calls;
                   *observed_result = result;
                 },
                 &read_calls, &read_result)) == net::ERR_IO_PENDING,
         "scripted read can pend");
  state->last_read_was_datagram = true;
  state->read_buffer.reset();
  state->read_buffer_length = 0;
  std::move(state->read_callback).Run(0);
  Expect(read_calls == 1 && read_result == 0 &&
             tunnel->LastReadWasDatagram(),
         "zero-length datagram remains distinct from EOF");

  state->start_result = net::ERR_CONNECTION_FAILED;
  state->open = false;
  Expect(tunnel->Start(net::CompletionOnceCallback()) ==
             net::ERR_CONNECTION_FAILED,
         "scripted start can fail synchronously");

  state->start_result = net::ERR_IO_PENDING;
  Expect(tunnel->Start(base::BindOnce([](int) {})) == net::ERR_IO_PENDING,
         "scripted destruction case has a pending callback");
  tunnel.reset();
  Expect(state->destructions == 1 && state->cancelled_callbacks == 1,
         "scripted tunnel destruction cancels pending callbacks");
}

void TestAsyncSingleTargetRoundTripAndReuse() {
  auto state = std::make_shared<ScriptedTunnelState>();
  int factory_calls = 0;
  std::vector<net::Socks5UdpDatagram> received;
  net::NaiveConnectUdpDatagramBackend backend(
      MakeContext(),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state, int* factory_calls,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            ++*factory_calls;
            return std::make_unique<ScriptedTunnel>(state);
          },
          state, &factory_calls));
  backend.Start(base::BindRepeating(
      [](std::vector<net::Socks5UdpDatagram>* received,
         net::Socks5UdpDatagram datagram) {
        received->push_back(std::move(datagram));
      },
      &received));

  Expect(backend.Send(Datagram({0x01, 0x02, 0x03}),
                      net::CompletionOnceCallback()) == net::OK,
         "async target admits first datagram");
  Expect(factory_calls == 1 && backend.target_count_for_testing() == 1,
         "first datagram lazily creates one target tunnel");
  Expect(state->start_callback && !state->write_callback,
         "outbound payload waits for pending CONNECT-UDP");

  CompleteStart(state, net::OK);
  Expect(state->read_callback && state->write_callback,
         "successful connect arms read and serialized write");
  Expect(state->written == std::vector<uint8_t>({0x01, 0x02, 0x03}) &&
             state->write_buffer,
         "pending write retains exact payload buffer");
  CompleteWrite(state, 3);
  Expect(backend.stats_for_testing().sent_datagrams == 1,
         "async write completion accounts one datagram");

  CompleteRead(state, {0x09, 0x08});
  Expect(received.size() == 1 &&
             received[0].destination == Datagram({}).destination &&
             received[0].payload == std::vector<uint8_t>({0x09, 0x08}),
         "async read returns bytes with original SOCKS endpoint");
  Expect(!state->read_callback.is_null(),
         "read pump immediately rearms after async datagram");

  CompleteRead(state, {});
  Expect(received.size() == 2 && received[1].payload.empty(),
         "async zero-length UDP datagram is delivered, not treated as EOF");
  Expect(backend.Send(Datagram({0x04}), net::CompletionOnceCallback()) ==
             net::OK,
         "same target admits a second datagram");
  Expect(factory_calls == 1 && state->write_callback,
         "same target reuses the connected tunnel");
  CompleteWrite(state, 1);
  Expect(backend.stats_for_testing().sent_datagrams == 2 &&
             backend.stats_for_testing().received_datagrams == 2,
         "single-target counters match bidirectional completions");
}

void TestSynchronousSingleTargetAndOversizeDrop() {
  auto state = std::make_shared<ScriptedTunnelState>();
  state->start_result = net::OK;
  state->write_result = 3;
  state->max_payload_size = 1200;
  net::NaiveConnectUdpDatagramBackend backend(
      MakeContext(), base::BindRepeating(
                         [](std::shared_ptr<ScriptedTunnelState> state,
                            const net::Socks5UdpBackendContext&,
                            const net::Socks5UdpEndpoint&)
                             -> std::unique_ptr<
                                 net::NaiveConnectUdpTargetTunnel> {
                           return std::make_unique<ScriptedTunnel>(state);
                         },
                         state));
  backend.Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  Expect(backend.Send(Datagram({0x01, 0x02, 0x03}),
                      net::CompletionOnceCallback()) == net::OK,
         "all-synchronous single target is admitted");
  Expect(backend.stats_for_testing().sent_datagrams == 1 &&
             state->read_callback,
         "all-synchronous connect/write still leaves continuous read armed");

  auto oversize_state = std::make_shared<ScriptedTunnelState>();
  oversize_state->start_result = net::OK;
  oversize_state->max_payload_size = 2;
  int oversize_responses = 0;
  net::NaiveConnectUdpDatagramBackend oversize_backend(
      MakeContext(32),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          oversize_state));
  oversize_backend.Start(base::BindRepeating(
      [](int* responses, net::Socks5UdpDatagram) { ++*responses; },
      &oversize_responses));
  Expect(oversize_backend.Send(Datagram({0x01, 0x02, 0x03}),
                               net::CompletionOnceCallback()) == net::OK,
         "oversize payload is a policy drop, not association-fatal");
  Expect(oversize_backend.stats_for_testing().oversize_drops == 1 &&
             !oversize_state->write_callback && oversize_responses == 0,
         "live payload ceiling drops before tunnel Write");

  Expect(oversize_backend.Send(Datagram({0x01, 0x02}),
                               net::CompletionOnceCallback()) == net::OK,
         "payload at the exact live ceiling is admitted");
  Expect(oversize_state->write_callback &&
             oversize_state->written == std::vector<uint8_t>({0x01, 0x02}),
         "exact-ceiling payload reaches the tunnel without truncation");
  CompleteWrite(oversize_state, 2);

  oversize_state->max_payload_size = 1;
  Expect(oversize_backend.Send(Datagram({0x03, 0x04}),
                               net::CompletionOnceCallback()) == net::OK,
         "payload above a lowered live ceiling is a policy drop");
  Expect(oversize_backend.stats_for_testing().oversize_drops == 2 &&
             !oversize_state->write_callback,
         "backend re-queries the live ceiling before every write");

  oversize_state->max_payload_size = 2;
  Expect(oversize_backend.Send(Datagram({0x05, 0x06}),
                               net::CompletionOnceCallback()) == net::OK,
         "payload is admitted after the live ceiling recovers");
  Expect(oversize_state->write_callback &&
             oversize_state->written == std::vector<uint8_t>({0x05, 0x06}),
         "ceiling recovery preserves the later payload exactly");
  CompleteWrite(oversize_state, 2);
  Expect(oversize_backend.stats_for_testing().sent_datagrams == 2 &&
             oversize_backend.stats_for_testing().oversize_drops == 2,
         "ceiling transitions account admitted and dropped datagrams");

  auto closed_state = std::make_shared<ScriptedTunnelState>();
  closed_state->start_result = net::OK;
  closed_state->write_result = 1;
  net::NaiveConnectUdpDatagramBackend closed_backend(
      MakeContext(33),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          closed_state),
      base::TimeDelta());
  closed_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  closed_backend.Send(Datagram({0x01}), net::CompletionOnceCallback());
  closed_state->open = false;
  closed_backend.Send(Datagram({0x02}), net::CompletionOnceCallback());
  RunUntilIdle();
  Expect(closed_backend.target_count_for_testing() == 0 &&
             closed_backend.stats_for_testing().sent_datagrams == 1 &&
             closed_backend.stats_for_testing().target_failures == 1 &&
             closed_backend.stats_for_testing().oversize_drops == 0,
         "closed live stream retires target instead of misclassifying MTU");
}

void TestShortWriteEofAndPendingDestruction() {
  auto short_state = std::make_shared<ScriptedTunnelState>();
  short_state->start_result = net::OK;
  short_state->write_result = 1;
  net::NaiveConnectUdpDatagramBackend short_backend(
      MakeContext(), base::BindRepeating(
                         [](std::shared_ptr<ScriptedTunnelState> state,
                            const net::Socks5UdpBackendContext&,
                            const net::Socks5UdpEndpoint&)
                             -> std::unique_ptr<
                                 net::NaiveConnectUdpTargetTunnel> {
                           return std::make_unique<ScriptedTunnel>(state);
                         },
                         short_state),
      base::TimeDelta());
  short_backend.Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  Expect(short_backend.Send(Datagram({0x01, 0x02, 0x03}),
                            net::CompletionOnceCallback()) == net::OK,
         "short-write target admission remains synchronous");
  RunUntilIdle();
  Expect(short_backend.target_count_for_testing() == 0 &&
             short_backend.stats_for_testing().target_failures == 1 &&
             short_backend.stats_for_testing().sent_datagrams == 0,
         "short write retires target without replay");

  auto eof_state = std::make_shared<ScriptedTunnelState>();
  eof_state->start_result = net::OK;
  eof_state->read_result = 0;
  eof_state->last_read_was_datagram = false;
  eof_state->write_result = 1;
  net::NaiveConnectUdpDatagramBackend eof_backend(
      MakeContext(), base::BindRepeating(
                         [](std::shared_ptr<ScriptedTunnelState> state,
                            const net::Socks5UdpBackendContext&,
                            const net::Socks5UdpEndpoint&)
                             -> std::unique_ptr<
                                 net::NaiveConnectUdpTargetTunnel> {
                           return std::make_unique<ScriptedTunnel>(state);
                         },
                         eof_state),
      base::TimeDelta());
  eof_backend.Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  Expect(eof_backend.Send(Datagram({0x01}),
                          net::CompletionOnceCallback()) == net::OK,
         "EOF case admits before transport outcome");
  RunUntilIdle();
  Expect(eof_backend.target_count_for_testing() == 0 &&
             eof_backend.stats_for_testing().target_failures == 1,
         "read zero without datagram evidence is EOF and retires target");

  auto pending_state = std::make_shared<ScriptedTunnelState>();
  auto pending_backend =
      std::make_unique<net::NaiveConnectUdpDatagramBackend>(
          MakeContext(),
          base::BindRepeating(
              [](std::shared_ptr<ScriptedTunnelState> state,
                 const net::Socks5UdpBackendContext&,
                 const net::Socks5UdpEndpoint&)
                  -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
                return std::make_unique<ScriptedTunnel>(state);
              },
              pending_state));
  pending_backend->Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  pending_backend->Send(Datagram({0x01}), net::CompletionOnceCallback());
  Expect(!pending_state->start_callback.is_null(),
         "destruction case has pending connect callback");
  pending_backend.reset();
  Expect(pending_state->destructions == 1 &&
             pending_state->cancelled_callbacks == 1,
         "backend destruction cancels pending target connect");
}

void TestPendingWriteDestruction() {
  auto state = std::make_shared<ScriptedTunnelState>();
  state->start_result = net::OK;
  state->write_result = net::ERR_IO_PENDING;
  auto backend = std::make_unique<net::NaiveConnectUdpDatagramBackend>(
      MakeContext(),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          state));
  backend->Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  backend->Send(Datagram({0x01, 0x02}), net::CompletionOnceCallback());
  Expect(state->write_callback && state->read_callback &&
             state->write_buffer,
         "destruction case retains pending target write and read buffers");
  backend.reset();
  Expect(state->destructions == 1 && state->cancelled_callbacks == 2 &&
             !state->write_buffer && !state->read_buffer,
         "backend destruction cancels pending target write/read exactly once");
}

void TestSynchronousZeroLengthAndCallbackDestruction() {
  auto zero_state = std::make_shared<ScriptedTunnelState>();
  zero_state->start_result = net::OK;
  zero_state->read_result = 0;
  zero_state->last_read_was_datagram = true;
  zero_state->write_result = 1;
  int zero_responses = 0;
  net::NaiveConnectUdpDatagramBackend zero_backend(
      MakeContext(), base::BindRepeating(
                         [](std::shared_ptr<ScriptedTunnelState> state,
                            const net::Socks5UdpBackendContext&,
                            const net::Socks5UdpEndpoint&)
                             -> std::unique_ptr<
                                 net::NaiveConnectUdpTargetTunnel> {
                           return std::make_unique<ScriptedTunnel>(state);
                         },
                         zero_state));
  zero_backend.Start(base::BindRepeating(
      [](std::shared_ptr<ScriptedTunnelState> state, int* responses,
         net::Socks5UdpDatagram datagram) {
        ++*responses;
        CHECK(datagram.payload.empty());
        state->read_result = net::ERR_IO_PENDING;
      },
      zero_state, &zero_responses));
  zero_backend.Send(Datagram({0x01}), net::CompletionOnceCallback());
  Expect(zero_responses == 1 && zero_state->read_callback,
         "synchronous empty datagram is delivered once then read rearms");

  auto destroy_state = std::make_shared<ScriptedTunnelState>();
  destroy_state->start_result = net::OK;
  destroy_state->write_result = 1;
  std::unique_ptr<net::NaiveConnectUdpDatagramBackend> destroy_backend;
  destroy_backend = std::make_unique<net::NaiveConnectUdpDatagramBackend>(
      MakeContext(),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          destroy_state));
  destroy_backend->Start(base::BindRepeating(
      [](std::unique_ptr<net::NaiveConnectUdpDatagramBackend>* backend,
         net::Socks5UdpDatagram) { backend->reset(); },
      &destroy_backend));
  destroy_backend->Send(Datagram({0x01}), net::CompletionOnceCallback());
  Expect(!destroy_state->read_callback.is_null(),
         "callback-destruction case has pending read");
  CompleteRead(destroy_state, {0x02});
  Expect(!destroy_backend && destroy_state->destructions == 1,
         "receive callback can destroy backend without UAF or rearm");
}

void TestMultiTargetRoutingAndFailureIsolation() {
  auto first_state = std::make_shared<ScriptedTunnelState>();
  first_state->start_result = net::OK;
  first_state->write_result = 1;
  auto second_state = std::make_shared<ScriptedTunnelState>();
  second_state->start_result = net::OK;
  second_state->write_result = 1;
  std::map<std::string, std::shared_ptr<ScriptedTunnelState>> states = {
      {"one.test", first_state}, {"two.test", second_state}};
  int factory_calls = 0;
  std::vector<net::Socks5UdpDatagram> received;
  net::NaiveConnectUdpDatagramBackend backend(
      MakeContext(),
      base::BindRepeating(
          [](const std::map<std::string,
                            std::shared_ptr<ScriptedTunnelState>>* states,
             int* factory_calls, const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint& endpoint)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            ++*factory_calls;
            return std::make_unique<ScriptedTunnel>(states->at(endpoint.host));
          },
          &states, &factory_calls));
  backend.Start(base::BindRepeating(
      [](std::vector<net::Socks5UdpDatagram>* received,
         net::Socks5UdpDatagram datagram) {
        received->push_back(std::move(datagram));
      },
      &received));
  const net::Socks5UdpEndpoint first_endpoint{
      .type = net::Socks5UdpAddressType::kDomain,
      .host = "one.test",
      .port = 1001,
  };
  const net::Socks5UdpEndpoint second_endpoint{
      .type = net::Socks5UdpAddressType::kDomain,
      .host = "two.test",
      .port = 1002,
  };
  backend.Send(DatagramFor(first_endpoint, {0x01}),
               net::CompletionOnceCallback());
  backend.Send(DatagramFor(second_endpoint, {0x02}),
               net::CompletionOnceCallback());
  Expect(factory_calls == 2 && backend.target_count_for_testing() == 2,
         "distinct targets own distinct tunnels");
  CompleteRead(second_state, {0x22});
  CompleteRead(first_state, {0x11});
  Expect(received.size() == 2 &&
             received[0].destination == second_endpoint &&
             received[0].payload == std::vector<uint8_t>({0x22}) &&
             received[1].destination == first_endpoint &&
             received[1].payload == std::vector<uint8_t>({0x11}),
         "interleaved responses cannot cross target routes");

  CompleteReadError(first_state, net::ERR_CONNECTION_CLOSED);
  RunUntilIdle();
  Expect(backend.target_count_for_testing() == 2 &&
             backend.stats_for_testing().target_failures == 1,
         "failed target becomes cooldown tombstone while peer remains open");
  backend.Send(DatagramFor(first_endpoint, {0x33}),
               net::CompletionOnceCallback());
  Expect(factory_calls == 2 &&
             backend.stats_for_testing().cooldown_drops == 1,
         "cooldown suppresses reconnect storms");
  backend.Send(DatagramFor(second_endpoint, {0x44}),
               net::CompletionOnceCallback());
  Expect(second_state->written == std::vector<uint8_t>({0x44}) &&
             backend.stats_for_testing().sent_datagrams == 3,
         "one target failure does not affect another target");
}

void TestTargetAndQueueLimits() {
  std::vector<std::shared_ptr<ScriptedTunnelState>> target_states;
  int factory_calls = 0;
  net::NaiveConnectUdpDatagramBackend target_cap_backend(
      MakeContext(),
      base::BindRepeating(
          [](std::vector<std::shared_ptr<ScriptedTunnelState>>* states,
             int* factory_calls, const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            ++*factory_calls;
            auto state = std::make_shared<ScriptedTunnelState>();
            states->push_back(state);
            return std::make_unique<ScriptedTunnel>(state);
          },
          &target_states, &factory_calls));
  target_cap_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  for (size_t i = 0; i < net::Socks5UdpBackendLimits::kMaxTargets + 1; ++i) {
    target_cap_backend.Send(
        DatagramFor(net::Socks5UdpEndpoint{
                        .type = net::Socks5UdpAddressType::kDomain,
                        .host = "target-" + std::to_string(i) + ".test",
                        .port = 443,
                    },
                    {0x01}),
        net::CompletionOnceCallback());
  }
  Expect(target_cap_backend.target_count_for_testing() == 32 &&
             factory_calls == 32 &&
             target_cap_backend.stats_for_testing().capacity_drops == 1,
         "33rd busy target is dropped without eviction");

  auto per_target_state = std::make_shared<ScriptedTunnelState>();
  net::NaiveConnectUdpDatagramBackend per_target_backend(
      MakeContext(),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          per_target_state));
  per_target_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  for (size_t i = 0;
       i < net::Socks5UdpBackendLimits::kMaxQueuedDatagramsPerTarget + 1;
       ++i) {
    per_target_backend.Send(Datagram({0x01}),
                            net::CompletionOnceCallback());
  }
  Expect(per_target_backend.stats_for_testing().admitted_datagrams == 16 &&
             per_target_backend.stats_for_testing().capacity_drops == 1,
         "per-target queue admits 16 and drops the 17th");

  std::vector<std::shared_ptr<ScriptedTunnelState>> association_states;
  net::NaiveConnectUdpDatagramBackend association_cap_backend(
      MakeContext(),
      base::BindRepeating(
          [](std::vector<std::shared_ptr<ScriptedTunnelState>>* states,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            auto state = std::make_shared<ScriptedTunnelState>();
            states->push_back(state);
            return std::make_unique<ScriptedTunnel>(state);
          },
          &association_states));
  association_cap_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  for (size_t target = 0; target < 9; ++target) {
    const size_t count = target < 8 ? 15 : 8;
    for (size_t packet = 0; packet < count; ++packet) {
      association_cap_backend.Send(
          DatagramFor(net::Socks5UdpEndpoint{
                          .type = net::Socks5UdpAddressType::kDomain,
                          .host = "queue-" + std::to_string(target) + ".test",
                          .port = 443,
                      },
                      {0x01}),
          net::CompletionOnceCallback());
    }
  }
  association_cap_backend.Send(
      DatagramFor(net::Socks5UdpEndpoint{
                      .type = net::Socks5UdpAddressType::kDomain,
                      .host = "queue-8.test",
                      .port = 443,
                  },
                  {0x02}),
      net::CompletionOnceCallback());
  Expect(association_cap_backend.stats_for_testing().admitted_datagrams ==
                 128 &&
             association_cap_backend.stats_for_testing().capacity_drops == 1,
         "association packet queue admits 128 and drops the 129th");

  auto byte_state = std::make_shared<ScriptedTunnelState>();
  net::NaiveConnectUdpDatagramBackend byte_cap_backend(
      MakeContext(),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          byte_state));
  byte_cap_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  for (int i = 0; i < 4; ++i) {
    byte_cap_backend.Send(Datagram(std::vector<uint8_t>(65000, 0x01)),
                          net::CompletionOnceCallback());
  }
  byte_cap_backend.Send(Datagram(std::vector<uint8_t>(10000, 0x02)),
                        net::CompletionOnceCallback());
  Expect(byte_cap_backend.stats_for_testing().admitted_datagrams == 4 &&
             byte_cap_backend.stats_for_testing().capacity_drops == 1,
         "association byte queue rejects payload beyond 256 KiB");
}

void TestTimeoutIdleCooldownAndNoReplay() {
  auto timed_out_state = std::make_shared<ScriptedTunnelState>();
  auto fresh_state = std::make_shared<ScriptedTunnelState>();
  fresh_state->start_result = net::OK;
  fresh_state->write_result = 1;
  int factory_calls = 0;
  net::NaiveConnectUdpDatagramBackend reconnect_backend(
      MakeContext(41, base::TimeDelta(), base::Seconds(30)),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> first,
             std::shared_ptr<ScriptedTunnelState> second, int* calls,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            std::shared_ptr<ScriptedTunnelState> state =
                (*calls)++ == 0 ? first : second;
            return std::make_unique<ScriptedTunnel>(state);
          },
          timed_out_state, fresh_state, &factory_calls),
      base::TimeDelta());
  reconnect_backend.Start(
      base::BindRepeating([](net::Socks5UdpDatagram) {}));
  reconnect_backend.Send(Datagram({0xaa}), net::CompletionOnceCallback());
  RunUntilIdle();
  Expect(factory_calls == 1 && timed_out_state->written.empty() &&
             reconnect_backend.stats_for_testing().connect_timeouts == 1 &&
             reconnect_backend.target_count_for_testing() == 0,
         "connect timeout clears old queue and expires test cooldown");
  reconnect_backend.Send(Datagram({0xbb}), net::CompletionOnceCallback());
  Expect(factory_calls == 2 &&
             fresh_state->written == std::vector<uint8_t>({0xbb}) &&
             reconnect_backend.stats_for_testing().sent_datagrams == 1,
         "only a later new packet creates a fresh tunnel; old data not replayed");

  auto idle_state = std::make_shared<ScriptedTunnelState>();
  idle_state->start_result = net::OK;
  idle_state->write_result = 1;
  net::NaiveConnectUdpDatagramBackend idle_backend(
      MakeContext(42, base::Seconds(10), base::TimeDelta()),
      base::BindRepeating(
          [](std::shared_ptr<ScriptedTunnelState> state,
             const net::Socks5UdpBackendContext&,
             const net::Socks5UdpEndpoint&)
              -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
            return std::make_unique<ScriptedTunnel>(state);
          },
          idle_state));
  idle_backend.Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
  idle_backend.Send(Datagram({0x01}), net::CompletionOnceCallback());
  RunUntilIdle();
  Expect(idle_backend.target_count_for_testing() == 0 &&
             idle_backend.stats_for_testing().idle_evictions == 1 &&
             idle_backend.stats_for_testing().target_failures == 0,
         "idle target is evicted without being classified as a failure");
}

void TestSynchronousReadPumpYield() {
  auto state = std::make_shared<ScriptedTunnelState>();
  state->start_result = net::OK;
  state->read_result = 1;
  state->read_payload = {0x42};
  state->last_read_was_datagram = true;
  state->write_result = 1;
  int responses = 0;
  net::NaiveConnectUdpDatagramBackend backend(
      MakeContext(), base::BindRepeating(
                         [](std::shared_ptr<ScriptedTunnelState> state,
                            const net::Socks5UdpBackendContext&,
                            const net::Socks5UdpEndpoint&)
                             -> std::unique_ptr<
                                 net::NaiveConnectUdpTargetTunnel> {
                           return std::make_unique<ScriptedTunnel>(state);
                         },
                         state));
  backend.Start(base::BindRepeating(
      [](std::shared_ptr<ScriptedTunnelState> state, int* responses,
         net::Socks5UdpDatagram) {
        ++*responses;
        if (*responses == 32) {
          state->read_result = net::ERR_IO_PENDING;
        }
      },
      state, &responses));
  backend.Send(Datagram({0x01}), net::CompletionOnceCallback());
  Expect(responses == 32 && state->read_callback.is_null(),
         "synchronous read pump yields after exactly 32 completions");
  RunUntilIdle();
  Expect(!state->read_callback.is_null() && responses == 32,
         "posted read pump resumes and arms the next asynchronous read");
}

void TestSeededLifecycleSchedules() {
  constexpr size_t kIterations = 2000;
  uint64_t random = 0x4d3655494645ULL;
  size_t destroyed_while_connecting = 0;
  size_t destroyed_while_writing = 0;
  size_t completed_reads = 0;

  for (size_t iteration = 0; iteration < kIterations; ++iteration) {
    auto state = std::make_shared<ScriptedTunnelState>();
    state->start_result =
        NextLifecycleRandom(&random) % 2 ? net::OK : net::ERR_IO_PENDING;
    state->write_result =
        NextLifecycleRandom(&random) % 2 ? 1 : net::ERR_IO_PENDING;
    auto backend = std::make_unique<net::NaiveConnectUdpDatagramBackend>(
        MakeContext(static_cast<unsigned int>(1000 + iteration)),
        base::BindRepeating(
            [](std::shared_ptr<ScriptedTunnelState> state,
               const net::Socks5UdpBackendContext&,
               const net::Socks5UdpEndpoint&)
                -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
              return std::make_unique<ScriptedTunnel>(state);
            },
            state),
        base::TimeDelta());
    backend->Start(base::BindRepeating([](net::Socks5UdpDatagram) {}));
    backend->Send(Datagram({static_cast<uint8_t>(iteration)}),
                  net::CompletionOnceCallback());

    if (state->start_callback) {
      const uint64_t action = NextLifecycleRandom(&random) % 3;
      if (action == 0) {
        backend.reset();
        ++destroyed_while_connecting;
      } else {
        CompleteStart(state, action == 1 ? net::OK : net::ERR_FAILED);
      }
    }
    if (backend && state->write_callback) {
      const uint64_t action = NextLifecycleRandom(&random) % 3;
      if (action == 0) {
        backend.reset();
        ++destroyed_while_writing;
      } else {
        CompleteWrite(state, action == 1 ? 1 : net::ERR_CONNECTION_CLOSED);
      }
    }
    if (backend && state->read_callback &&
        NextLifecycleRandom(&random) % 2) {
      CompleteRead(state, NextLifecycleRandom(&random) % 2
                              ? std::vector<uint8_t>{}
                              : std::vector<uint8_t>{0x42});
      ++completed_reads;
    }

    backend.reset();
    RunUntilIdle();
    Expect(state->destructions == 1 && !state->start_callback &&
               !state->write_callback && !state->read_callback,
           "seeded lifecycle schedule cancels every pending callback");
  }

  Expect(destroyed_while_connecting > 0 && destroyed_while_writing > 0 &&
             completed_reads > 0,
         "seeded lifecycle schedule exercises connect/write/read destruction");
}

}  // namespace

int main() {
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  TestFrozenContracts();
  TestImmutableContextAndSkeleton();
  TestScriptedTunnelSeam();
  TestAsyncSingleTargetRoundTripAndReuse();
  TestSynchronousSingleTargetAndOversizeDrop();
  TestShortWriteEofAndPendingDestruction();
  TestPendingWriteDestruction();
  TestSynchronousZeroLengthAndCallbackDestruction();
  TestMultiTargetRoutingAndFailureIsolation();
  TestTargetAndQueueLimits();
  TestTimeoutIdleCooldownAndNoReplay();
  TestSynchronousReadPumpYield();
  TestSeededLifecycleSchedules();
  if (failures != 0) {
    std::cerr << "M3 G0 failures=" << failures << "\n";
    return 1;
  }
  std::cout << "M3_G0_BACKEND_CONTRACT_OK\n";
  std::cout << "M3_G1_SINGLE_TARGET_OK\n";
  std::cout << "M3_G2_MULTI_TARGET_LIMITS_OK\n";
  std::cout << "M3_G2_FAILURE_ISOLATION_OK\n";
  std::cout << "M3_G5_DETERMINISTIC_LIFECYCLE_OK\n";
  std::cout << "M6_G1_LIVE_CEILING_UNIT_OK\n";
  std::cout << "M6_G4_SEEDED_LIFECYCLE_OK iterations=2000\n";
  return 0;
}
