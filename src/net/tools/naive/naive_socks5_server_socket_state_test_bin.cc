// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/containers/extend.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/next_proto.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/socks5_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation(
        "naive_socks5_server_socket_state_test",
        "");

enum class IoMode {
  kSynchronous,
  kAsynchronous,
};

struct ScriptState {
  explicit ScriptState(std::vector<uint8_t> input)
      : input(std::move(input)) {}

  std::vector<uint8_t> input;
  size_t read_offset = 0;
  std::vector<uint8_t> written;
  int read_calls = 0;
  int write_calls = 0;
  int async_read_completions = 0;
  int async_write_completions = 0;
  int disconnects = 0;
};

// A deliberately small scripted transport. Chromium's socket_test_util offers
// MockTCPClientSocket and SequencedSocketData, but that test support library is
// not present as a build target in the minimized Naive checkout. This fake
// preserves the same synchronous/ERR_IO_PENDING contract without pulling the
// full Chromium test dependency graph into the standalone M2 target.
class ScriptedStreamSocket final : public net::StreamSocket {
 public:
  ScriptedStreamSocket(std::shared_ptr<ScriptState> state,
                       IoMode read_mode,
                       IoMode write_mode,
                       size_t max_read,
                       size_t max_write)
      : state_(std::move(state)),
        read_mode_(read_mode),
        write_mode_(write_mode),
        max_read_(max_read),
        max_write_(max_write) {}

  ~ScriptedStreamSocket() override = default;

  void set_read_mode(IoMode mode) { read_mode_ = mode; }
  void set_write_mode(IoMode mode) { write_mode_ = mode; }
  void set_max_read(size_t max_read) { max_read_ = max_read; }
  void set_max_write(size_t max_write) { max_write_ = max_write; }

  int Connect(net::CompletionOnceCallback callback) override {
    connected_ = true;
    return net::OK;
  }

  void Disconnect() override {
    if (connected_) {
      ++state_->disconnects;
    }
    connected_ = false;
    read_pending_ = false;
    write_pending_ = false;
    weak_ptr_factory_.InvalidateWeakPtrs();
  }

  bool IsConnected() const override { return connected_; }

  bool IsConnectedAndIdle() const override {
    return connected_ && !read_pending_ && !write_pending_;
  }

