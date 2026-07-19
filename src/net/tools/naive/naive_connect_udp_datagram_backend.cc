// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connect_udp_datagram_backend.h"

#include <algorithm>
#include <deque>
#include <vector>
#include <utility>

#include "base/functional/bind.h"
#include "base/check.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"

namespace net {
namespace {

constexpr int kReadBufferSize = 64 * 1024;

}  // namespace

struct NaiveConnectUdpDatagramBackend::TargetEntry {
  enum class State {
    kConnecting,
    kOpen,
    kRetiring,
  };

  TargetEntry(Socks5UdpEndpoint endpoint,
              uint64_t generation,
              std::unique_ptr<NaiveConnectUdpTargetTunnel> tunnel)
      : endpoint(std::move(endpoint)),
        generation(generation),
        tunnel(std::move(tunnel)) {}
  ~TargetEntry() = default;

  Socks5UdpEndpoint endpoint;
  uint64_t generation;
  State state = State::kConnecting;
  std::deque<std::vector<uint8_t>> outbound_queue;
  scoped_refptr<IOBufferWithSize> write_buffer;
  bool write_pending = false;
  bool write_pump_scheduled = false;
  scoped_refptr<IOBufferWithSize> read_buffer;
  bool read_pending = false;
  bool read_pump_scheduled = false;
  // Must be destroyed before pending I/O buffers. The tunnel destructor
  // cancels callbacks that may still reference those buffers.
  std::unique_ptr<NaiveConnectUdpTargetTunnel> tunnel;
};

NaiveConnectUdpDatagramBackend::NaiveConnectUdpDatagramBackend(
    Socks5UdpBackendContext context,
    NaiveConnectUdpTargetTunnelFactory tunnel_factory)
    : context_(std::move(context)),
      tunnel_factory_(std::move(tunnel_factory)) {
  CHECK(context_.session);
  CHECK(tunnel_factory_);
}

NaiveConnectUdpDatagramBackend::~NaiveConnectUdpDatagramBackend() = default;

void NaiveConnectUdpDatagramBackend::Start(
    ReceiveCallback receive_callback) {
  CHECK(!receive_callback_);
  CHECK(receive_callback);
  receive_callback_ = std::move(receive_callback);
}

int NaiveConnectUdpDatagramBackend::Send(
    Socks5UdpDatagram datagram,
    CompletionOnceCallback callback) {
  if (!receive_callback_) {
    return ERR_UNEXPECTED;
  }
  const Socks5UdpTargetKey key(datagram.destination);
  auto it = targets_.find(key);
  if (it == targets_.end()) {
    std::unique_ptr<NaiveConnectUdpTargetTunnel> tunnel =
        tunnel_factory_.Run(context_, datagram.destination);
    if (!tunnel) {
      ++stats_.target_failures;
      return OK;
    }
    const uint64_t generation = next_generation_++;
    auto entry = std::make_unique<TargetEntry>(
        datagram.destination, generation, std::move(tunnel));
    it = targets_.emplace(key, std::move(entry)).first;
  }

  TargetEntry* entry = it->second.get();
  if (entry->state == TargetEntry::State::kRetiring) {
    ++stats_.capacity_drops;
    return OK;
  }
  if (entry->outbound_queue.size() >=
          Socks5UdpBackendLimits::kMaxQueuedDatagramsPerTarget ||
      queued_datagram_count_ >=
          Socks5UdpBackendLimits::kMaxQueuedDatagramsPerAssociation ||
      datagram.payload.size() >
          Socks5UdpBackendLimits::kMaxQueuedPayloadBytesPerAssociation -
              std::min(queued_payload_bytes_,
                       Socks5UdpBackendLimits::
                           kMaxQueuedPayloadBytesPerAssociation)) {
    ++stats_.capacity_drops;
    return OK;
  }

  queued_payload_bytes_ += datagram.payload.size();
  ++queued_datagram_count_;
  ++stats_.admitted_datagrams;
  entry->outbound_queue.push_back(std::move(datagram.payload));
  const uint64_t generation = entry->generation;
  if (entry->state == TargetEntry::State::kConnecting &&
      entry->outbound_queue.size() == 1) {
    StartTarget(key, generation);
  } else if (entry->state == TargetEntry::State::kOpen) {
    PumpTargetWrites(key, generation);
  }
  return OK;
}

NaiveConnectUdpDatagramBackend::TargetEntry*
NaiveConnectUdpDatagramBackend::FindTarget(const Socks5UdpTargetKey& key,
                                           uint64_t generation) {
  auto it = targets_.find(key);
  if (it == targets_.end() || it->second->generation != generation) {
    return nullptr;
  }
  return it->second.get();
}

void NaiveConnectUdpDatagramBackend::StartTarget(
    const Socks5UdpTargetKey& key,
    uint64_t generation) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kConnecting) {
    return;
  }
  const int result = entry->tunnel->Start(base::BindOnce(
      &NaiveConnectUdpDatagramBackend::OnTargetConnectComplete,
      weak_ptr_factory_.GetWeakPtr(), key, generation));
  if (result != ERR_IO_PENDING) {
    HandleTargetConnectComplete(key, generation, result);
  }
}

