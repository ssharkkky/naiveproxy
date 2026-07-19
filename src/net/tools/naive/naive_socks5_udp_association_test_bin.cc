// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/datagram_server_socket.h"
#include "net/socket/next_proto.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/socks5_server_socket.h"
#include "net/tools/naive/socks5_udp_association.h"
#include "net/tools/naive/socks5_udp_codec.h"
#include "net/tools/naive/socks5_udp_datagram_backend.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive_socks5_udp_association_test",
                                        "");

struct StreamReadEvent {
  std::vector<uint8_t> data;
  size_t offset = 0;
  int result = net::OK;
};

struct ControlState {
  std::deque<StreamReadEvent> reads;
  std::vector<uint8_t> written;
  int read_calls = 0;
  int write_calls = 0;
  int disconnects = 0;
  int pending_read_cancellations = 0;
  bool read_pending = false;
};

void QueueControlData(const std::shared_ptr<ControlState>& state,
                      std::vector<uint8_t> data) {
  CHECK(!data.empty());
  state->reads.push_back(
      StreamReadEvent{.data = std::move(data), .offset = 0, .result = net::OK});
}

void QueueControlResult(const std::shared_ptr<ControlState>& state,
                        int result) {
  CHECK_LE(result, 0);
  state->reads.push_back(
      StreamReadEvent{.data = {}, .offset = 0, .result = result});
}

class ScriptedControlSocket final : public net::StreamSocket {
 public:
  ScriptedControlSocket(std::shared_ptr<ControlState> state,
                        net::IPEndPoint local_endpoint,
                        net::IPEndPoint peer_endpoint)
      : state_(std::move(state)),
        local_endpoint_(std::move(local_endpoint)),
        peer_endpoint_(std::move(peer_endpoint)) {}

  ~ScriptedControlSocket() override { Disconnect(); }

  int Connect(net::CompletionOnceCallback callback) override {
    connected_ = true;
    return net::OK;
  }

  void Disconnect() override {
    if (!connected_) {
      return;
    }
    connected_ = false;
    ++state_->disconnects;
    if (pending_read_callback_) {
      ++state_->pending_read_cancellations;
      pending_read_callback_.Reset();
      pending_read_buffer_.reset();
      state_->read_pending = false;
    }
  }

  bool IsConnected() const override { return connected_; }

  bool IsConnectedAndIdle() const override {
    return connected_ && !state_->read_pending;
  }

  int GetPeerAddress(net::IPEndPoint* address) const override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    *address = peer_endpoint_;
    return net::OK;
  }

  int GetLocalAddress(net::IPEndPoint* address) const override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    *address = local_endpoint_;
    return net::OK;
  }

  const net::NetLogWithSource& NetLog() const override { return net_log_; }

  bool WasEverUsed() const override {
    return state_->read_calls != 0 || state_->write_calls != 0;
  }

  net::NextProto GetNegotiatedProtocol() const override {
    return net::NextProto::kProtoUnknown;
  }

  bool GetSSLInfo(net::SSLInfo* ssl_info) override { return false; }

  int64_t GetTotalReceivedBytes() const override { return received_bytes_; }

  void ApplySocketTag(const net::SocketTag& tag) override {}

  int Read(net::IOBuffer* buffer,
           int buffer_length,
           net::CompletionOnceCallback callback) override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    CHECK(!state_->read_pending);
    ++state_->read_calls;
    if (state_->reads.empty()) {
      state_->read_pending = true;
      pending_read_buffer_ = base::WrapRefCounted(buffer);
      pending_read_callback_ = std::move(callback);
      return net::ERR_IO_PENDING;
    }

    StreamReadEvent& event = state_->reads.front();
    if (event.data.empty()) {
      const int result = event.result;
      state_->reads.pop_front();
      return result;
    }

    const size_t count = std::min(static_cast<size_t>(buffer_length),
                                  event.data.size() - event.offset);
    buffer->first(count).copy_from(
        base::span(event.data).subspan(event.offset, count));
    event.offset += count;
    received_bytes_ += static_cast<int64_t>(count);
    if (event.offset == event.data.size()) {
      state_->reads.pop_front();
    }
    return static_cast<int>(count);
  }

  int Write(
      net::IOBuffer* buffer,
      int buffer_length,
      net::CompletionOnceCallback callback,
      const net::NetworkTrafficAnnotationTag& traffic_annotation) override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    ++state_->write_calls;
    state_->written.insert(state_->written.end(), buffer->span().begin(),
                           buffer->span().begin() + buffer_length);
    return buffer_length;
  }

  int SetReceiveBufferSize(int32_t size) override { return net::OK; }
  int SetSendBufferSize(int32_t size) override { return net::OK; }

 private:
  const std::shared_ptr<ControlState> state_;
  const net::IPEndPoint local_endpoint_;
  const net::IPEndPoint peer_endpoint_;
  bool connected_ = true;
  int64_t received_bytes_ = 0;
  scoped_refptr<net::IOBuffer> pending_read_buffer_;
  net::CompletionOnceCallback pending_read_callback_;
  net::NetLogWithSource net_log_;
};