  int GetPeerAddress(net::IPEndPoint* address) const override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    *address = net::IPEndPoint(net::IPAddress::IPv4Localhost(), 49152);
    return net::OK;
  }

  int GetLocalAddress(net::IPEndPoint* address) const override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    *address = net::IPEndPoint(net::IPAddress::IPv4Localhost(), 1080);
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

  int64_t GetTotalReceivedBytes() const override {
    return static_cast<int64_t>(state_->read_offset);
  }

  void ApplySocketTag(const net::SocketTag& tag) override {}

  int Read(net::IOBuffer* buf,
           int buf_len,
           net::CompletionOnceCallback callback) override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    CHECK(!read_pending_);
    ++state_->read_calls;
    const size_t remaining = state_->input.size() - state_->read_offset;
    const size_t count =
        std::min({remaining, static_cast<size_t>(buf_len), max_read_});
    if (read_mode_ == IoMode::kSynchronous) {
      return CompleteRead(base::WrapRefCounted(buf), count);
    }

    read_pending_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ScriptedStreamSocket::CompleteReadAsync,
                       weak_ptr_factory_.GetWeakPtr(), base::WrapRefCounted(buf),
                       count, std::move(callback)));
    return net::ERR_IO_PENDING;
  }

  int Write(net::IOBuffer* buf,
            int buf_len,
            net::CompletionOnceCallback callback,
            const net::NetworkTrafficAnnotationTag& traffic_annotation)
      override {
    if (!connected_) {
      return net::ERR_SOCKET_NOT_CONNECTED;
    }
    CHECK(!write_pending_);
    ++state_->write_calls;
    const size_t count = std::min(static_cast<size_t>(buf_len), max_write_);
    if (write_mode_ == IoMode::kSynchronous) {
      return CompleteWrite(base::WrapRefCounted(buf), count);
    }

    write_pending_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ScriptedStreamSocket::CompleteWriteAsync,
                       weak_ptr_factory_.GetWeakPtr(), base::WrapRefCounted(buf),
                       count, std::move(callback)));
    return net::ERR_IO_PENDING;
  }

  int SetReceiveBufferSize(int32_t size) override { return net::OK; }
  int SetSendBufferSize(int32_t size) override { return net::OK; }

 private:
  int CompleteRead(scoped_refptr<net::IOBuffer> buffer, size_t count) {
    if (count != 0) {
      buffer->first(count).copy_from(
          base::span(state_->input).subspan(state_->read_offset, count));
      state_->read_offset += count;
    }
    return static_cast<int>(count);
  }

  void CompleteReadAsync(scoped_refptr<net::IOBuffer> buffer,
                         size_t count,
                         net::CompletionOnceCallback callback) {
    read_pending_ = false;
    ++state_->async_read_completions;
    const int result = CompleteRead(std::move(buffer), count);
    std::move(callback).Run(result);
  }

  int CompleteWrite(scoped_refptr<net::IOBuffer> buffer, size_t count) {
    base::Extend(state_->written, buffer->first(count));
    return static_cast<int>(count);
  }

  void CompleteWriteAsync(scoped_refptr<net::IOBuffer> buffer,
                          size_t count,
                          net::CompletionOnceCallback callback) {
    write_pending_ = false;
    ++state_->async_write_completions;
    const int result = CompleteWrite(std::move(buffer), count);
    std::move(callback).Run(result);
  }

  const std::shared_ptr<ScriptState> state_;
  IoMode read_mode_;
  IoMode write_mode_;
  size_t max_read_;
  size_t max_write_;
  bool connected_ = true;
  bool read_pending_ = false;
  bool write_pending_ = false;
  net::NetLogWithSource net_log_;
  base::WeakPtrFactory<ScriptedStreamSocket> weak_ptr_factory_{this};
};

struct Harness {
  std::shared_ptr<ScriptState> state;
  ScriptedStreamSocket* transport = nullptr;
  std::unique_ptr<net::Socks5ServerSocket> socket;
};

int failures = 0;

void Expect(bool condition, std::string_view description) {
  if (!condition) {
    std::cerr << "FAILED: " << description << "\n";
    ++failures;
  }
}

std::vector<uint8_t> DomainConnectInput() {
  const std::string_view domain = "example.test";
  std::vector<uint8_t> input = {
      0x05, 0x01, 0x00,  // Greeting: no authentication.
      0x05, 0x01, 0x00, 0x03,
      static_cast<uint8_t>(domain.size()),
  };
  base::Extend(input, base::as_byte_span(domain));
  input.push_back(0x01);
  input.push_back(0xbb);  // Port 443.
  return input;
}

std::vector<uint8_t> ExpectedSuccessWrites() {
  return {
      0x05, 0x00,  // Greeting response.
      0x05, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00,  // BND.ADDR.
      0x00, 0x00,              // BND.PORT.
  };
}

Harness MakeHarness(IoMode read_mode,
                    IoMode write_mode,
                    size_t max_read = std::numeric_limits<size_t>::max(),
                    size_t max_write = std::numeric_limits<size_t>::max()) {
  Harness harness;
  harness.state = std::make_shared<ScriptState>(DomainConnectInput());
  auto transport = std::make_unique<ScriptedStreamSocket>(
      harness.state, read_mode, write_mode, max_read, max_write);
  harness.transport = transport.get();
  harness.socket = std::make_unique<net::Socks5ServerSocket>(
      std::move(transport), "", "", kTrafficAnnotation);
  return harness;
}

