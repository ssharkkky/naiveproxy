// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/socks5_udp_association.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/socket/datagram_server_socket.h"
#include "net/tools/naive/socks5_server_socket.h"

namespace net {
namespace {

constexpr int kControlReadBufferSize = 1024;
constexpr int kMaxUdpPacketSize = 64 * 1024;
constexpr size_t kMaxQueuedResponses = 64;
constexpr int kMaxSynchronousReadsPerPump = 32;

IPAddress NormalizeAddress(const IPAddress& address) {
  return address.IsIPv4MappedIPv6() ? ConvertIPv4MappedIPv6ToIPv4(address)
                                    : address;
}

uint16_t RequestedClientPort(
    const std::unique_ptr<Socks5ServerSocket>& control_socket) {
  CHECK(control_socket);
  return control_socket->request_endpoint().port();
}

// Per-datagram relay failures (ICMP port-unreachable, oversize). Do not
// Finish() the whole association.
bool IsRecoverableRelayError(int result) {
  switch (result) {
    case ERR_CONNECTION_RESET:
    case ERR_MSG_TOO_BIG:
    case ERR_ADDRESS_UNREACHABLE:
      return true;
    default:
      return false;
  }
}

}  // namespace

Socks5UdpAssociation::QueuedResponse::QueuedResponse(
    scoped_refptr<IOBufferWithSize> buffer,
    int size)
    : buffer(std::move(buffer)), size(size) {}
Socks5UdpAssociation::QueuedResponse::QueuedResponse(QueuedResponse&&) =
    default;
Socks5UdpAssociation::QueuedResponse&
Socks5UdpAssociation::QueuedResponse::operator=(QueuedResponse&&) = default;
Socks5UdpAssociation::QueuedResponse::~QueuedResponse() = default;

Socks5UdpAssociation::Socks5UdpAssociation(
    unsigned int id,
    std::unique_ptr<Socks5ServerSocket> control_socket,
    std::unique_ptr<DatagramServerSocket> relay_socket,
    std::unique_ptr<Socks5UdpDatagramBackend> backend,
    const IPEndPoint& control_peer)
    : id_(id),
      control_socket_(std::move(control_socket)),
      relay_socket_(std::move(relay_socket)),
      backend_(std::move(backend)),
      control_peer_(control_peer),
      requested_client_port_(RequestedClientPort(control_socket_)) {
  CHECK(control_socket_);
  CHECK(relay_socket_);
  CHECK(backend_);
}

Socks5UdpAssociation::~Socks5UdpAssociation() = default;

bool Socks5UdpAssociation::ShouldExpire(base::TimeTicks now,
                                        base::TimeDelta idle_timeout,
                                        base::TimeDelta tunnel_timeout) const {
  return now - last_activity_ > idle_timeout ||
         now - created_at_ > tunnel_timeout;
}

int Socks5UdpAssociation::Start(CompletionOnceCallback callback) {
  CHECK(!completion_callback_);
  completion_callback_ = std::move(callback);
  backend_->Start(base::BindRepeating(
      &Socks5UdpAssociation::OnBackendDatagram,
      weak_ptr_factory_.GetWeakPtr()));
  // Always start the pumps from a later task. Besides making Start()'s
  // completion contract unambiguous, this lets NaiveProxy finish installing
  // the association owner before a synchronously-ready socket can terminate
  // it.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&Socks5UdpAssociation::StartPumps,
                     weak_ptr_factory_.GetWeakPtr()));
  return ERR_IO_PENDING;
}

void Socks5UdpAssociation::StartPumps() {
  if (finished_) {
    return;
  }
  PumpControlReads();
  PumpRelayReads();
}