struct RelayReadEvent {
  std::vector<uint8_t> data;
  net::IPEndPoint source;
  int result = net::OK;
};

struct SentPacket {
  std::vector<uint8_t> data;
  net::IPEndPoint destination;
};

struct RelayState {
  std::deque<RelayReadEvent> reads;
  std::deque<int> send_results;
  std::vector<SentPacket> sent_packets;
  int recv_calls = 0;
  int send_calls = 0;
  int close_calls = 0;
  int recv_after_close_calls = 0;
  int pending_recv_cancellations = 0;
  int pending_send_cancellations = 0;
  bool recv_pending = false;
  bool send_pending = false;
  bool closed = false;
  scoped_refptr<net::IOBuffer> pending_send_buffer;
  net::CompletionOnceCallback pending_send_callback;
};

void QueueRelayData(const std::shared_ptr<RelayState>& state,
                    std::vector<uint8_t> data,
                    const net::IPEndPoint& source) {
  CHECK(!data.empty());
  state->reads.push_back(RelayReadEvent{
      .data = std::move(data), .source = source, .result = net::OK});
}

class ScriptedDatagramSocket final : public net::DatagramServerSocket {
 public:
  ScriptedDatagramSocket(std::shared_ptr<RelayState> state,
                         net::IPEndPoint local_endpoint)
      : state_(std::move(state)), local_endpoint_(std::move(local_endpoint)) {}

  ~ScriptedDatagramSocket() override { Close(); }

  int Listen(const net::IPEndPoint& address) override { return net::OK; }

