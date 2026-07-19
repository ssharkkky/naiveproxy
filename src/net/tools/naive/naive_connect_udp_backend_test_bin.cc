// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <iostream>
#include <memory>
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

class UnusedScriptedTunnel final : public net::NaiveConnectUdpTargetTunnel {
 public:
  int Start(net::CompletionOnceCallback callback) override { return net::OK; }
  int Read(net::IOBuffer* buffer,
           int buffer_length,
           net::CompletionOnceCallback callback) override {
    return net::ERR_IO_PENDING;
  }
  int Write(net::IOBuffer* buffer,
            int buffer_length,
            net::CompletionOnceCallback callback) override {
    return buffer_length;
  }
  bool IsOpen() const override { return true; }
  bool LastReadWasDatagram() const override { return false; }
  size_t MaxPayloadSize() const override { return 1200; }
};

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
  scoped_refptr<net::IOBuffer> read_buffer;
  int read_buffer_length = 0;
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
    }
    return state_->read_result;
  }

  int Write(net::IOBuffer* buffer,
            int buffer_length,
            net::CompletionOnceCallback callback) override {
    state_->written.assign(buffer->span().begin(),
                           buffer->span().begin() + buffer_length);
    if (state_->write_result == net::ERR_IO_PENDING) {
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

int failures = 0;

void Expect(bool condition, std::string_view description) {
  if (!condition) {
    std::cerr << "FAILED: " << description << "\n";
    ++failures;
  }
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
  bool exact_context_seen = false;
  net::NaiveConnectUdpDatagramBackend backend(
      context,
      base::BindRepeating(
          [](const net::NetworkAnonymizationKey* expected_nak,
             bool* exact_context_seen,
             const net::Socks5UdpBackendContext& actual_context,
             const net::Socks5UdpEndpoint&)
                       -> std::unique_ptr<net::NaiveConnectUdpTargetTunnel> {
                     *exact_context_seen =
                         actual_context.network_anonymization_key ==
                         *expected_nak;
                     return std::make_unique<UnusedScriptedTunnel>();
                   },
          &nak, &exact_context_seen));
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

}  // namespace

int main() {
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  TestFrozenContracts();
  TestImmutableContextAndSkeleton();
  TestScriptedTunnelSeam();
  if (failures != 0) {
    std::cerr << "M3 G0 failures=" << failures << "\n";
    return 1;
  }
  std::cout << "M3_G0_BACKEND_CONTRACT_OK\n";
  return 0;
}