void NaiveConnectUdpDatagramBackend::OnTargetConnectComplete(
    Socks5UdpTargetKey key,
    uint64_t generation,
    int result) {
  HandleTargetConnectComplete(key, generation, result);
}

void NaiveConnectUdpDatagramBackend::HandleTargetConnectComplete(
    const Socks5UdpTargetKey& key,
    uint64_t generation,
    int result) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kConnecting) {
    return;
  }
  if (result != OK || !entry->tunnel->IsOpen() ||
      entry->tunnel->MaxPayloadSize() == 0) {
    ScheduleTargetRetirement(
        key, generation, result == OK ? ERR_CONNECTION_CLOSED : result);
    return;
  }
  entry->state = TargetEntry::State::kOpen;
  base::WeakPtr<NaiveConnectUdpDatagramBackend> self =
      weak_ptr_factory_.GetWeakPtr();
  PumpTargetReads(key, generation);
  if (!self) {
    return;
  }
  self->PumpTargetWrites(key, generation);
}

void NaiveConnectUdpDatagramBackend::PumpTargetReads(
    const Socks5UdpTargetKey& key,
    uint64_t generation) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen ||
      entry->read_pending || entry->read_pump_scheduled) {
    return;
  }
  for (size_t operation = 0;
       operation < Socks5UdpBackendLimits::kMaxSynchronousPumpOperations;
       ++operation) {
    entry = FindTarget(key, generation);
    if (!entry || entry->state != TargetEntry::State::kOpen) {
      return;
    }
    entry->read_buffer =
        base::MakeRefCounted<IOBufferWithSize>(kReadBufferSize);
    const int result = entry->tunnel->Read(
        entry->read_buffer.get(), entry->read_buffer->size(),
        base::BindOnce(
            &NaiveConnectUdpDatagramBackend::OnTargetReadComplete,
            weak_ptr_factory_.GetWeakPtr(), key, generation));
    if (result == ERR_IO_PENDING) {
      entry->read_pending = true;
      return;
    }
    if (!HandleTargetReadComplete(key, generation, result)) {
      return;
    }
  }
  entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen) {
    return;
  }
  entry->read_pump_scheduled = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &NaiveConnectUdpDatagramBackend::RunScheduledReadPump,
          weak_ptr_factory_.GetWeakPtr(), key, generation));
}

void NaiveConnectUdpDatagramBackend::RunScheduledReadPump(
    Socks5UdpTargetKey key,
    uint64_t generation) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry) {
    return;
  }
  entry->read_pump_scheduled = false;
  PumpTargetReads(key, generation);
}

void NaiveConnectUdpDatagramBackend::OnTargetReadComplete(
    Socks5UdpTargetKey key,
    uint64_t generation,
    int result) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry) {
    return;
  }
  entry->read_pending = false;
  if (HandleTargetReadComplete(key, generation, result)) {
    PumpTargetReads(key, generation);
  }
}

bool NaiveConnectUdpDatagramBackend::HandleTargetReadComplete(
    const Socks5UdpTargetKey& key,
    uint64_t generation,
    int result) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen) {
    return false;
  }
  if (result < 0 || (result == 0 && !entry->tunnel->LastReadWasDatagram()) ||
      result > entry->read_buffer->size()) {
    ScheduleTargetRetirement(
        key, generation,
        result < 0 ? result : ERR_CONNECTION_CLOSED);
    return false;
  }

  Socks5UdpDatagram datagram;
  datagram.destination = entry->endpoint;
  datagram.payload.assign(entry->read_buffer->span().begin(),
                          entry->read_buffer->span().begin() + result);
  entry->read_buffer.reset();
  ++stats_.received_datagrams;
  base::WeakPtr<NaiveConnectUdpDatagramBackend> self =
      weak_ptr_factory_.GetWeakPtr();
  receive_callback_.Run(std::move(datagram));
  return !!self;
}