  int RecvFrom(net::IOBuffer* buffer,
               int buffer_length,
               net::IPEndPoint* address,
               net::CompletionOnceCallback callback) override {
    ++state_->recv_calls;
    if (state_->closed) {
      ++state_->recv_after_close_calls;
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    CHECK(!state_->recv_pending);
    if (state_->reads.empty()) {
      state_->recv_pending = true;
      pending_recv_buffer_ = base::WrapRefCounted(buffer);
      pending_recv_address_ = address;
      pending_recv_callback_ = std::move(callback);
      return net::ERR_IO_PENDING;
    }

    RelayReadEvent event = std::move(state_->reads.front());
    state_->reads.pop_front();
    if (event.result != net::OK) {
      return event.result;
    }
    CHECK_LE(event.data.size(), static_cast<size_t>(buffer_length));
    buffer->first(event.data.size()).copy_from(event.data);
    *address = event.source;
    return static_cast<int>(event.data.size());
  }

  int SendTo(net::IOBuffer* buffer,
             int buffer_length,
             const net::IPEndPoint& address,
             net::CompletionOnceCallback callback) override {
    ++state_->send_calls;
    if (state_->closed) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    std::vector<uint8_t> packet(static_cast<size_t>(buffer_length));
    base::span(packet).copy_from(buffer->first(buffer_length));
    state_->sent_packets.push_back(
        SentPacket{.data = std::move(packet), .destination = address});

    int result = buffer_length;
    if (!state_->send_results.empty()) {
      result = state_->send_results.front();
      state_->send_results.pop_front();
    }
    if (result == net::ERR_IO_PENDING) {
      CHECK(!state_->send_pending);
      state_->send_pending = true;
      state_->pending_send_buffer = base::WrapRefCounted(buffer);
      state_->pending_send_callback = std::move(callback);
    }
    return result;
  }

  void Close() override {
    if (state_->closed) {
      return;
    }
    state_->closed = true;
    ++state_->close_calls;
    if (pending_recv_callback_) {
      ++state_->pending_recv_cancellations;
      pending_recv_callback_.Reset();
      pending_recv_buffer_.reset();
      pending_recv_address_ = nullptr;
      state_->recv_pending = false;
    }
    if (state_->pending_send_callback) {
      ++state_->pending_send_cancellations;
      state_->pending_send_callback.Reset();
      state_->pending_send_buffer.reset();
      state_->send_pending = false;
    }
  }

  int GetPeerAddress(net::IPEndPoint* address) const override {
    return net::ERR_SOCKET_NOT_CONNECTED;
  }

  int GetLocalAddress(net::IPEndPoint* address) const override {
    *address = local_endpoint_;
    return net::OK;
  }

  void UseNonBlockingIO() override {}
  int SetDoNotFragment() override { return net::OK; }
  int SetRecvTos() override { return net::OK; }
  int SetTos(net::DiffServCodePoint dscp, net::EcnCodePoint ecn) override {
    return net::OK;
  }
  void SetMsgConfirm(bool confirm) override {}
  const net::NetLogWithSource& NetLog() const override { return net_log_; }
  net::DscpAndEcn GetLastTos() const override {
    return {net::DSCP_DEFAULT, net::ECN_DEFAULT};
  }
  int SetReceiveBufferSize(int32_t size) override { return net::OK; }
  int SetSendBufferSize(int32_t size) override { return net::OK; }
  void AllowAddressReuse() override {}
  void AllowBroadcast() override {}
  void AllowAddressSharingForMulticast() override {}
  int JoinGroup(const net::IPAddress& group_address) const override {
    return net::OK;
  }
  int LeaveGroup(const net::IPAddress& group_address) const override {
    return net::OK;
  }
  int SetMulticastInterface(uint32_t interface_index) override {
    return net::OK;
  }
  int SetMulticastTimeToLive(int time_to_live) override { return net::OK; }
  int SetMulticastLoopbackMode(bool loopback) override { return net::OK; }
  int SetDiffServCodePoint(net::DiffServCodePoint dscp) override {
    return net::OK;
  }
  void DetachFromThread() override {}

 private:
  const std::shared_ptr<RelayState> state_;
  const net::IPEndPoint local_endpoint_;
  scoped_refptr<net::IOBuffer> pending_recv_buffer_;
  net::IPEndPoint* pending_recv_address_ = nullptr;
  net::CompletionOnceCallback pending_recv_callback_;
  net::NetLogWithSource net_log_;
};

void CompleteRelaySend(const std::shared_ptr<RelayState>& state, int result) {
  CHECK(state->send_pending);
  CHECK(state->pending_send_callback);
  state->send_pending = false;
  state->pending_send_buffer.reset();
  std::move(state->pending_send_callback).Run(result);
}

enum class BackendMode {
  kAcceptSynchronously,
  kEchoSynchronously,
  kPending,
};

struct BackendState {
  BackendMode mode = BackendMode::kAcceptSynchronously;
  std::vector<net::Socks5UdpDatagram> sent_datagrams;
  int start_calls = 0;
  int send_calls = 0;
  int pending_send_cancellations = 0;
  int destructions = 0;
  bool send_pending = false;
  net::Socks5UdpDatagramBackend::ReceiveCallback receive_callback;
};

class ScriptedBackend final : public net::Socks5UdpDatagramBackend {
 public:
  explicit ScriptedBackend(std::shared_ptr<BackendState> state)
      : state_(std::move(state)) {}

  ~ScriptedBackend() override {
    state_->receive_callback.Reset();
    if (pending_send_callback_) {
      ++state_->pending_send_cancellations;
      pending_send_callback_.Reset();
      state_->send_pending = false;
    }
    ++state_->destructions;
  }

  void Start(ReceiveCallback receive_callback) override {
    ++state_->start_calls;
    state_->receive_callback = std::move(receive_callback);
  }

  int Send(net::Socks5UdpDatagram datagram,
           net::CompletionOnceCallback callback) override {
    ++state_->send_calls;
    state_->sent_datagrams.push_back(datagram);
    switch (state_->mode) {
      case BackendMode::kAcceptSynchronously:
        return net::OK;
      case BackendMode::kEchoSynchronously:
        state_->receive_callback.Run(std::move(datagram));
        return net::OK;
      case BackendMode::kPending:
        CHECK(!state_->send_pending);
        state_->send_pending = true;
        pending_send_callback_ = std::move(callback);
        return net::ERR_IO_PENDING;
    }
  }