template <typename Starter>
int CompleteOperation(Starter starter,
                      bool expect_async,
                      std::string_view description) {
  int callback_count = 0;
  int callback_result = net::ERR_UNEXPECTED;
  int result = starter(base::BindOnce(
      [](int* callback_count, int* callback_result, int result) {
        ++*callback_count;
        *callback_result = result;
      },
      &callback_count, &callback_result));
  Expect((result == net::ERR_IO_PENDING) == expect_async, description);
  if (result == net::ERR_IO_PENDING) {
    base::RunLoop run_loop;
    run_loop.RunUntilIdle();
    Expect(callback_count == 1, description);
    return callback_result;
  }
  Expect(callback_count == 0, description);
  return result;
}

int ReadRequest(Harness& harness,
                bool expect_async,
                std::string_view description) {
  return CompleteOperation(
      [&](net::CompletionOnceCallback callback) {
        return harness.socket->ReadRequest(std::move(callback));
      },
      expect_async, description);
}

int WriteSuccessReply(Harness& harness,
                      bool expect_async,
                      std::string_view description) {
  return CompleteOperation(
      [&](net::CompletionOnceCallback callback) {
        return harness.socket->WriteReply(
            net::Socks5ServerSocket::Reply::kSuccess, net::IPEndPoint(),
            std::move(callback));
      },
      expect_async, description);
}

void ExpectParsedConnect(const Harness& harness,
                         std::string_view description) {
  Expect(harness.socket->request_parsed(), description);
  Expect(harness.socket->command() ==
             net::Socks5ServerSocket::Command::kConnect,
         description);
  Expect(harness.socket->request_endpoint() ==
             net::HostPortPair("example.test", 443),
         description);
}

void TestFullySynchronous() {
  Harness harness =
      MakeHarness(IoMode::kSynchronous, IoMode::kSynchronous);
  Expect(ReadRequest(harness, false, "fully synchronous request") == net::OK,
         "fully synchronous request result");
  ExpectParsedConnect(harness, "fully synchronous parsed request");
  Expect(WriteSuccessReply(harness, false, "fully synchronous reply") ==
             net::OK,
         "fully synchronous reply result");
  Expect(harness.state->written == ExpectedSuccessWrites(),
         "fully synchronous wire bytes");
  Expect(harness.socket->IsConnected(), "fully synchronous connected state");

  int callback_count = 0;
  const int result = harness.socket->Connect(base::BindOnce(
      [](int* callback_count, int) { ++*callback_count; }, &callback_count));
  Expect(result == net::OK, "completed handshake Connect is synchronous");
  Expect(callback_count == 0, "completed handshake Connect skips callback");
}

void TestFullyAsynchronous() {
  Harness harness = MakeHarness(IoMode::kAsynchronous, IoMode::kAsynchronous,
                                std::numeric_limits<size_t>::max(), 2);
  Expect(ReadRequest(harness, true, "fully asynchronous request") == net::OK,
         "fully asynchronous request result");
  ExpectParsedConnect(harness, "fully asynchronous parsed request");
  Expect(WriteSuccessReply(harness, true, "fully asynchronous reply") ==
             net::OK,
         "fully asynchronous reply result");
  Expect(harness.state->written == ExpectedSuccessWrites(),
         "fully asynchronous wire bytes");
  Expect(harness.state->async_read_completions > 0,
         "fully asynchronous read completion observed");
  Expect(harness.state->async_write_completions > 1,
         "asynchronous partial reply writes observed");
}

void TestByteFragmentedCommandAndPartialReply() {
  Harness harness = MakeHarness(IoMode::kSynchronous, IoMode::kSynchronous, 1);
  Expect(ReadRequest(harness, false, "byte-fragmented command") == net::OK,
         "byte-fragmented command result");
  ExpectParsedConnect(harness, "byte-fragmented parsed request");
  Expect(harness.state->read_calls ==
             static_cast<int>(DomainConnectInput().size()),
         "every command byte consumed by an independent read");

  harness.transport->set_max_write(2);
  const int writes_before_reply = harness.state->write_calls;
  Expect(WriteSuccessReply(harness, false, "partial synchronous reply") ==
             net::OK,
         "partial synchronous reply result");
  Expect(harness.state->write_calls - writes_before_reply == 5,
         "ten-byte reply completed in five partial writes");
  Expect(harness.state->written == ExpectedSuccessWrites(),
         "partial synchronous reply wire bytes");
}