void NaiveConnectUdpDatagramBackend::PumpTargetWrites(
    const Socks5UdpTargetKey& key,
    uint64_t generation) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen ||
      entry->write_pending || entry->write_pump_scheduled) {
    return;
  }
  for (size_t operation = 0;
       operation < Socks5UdpBackendLimits::kMaxSynchronousPumpOperations;
       ++operation) {
    entry = FindTarget(key, generation);
    if (!entry || entry->state != TargetEntry::State::kOpen ||
        entry->outbound_queue.empty()) {
      return;
    }
    const std::vector<uint8_t>& payload = entry->outbound_queue.front();
    if (payload.size() > entry->tunnel->MaxPayloadSize()) {
      queued_payload_bytes_ -= payload.size();
      --queued_datagram_count_;
      entry->outbound_queue.pop_front();
      ++stats_.oversize_drops;
      continue;
    }
    entry->write_buffer = base::MakeRefCounted<IOBufferWithSize>(
        static_cast<int>(std::max<size_t>(payload.size(), 1)));
    if (!payload.empty()) {
      entry->write_buffer->span().first(payload.size()).copy_from(payload);
    }
    const int result = entry->tunnel->Write(
        entry->write_buffer.get(), static_cast<int>(payload.size()),
        base::BindOnce(
            &NaiveConnectUdpDatagramBackend::OnTargetWriteComplete,
            weak_ptr_factory_.GetWeakPtr(), key, generation));
    if (result == ERR_IO_PENDING) {
      entry->write_pending = true;
      return;
    }
    if (!HandleTargetWriteComplete(key, generation, result)) {
      return;
    }
  }
  entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen ||
      entry->outbound_queue.empty()) {
    return;
  }
  entry->write_pump_scheduled = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &NaiveConnectUdpDatagramBackend::RunScheduledWritePump,
          weak_ptr_factory_.GetWeakPtr(), key, generation));
}

void NaiveConnectUdpDatagramBackend::RunScheduledWritePump(
    Socks5UdpTargetKey key,
    uint64_t generation) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry) {
    return;
  }
  entry->write_pump_scheduled = false;
  PumpTargetWrites(key, generation);
}

void NaiveConnectUdpDatagramBackend::OnTargetWriteComplete(
    Socks5UdpTargetKey key,
    uint64_t generation,
    int result) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry) {
    return;
  }
  entry->write_pending = false;
  if (HandleTargetWriteComplete(key, generation, result)) {
    PumpTargetWrites(key, generation);
  }
}

bool NaiveConnectUdpDatagramBackend::HandleTargetWriteComplete(
    const Socks5UdpTargetKey& key,
    uint64_t generation,
    int result) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state != TargetEntry::State::kOpen ||
      entry->outbound_queue.empty()) {
    return false;
  }
  const size_t expected = entry->outbound_queue.front().size();
  entry->write_buffer.reset();
  if (result < 0 || static_cast<size_t>(result) != expected) {
    ScheduleTargetRetirement(
        key, generation, result < 0 ? result : ERR_FAILED);
    return false;
  }
  queued_payload_bytes_ -= expected;
  --queued_datagram_count_;
  entry->outbound_queue.pop_front();
  ++stats_.sent_datagrams;
  return true;
}

void NaiveConnectUdpDatagramBackend::ScheduleTargetRetirement(
    const Socks5UdpTargetKey& key,
    uint64_t generation,
    int error) {
  TargetEntry* entry = FindTarget(key, generation);
  if (!entry || entry->state == TargetEntry::State::kRetiring) {
    return;
  }
  entry->state = TargetEntry::State::kRetiring;
  ClearQueuedDatagrams(entry);
  ++stats_.target_failures;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&NaiveConnectUdpDatagramBackend::RetireTarget,
                     weak_ptr_factory_.GetWeakPtr(), key, generation));
}

void NaiveConnectUdpDatagramBackend::RetireTarget(
    Socks5UdpTargetKey key,
    uint64_t generation) {
  auto it = targets_.find(key);
  if (it == targets_.end() || it->second->generation != generation) {
    return;
  }
  targets_.erase(it);
}

void NaiveConnectUdpDatagramBackend::ClearQueuedDatagrams(
    TargetEntry* entry) {
  for (const std::vector<uint8_t>& payload : entry->outbound_queue) {
    queued_payload_bytes_ -= payload.size();
    --queued_datagram_count_;
  }
  entry->outbound_queue.clear();
}

}  // namespace net