 private:
  const std::shared_ptr<BackendState> state_;
  net::CompletionOnceCallback pending_send_callback_;
};

struct CompletionState {
  int calls = 0;
  int result = net::ERR_UNEXPECTED;
};

struct AssociationHarness {
  std::shared_ptr<ControlState> control = std::make_shared<ControlState>();
  std::shared_ptr<RelayState> relay = std::make_shared<RelayState>();
  std::shared_ptr<BackendState> backend = std::make_shared<BackendState>();
  CompletionState completion;
  net::IPEndPoint control_peer{net::IPAddress::IPv4Localhost(), 41000};
  net::IPEndPoint relay_endpoint{net::IPAddress::IPv4Localhost(), 42000};
  std::unique_ptr<net::Socks5UdpAssociation> association;
};

std::vector<uint8_t> UdpAssociateHandshake(uint16_t requested_port) {
  return {
      0x05,
      0x01,
      0x00,  // Greeting: no authentication.
      0x05,
      0x03,
      0x00,
      0x01,
      0x00,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(requested_port >> 8),
      static_cast<uint8_t>(requested_port & 0xff),
  };
}

void BuildAssociation(AssociationHarness& harness,
                      uint16_t requested_port = 0) {
  QueueControlData(harness.control, UdpAssociateHandshake(requested_port));
  auto transport = std::make_unique<ScriptedControlSocket>(
      harness.control, net::IPEndPoint(net::IPAddress::IPv4Localhost(), 1080),
      harness.control_peer);
  auto control_socket = std::make_unique<net::Socks5ServerSocket>(
      std::move(transport), "", "", kTrafficAnnotation);

  int callback_calls = 0;
  int result = control_socket->ReadRequest(base::BindOnce(
      [](int* callback_calls, int) { ++*callback_calls; }, &callback_calls));
  CHECK_EQ(result, net::OK);
  CHECK_EQ(callback_calls, 0);
  CHECK(control_socket->request_parsed());
  CHECK_EQ(control_socket->command(),
           net::Socks5ServerSocket::Command::kUdpAssociate);
  CHECK_EQ(control_socket->request_endpoint().port(), requested_port);
  result = control_socket->WriteReply(
      net::Socks5ServerSocket::Reply::kSuccess, harness.relay_endpoint,
      base::BindOnce([](int* callback_calls, int) { ++*callback_calls; },
                     &callback_calls));
  CHECK_EQ(result, net::OK);
  CHECK_EQ(callback_calls, 0);

  auto relay = std::make_unique<ScriptedDatagramSocket>(harness.relay,
                                                        harness.relay_endpoint);
  auto backend = std::make_unique<ScriptedBackend>(harness.backend);
  harness.association = std::make_unique<net::Socks5UdpAssociation>(
      7, std::move(control_socket), std::move(relay), std::move(backend),
      harness.control_peer);
}

int StartAssociation(AssociationHarness& harness) {
  return harness.association->Start(base::BindOnce(
      [](CompletionState* state, int result) {
        ++state->calls;
        state->result = result;
      },
      &harness.completion));
}

std::vector<uint8_t> ValidPacket() {
  net::Socks5UdpDatagram datagram{
      .destination =
          net::Socks5UdpEndpoint{
              .type = net::Socks5UdpAddressType::kIpv4,
              .host = "203.0.113.7",
              .port = 443,
          },
      .payload = {0xde, 0xad, 0xbe, 0xef},
  };
  auto packet = net::BuildSocks5UdpDatagram(datagram);
  CHECK(packet.has_value());
  return std::move(*packet);
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

void TestFirstRecvFromCompletesSynchronously() {
  AssociationHarness harness;
  BuildAssociation(harness);
  harness.backend->mode = BackendMode::kEchoSynchronously;
  const std::vector<uint8_t> packet = ValidPacket();
  QueueRelayData(harness.relay, packet, harness.control_peer);

  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "Start remains active for synchronous first RecvFrom");
  RunUntilIdle();

  Expect(harness.completion.calls == 0,
         "synchronous first RecvFrom keeps association active");
  Expect(harness.backend->send_calls == 1,
         "synchronous first RecvFrom reaches backend");
  Expect(harness.relay->recv_calls == 2 && harness.relay->recv_pending,
         "synchronous first RecvFrom is followed by pending receive");
  Expect(harness.relay->sent_packets.size() == 1,
         "synchronous backend echo is sent to client");
  if (harness.relay->sent_packets.size() == 1) {
    Expect(harness.relay->sent_packets[0].data == packet,
           "synchronous backend echo preserves SOCKS5 UDP packet");
    Expect(harness.relay->sent_packets[0].destination == harness.control_peer,
           "synchronous backend echo targets learned client endpoint");
  }
}

void TestSynchronousInvalidBurstYieldsAndRecovers() {
  AssociationHarness harness;
  BuildAssociation(harness);
  const std::vector<uint8_t> packet = ValidPacket();
  const net::IPEndPoint malformed_source(net::IPAddress::IPv4Localhost(),
                                         41001);
  const net::IPEndPoint unauthorized(net::IPAddress::IPv6Localhost(), 41000);
  for (int i = 0; i < 20; ++i) {
    QueueRelayData(harness.relay, {0x00, 0x00, 0x00}, malformed_source);
  }
  for (int i = 0; i < 20; ++i) {
    QueueRelayData(harness.relay, packet, unauthorized);
  }
  QueueRelayData(harness.relay, packet, harness.control_peer);

  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "invalid burst association starts");
  int receives_at_yield = -1;
  int backend_sends_at_yield = -1;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::shared_ptr<RelayState> relay,
                        std::shared_ptr<BackendState> backend, int* receives,
                        int* backend_sends) {
                       *receives = relay->recv_calls;
                       *backend_sends = backend->send_calls;
                     },
                     harness.relay, harness.backend, &receives_at_yield,
                     &backend_sends_at_yield));
  RunUntilIdle();

  Expect(receives_at_yield == 32,
         "relay pump yields after exactly 32 synchronous reads");
  Expect(backend_sends_at_yield == 0,
         "valid packet is not consumed before relay pump yield");
  Expect(harness.backend->send_calls == 1,
         "relay pump recovers and forwards valid packet after invalid burst");
  Expect(harness.relay->sent_packets.empty(),
         "invalid burst never manufactures a client response");
  Expect(harness.relay->recv_calls == 42 && harness.relay->recv_pending,
         "relay pump resumes through remaining packets and rearms receive");
  Expect(harness.completion.calls == 0,
         "invalid burst does not terminate association");
}