void Socks5UdpAssociation::PumpControlReads() {
  if (finished_ || control_read_pending_ || control_read_pump_scheduled_) {
    return;
  }
  for (int i = 0; i < kMaxSynchronousReadsPerPump; ++i) {
    const int result = StartControlRead();
    if (result == ERR_IO_PENDING) {
      control_read_pending_ = true;
      return;
    }
    if (result <= 0) {
      Finish(result == 0 ? ERR_CONNECTION_CLOSED : result);
      return;
    }
    last_activity_ = base::TimeTicks::Now();
  }
  control_read_pump_scheduled_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&Socks5UdpAssociation::RunScheduledControlReadPump,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Socks5UdpAssociation::RunScheduledControlReadPump() {
  control_read_pump_scheduled_ = false;
  PumpControlReads();
}

int Socks5UdpAssociation::StartControlRead() {
  control_read_buffer_ =
      base::MakeRefCounted<IOBufferWithSize>(kControlReadBufferSize);
  return control_socket_->Read(
      control_read_buffer_.get(), control_read_buffer_->size(),
      base::BindOnce(&Socks5UdpAssociation::OnControlReadComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Socks5UdpAssociation::OnControlReadComplete(int result) {
  control_read_pending_ = false;
  if (finished_) {
    return;
  }
  if (result <= 0) {
    Finish(result == 0 ? ERR_CONNECTION_CLOSED : result);
    return;
  }
  last_activity_ = base::TimeTicks::Now();
  PumpControlReads();
}

void Socks5UdpAssociation::PumpRelayReads() {
  if (finished_ || relay_read_pending_ || relay_read_pump_scheduled_ ||
      backend_send_pending_) {
    return;
  }
  for (int i = 0; i < kMaxSynchronousReadsPerPump; ++i) {
    const int result = StartRelayRead();
    if (result == ERR_IO_PENDING) {
      relay_read_pending_ = true;
      return;
    }
    if (!HandleRelayRead(result)) {
      return;
    }
  }
  relay_read_pump_scheduled_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&Socks5UdpAssociation::RunScheduledRelayReadPump,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Socks5UdpAssociation::RunScheduledRelayReadPump() {
  relay_read_pump_scheduled_ = false;
  PumpRelayReads();
}

int Socks5UdpAssociation::StartRelayRead() {
  relay_read_buffer_ =
      base::MakeRefCounted<IOBufferWithSize>(kMaxUdpPacketSize);
  return relay_socket_->RecvFrom(
      relay_read_buffer_.get(), relay_read_buffer_->size(), &relay_read_source_,
      base::BindOnce(&Socks5UdpAssociation::OnRelayReadComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Socks5UdpAssociation::OnRelayReadComplete(int result) {
  relay_read_pending_ = false;
  if (finished_) {
    return;
  }
  if (HandleRelayRead(result)) {
    PumpRelayReads();
  }
}

bool Socks5UdpAssociation::HandleRelayRead(int result) {
  if (result < 0) {
    if (IsRecoverableRelayError(result)) {
      return true;
    }
    Finish(result);
    return false;
  }
  if (result == 0) {
    return true;
  }
  if (!IsAuthorizedClient(relay_read_source_)) {
    VLOG(1) << "SOCKS5 UDP dropped packet from unauthorized client";
    return true;
  }

  auto datagram = ParseSocks5UdpDatagram(
      relay_read_buffer_->span().first(static_cast<size_t>(result)));
  if (!datagram.has_value()) {
    if (datagram.error() == Socks5UdpCodecError::kFragmentUnsupported) {
      ++dropped_fragment_count_;
      // Log on powers of two so a hostile sender cannot flood the log while
      // the cumulative drop count remains observable.
      if ((dropped_fragment_count_ & (dropped_fragment_count_ - 1)) == 0) {
        LOG(WARNING) << "SOCKS5 UDP dropped unsupported fragmented datagrams: "
                     << dropped_fragment_count_;
      }
    } else {
      VLOG(1) << "SOCKS5 UDP dropped malformed datagram: "
              << Socks5UdpCodecErrorToString(datagram.error());
    }
    return true;
  }

  // Learn a wildcard request's source port only after both source-IP and
  // packet validation succeed.
  if (!client_endpoint_.has_value()) {
    client_endpoint_ = relay_read_source_;
  }
  last_activity_ = base::TimeTicks::Now();
  backend_send_pending_ = true;
  const int send_result = backend_->Send(
      std::move(*datagram),
      base::BindOnce(&Socks5UdpAssociation::OnBackendSendComplete,
                     weak_ptr_factory_.GetWeakPtr()));
  if (finished_) {
    backend_send_pending_ = false;
    return false;
  }
  return send_result == ERR_IO_PENDING
             ? false
             : HandleBackendSendComplete(send_result);
}

bool Socks5UdpAssociation::IsAuthorizedClient(
    const IPEndPoint& source) const {
  if (NormalizeAddress(source.address()) !=
      NormalizeAddress(control_peer_.address())) {
    return false;
  }
  if (client_endpoint_.has_value()) {
    return source == *client_endpoint_;
  }
  return requested_client_port_ == 0 ||
         requested_client_port_ == source.port();
}

bool Socks5UdpAssociation::HandleBackendSendComplete(int result) {
  backend_send_pending_ = false;
  if (finished_) {
    return false;
  }
  if (result != OK) {
    Finish(result);
    return false;
  }
  return true;
}

void Socks5UdpAssociation::OnBackendSendComplete(int result) {
  if (HandleBackendSendComplete(result)) {
    PumpRelayReads();
  }
}

void Socks5UdpAssociation::OnBackendDatagram(Socks5UdpDatagram datagram) {
  if (finished_ || !client_endpoint_.has_value()) {
    return;
  }
  auto packet = BuildSocks5UdpDatagram(datagram);
  if (!packet.has_value()) {
    VLOG(1) << "SOCKS5 UDP backend produced invalid endpoint: "
            << Socks5UdpCodecErrorToString(packet.error());
    return;
  }
  if (response_queue_.size() >= kMaxQueuedResponses) {
    ++dropped_response_count_;
    if ((dropped_response_count_ & (dropped_response_count_ - 1)) == 0) {
      LOG(WARNING) << "SOCKS5 UDP response queue full; dropped datagrams: "
                   << dropped_response_count_;
    }
    return;
  }
  auto buffer =
      base::MakeRefCounted<IOBufferWithSize>(static_cast<int>(packet->size()));
  buffer->span().copy_from(*packet);
  response_queue_.emplace_back(std::move(buffer),
                               static_cast<int>(packet->size()));
  PumpRelayWrites();
}

void Socks5UdpAssociation::PumpRelayWrites() {
  if (finished_ || relay_write_pending_ || response_queue_.empty()) {
    return;
  }
  QueuedResponse& response = response_queue_.front();
  const int result = relay_socket_->SendTo(
      response.buffer.get(), response.size, *client_endpoint_,
      base::BindOnce(&Socks5UdpAssociation::OnRelayWriteComplete,
                     weak_ptr_factory_.GetWeakPtr()));
  if (result == ERR_IO_PENDING) {
    relay_write_pending_ = true;
    return;
  }
  OnRelayWriteComplete(result);
}

void Socks5UdpAssociation::OnRelayWriteComplete(int result) {
  relay_write_pending_ = false;
  if (result < 0) {
    if (IsRecoverableRelayError(result)) {
      CHECK(!response_queue_.empty());
      response_queue_.pop_front();
      PumpRelayWrites();
      return;
    }
    Finish(result);
    return;
  }
  CHECK(!response_queue_.empty());
  if (result != response_queue_.front().size) {
    Finish(ERR_FAILED);
    return;
  }
  response_queue_.pop_front();
  last_activity_ = base::TimeTicks::Now();
  PumpRelayWrites();
}

void Socks5UdpAssociation::Finish(int result) {
  if (finished_) {
    return;
  }
  finished_ = true;
  weak_ptr_factory_.InvalidateWeakPtrs();
  relay_socket_->Close();
  control_socket_->Disconnect();
  if (completion_callback_) {
    std::move(completion_callback_).Run(result);
  }
}

}  // namespace net