void TestPhaseCombinations() {
  {
    Harness harness =
        MakeHarness(IoMode::kAsynchronous, IoMode::kSynchronous);
    Expect(ReadRequest(harness, true, "async request sync reply") == net::OK,
           "async request phase result");
    Expect(WriteSuccessReply(harness, false, "async request sync reply") ==
               net::OK,
           "sync reply phase result");
    Expect(harness.state->written == ExpectedSuccessWrites(),
           "async request sync reply wire bytes");
  }
  {
    Harness harness =
        MakeHarness(IoMode::kSynchronous, IoMode::kSynchronous);
    Expect(ReadRequest(harness, false, "sync request async reply") == net::OK,
           "sync request phase result");
    harness.transport->set_write_mode(IoMode::kAsynchronous);
    Expect(WriteSuccessReply(harness, true, "sync request async reply") ==
               net::OK,
           "async reply phase result");
    Expect(harness.state->written == ExpectedSuccessWrites(),
           "sync request async reply wire bytes");
  }
}

void TestPendingReadDestructionCancelsCallback() {
  Harness harness =
      MakeHarness(IoMode::kAsynchronous, IoMode::kSynchronous);
  int callback_count = 0;
  const int result = harness.socket->ReadRequest(base::BindOnce(
      [](int* callback_count, int) { ++*callback_count; }, &callback_count));
  Expect(result == net::ERR_IO_PENDING,
         "pending read destruction starts asynchronously");
  harness.socket.reset();
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();
  Expect(callback_count == 0, "destroyed pending read cancels callback");
  Expect(harness.state->async_read_completions == 0,
         "destroyed transport suppresses pending read completion");
  Expect(harness.state->disconnects == 1,
         "pending read destruction disconnects transport");
}

void TestPendingWriteDestructionCancelsCallback() {
  Harness harness =
      MakeHarness(IoMode::kSynchronous, IoMode::kSynchronous);
  Expect(ReadRequest(harness, false, "pending write setup") == net::OK,
         "pending write setup result");
  harness.transport->set_write_mode(IoMode::kAsynchronous);
  harness.transport->set_max_write(2);
  int callback_count = 0;
  const int result = harness.socket->WriteReply(
      net::Socks5ServerSocket::Reply::kSuccess, net::IPEndPoint(),
      base::BindOnce(
          [](int* callback_count, int) { ++*callback_count; },
          &callback_count));
  Expect(result == net::ERR_IO_PENDING,
         "pending write destruction starts asynchronously");
  harness.socket.reset();
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();
  Expect(callback_count == 0, "destroyed pending write cancels callback");
  Expect(harness.state->async_write_completions == 0,
         "destroyed transport suppresses pending write completion");
  Expect(harness.state->written == std::vector<uint8_t>({0x05, 0x00}),
         "cancelled reply writes no bytes");
  Expect(harness.state->disconnects == 1,
         "pending write destruction disconnects transport");
}

}  // namespace

int main() {
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  TestFullySynchronous();
  TestFullyAsynchronous();
  TestByteFragmentedCommandAndPartialReply();
  TestPhaseCombinations();
  TestPendingReadDestructionCancelsCallback();
  TestPendingWriteDestructionCancelsCallback();

  if (failures != 0) {
    std::cerr << "M2 G2 deterministic state-machine failures=" << failures
              << "\n";
    return 1;
  }
  std::cout << "M2_G2_DETERMINISTIC_STATE_MACHINE_OK\n";
  return 0;
}