void TestSynchronousBackendEchoAndSendErrorStopsReads() {
  AssociationHarness harness;
  BuildAssociation(harness);
  harness.backend->mode = BackendMode::kEchoSynchronously;
  harness.relay->send_results.push_back(net::ERR_FAILED);
  QueueRelayData(harness.relay, ValidPacket(), harness.control_peer);

  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "reentrant send-error association starts");
  RunUntilIdle();

  Expect(harness.backend->send_calls == 1,
         "reentrant backend callback runs during Send");
  Expect(harness.relay->send_calls == 1,
         "reentrant backend callback attempts relay SendTo");
  Expect(harness.completion.calls == 1 &&
             harness.completion.result == net::ERR_FAILED,
         "synchronous SendTo error completes association once");
  Expect(harness.relay->close_calls == 1,
         "synchronous SendTo error closes relay once");
  Expect(harness.relay->recv_calls == 1,
         "synchronous SendTo error does not rearm RecvFrom");
  Expect(harness.relay->recv_after_close_calls == 0,
         "synchronous SendTo error never reads closed relay");
}

void TestSynchronousControlDataYieldsThenEof() {
  AssociationHarness harness;
  BuildAssociation(harness);
  for (int i = 0; i < 33; ++i) {
    QueueControlData(harness.control,
                     {static_cast<uint8_t>(0x80 + (i & 0x0f))});
  }
  QueueControlResult(harness.control, 0);
  const int reads_before_start = harness.control->read_calls;

  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "control-data association starts");
  int reads_at_yield = -1;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::shared_ptr<ControlState> control, int baseline,
                        int* reads_at_yield) {
                       *reads_at_yield = control->read_calls - baseline;
                     },
                     harness.control, reads_before_start, &reads_at_yield));
  RunUntilIdle();

  Expect(reads_at_yield == 32,
         "control pump yields after 32 synchronous data reads");
  Expect(harness.control->read_calls - reads_before_start == 34,
         "control pump resumes and consumes data plus EOF");
  Expect(harness.completion.calls == 1 &&
             harness.completion.result == net::ERR_CONNECTION_CLOSED,
         "synchronous control EOF completes association once");
  Expect(harness.control->disconnects == 1,
         "synchronous control EOF disconnects control socket");
  Expect(harness.relay->close_calls == 1,
         "synchronous control EOF closes relay socket");
}

void TestPendingReadDestructionCancelsCallbacks() {
  AssociationHarness harness;
  BuildAssociation(harness);
  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "pending-read destruction association starts");
  RunUntilIdle();
  Expect(harness.control->read_pending && harness.relay->recv_pending,
         "control and relay reads are pending before destruction");

  harness.association.reset();
  RunUntilIdle();

  Expect(harness.completion.calls == 0,
         "destruction does not invoke association completion");
  Expect(harness.control->pending_read_cancellations == 1 &&
             !harness.control->read_pending,
         "destruction cancels pending control read");
  Expect(harness.relay->pending_recv_cancellations == 1 &&
             !harness.relay->recv_pending,
         "destruction cancels pending relay receive");
  Expect(harness.backend->destructions == 1,
         "destruction releases datagram backend");
}

void TestPendingBackendSendDestructionCancelsCallback() {
  AssociationHarness harness;
  BuildAssociation(harness);
  harness.backend->mode = BackendMode::kPending;
  QueueRelayData(harness.relay, ValidPacket(), harness.control_peer);
  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "pending-backend destruction association starts");
  RunUntilIdle();
  Expect(harness.backend->send_pending,
         "backend completion is pending before destruction");

  harness.association.reset();
  RunUntilIdle();

  Expect(harness.completion.calls == 0,
         "pending backend destruction does not complete association");
  Expect(harness.backend->pending_send_cancellations == 1 &&
             !harness.backend->send_pending,
         "destruction cancels pending backend completion");
  Expect(harness.control->pending_read_cancellations == 1,
         "pending backend destruction also cancels control read");
}

void TestResponseQueuePressure() {
  AssociationHarness harness;
  BuildAssociation(harness);
  QueueRelayData(harness.relay, ValidPacket(), harness.control_peer);
  Expect(StartAssociation(harness) == net::ERR_IO_PENDING,
         "response-pressure association starts");
  RunUntilIdle();
  Expect(harness.backend->receive_callback && harness.relay->recv_pending,
         "response-pressure case learns the client endpoint");

  harness.relay->send_results.push_back(net::ERR_IO_PENDING);
  const net::Socks5UdpDatagram response{
      .destination = net::Socks5UdpEndpoint{
          .type = net::Socks5UdpAddressType::kDomain,
          .host = "response.test",
          .port = 53,
      },
      .payload = {0x42},
  };
  for (int i = 0; i < 66; ++i) {
    harness.backend->receive_callback.Run(response);
  }
  Expect(harness.relay->send_calls == 1 && harness.relay->send_pending,
         "first response write pends while the bounded queue fills");
  const int first_size =
      static_cast<int>(harness.relay->sent_packets.front().data.size());
  CompleteRelaySend(harness.relay, first_size);
  Expect(harness.relay->send_calls == 64 &&
             harness.relay->sent_packets.size() == 64,
         "response queue sends exactly its 64-packet capacity and drops two");
  Expect(harness.completion.calls == 0 && harness.relay->recv_pending,
         "response pressure does not terminate the SOCKS association");
}

}  // namespace

int main() {
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  TestFirstRecvFromCompletesSynchronously();
  TestSynchronousInvalidBurstYieldsAndRecovers();
  TestSynchronousBackendEchoAndSendErrorStopsReads();
  TestSynchronousControlDataYieldsThenEof();
  TestPendingReadDestructionCancelsCallbacks();
  TestPendingBackendSendDestructionCancelsCallback();
  TestResponseQueuePressure();

  if (failures != 0) {
    std::cerr << "M2 G4/G5 deterministic association failures=" << failures
              << "\n";
    return 1;
  }
  std::cout << "M2_G4_G5_UDP_ASSOCIATION_OK\n";
  return 0;
}
