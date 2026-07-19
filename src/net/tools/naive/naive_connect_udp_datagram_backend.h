// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "net/tools/naive/socks5_udp_datagram_backend.h"

namespace net {

class IOBuffer;

// Narrow, fakeable boundary around one fixed-target CONNECT-UDP tunnel. Tests
// script this interface; the production implementation added in M3-G3 wraps
// NaiveConnectUdpTunnel without virtualizing Chromium session-pool internals.
class NaiveConnectUdpTargetTunnel {
 public:
  virtual ~NaiveConnectUdpTargetTunnel() = default;

  virtual int Start(CompletionOnceCallback callback) = 0;
  virtual int Read(IOBuffer* buffer,
                   int buffer_length,
                   CompletionOnceCallback callback) = 0;
  virtual int Write(IOBuffer* buffer,
                    int buffer_length,
                    CompletionOnceCallback callback) = 0;

  // `LastReadWasDatagram()` disambiguates a valid zero-length UDP datagram
  // from a stream-close result of zero.
  virtual bool IsOpen() const = 0;
  virtual bool LastReadWasDatagram() const = 0;
  virtual size_t MaxPayloadSize() const = 0;
};

using NaiveConnectUdpTargetTunnelFactory =
    base::RepeatingCallback<std::unique_ptr<NaiveConnectUdpTargetTunnel>(
        const Socks5UdpBackendContext& context,
        const Socks5UdpEndpoint& target)>;

// Association-level owner that will map SOCKS targets to fixed-target
// CONNECT-UDP tunnels. G0 establishes the immutable context and injectable
// tunnel seam; G1/G2 fill in the state machine and target map.
class NaiveConnectUdpDatagramBackend final
    : public Socks5UdpDatagramBackend {
 public:
  struct Stats {
    uint64_t admitted_datagrams = 0;
    uint64_t sent_datagrams = 0;
    uint64_t received_datagrams = 0;
    uint64_t oversize_drops = 0;
    uint64_t capacity_drops = 0;
    uint64_t cooldown_drops = 0;
    uint64_t target_failures = 0;
    uint64_t connect_timeouts = 0;
    uint64_t idle_evictions = 0;
  };

  NaiveConnectUdpDatagramBackend(
      Socks5UdpBackendContext context,
      NaiveConnectUdpTargetTunnelFactory tunnel_factory,
      base::TimeDelta failed_target_cooldown =
          Socks5UdpBackendLimits::kFailedTargetCooldown);
  ~NaiveConnectUdpDatagramBackend() override;

  NaiveConnectUdpDatagramBackend(const NaiveConnectUdpDatagramBackend&) =
      delete;
  NaiveConnectUdpDatagramBackend& operator=(
      const NaiveConnectUdpDatagramBackend&) = delete;

  void Start(ReceiveCallback receive_callback) override;
  int Send(Socks5UdpDatagram datagram,
           CompletionOnceCallback callback) override;

  const Stats& stats_for_testing() const { return stats_; }
  size_t target_count_for_testing() const { return targets_.size(); }

 private:
  struct TargetEntry;

  TargetEntry* FindTarget(const Socks5UdpTargetKey& key,
                          uint64_t generation);
  void StartTarget(const Socks5UdpTargetKey& key, uint64_t generation);
  void OnTargetConnectComplete(Socks5UdpTargetKey key,
                               uint64_t generation,
                               int result);
  void HandleTargetConnectComplete(const Socks5UdpTargetKey& key,
                                   uint64_t generation,
                                   int result);
  void OnTargetConnectTimeout(Socks5UdpTargetKey key, uint64_t generation);
  void PumpTargetReads(const Socks5UdpTargetKey& key, uint64_t generation);
  void RunScheduledReadPump(Socks5UdpTargetKey key, uint64_t generation);
  void OnTargetReadComplete(Socks5UdpTargetKey key,
                            uint64_t generation,
                            int result);
  bool HandleTargetReadComplete(const Socks5UdpTargetKey& key,
                                uint64_t generation,
                                int result);
  void PumpTargetWrites(const Socks5UdpTargetKey& key, uint64_t generation);
  void RunScheduledWritePump(Socks5UdpTargetKey key, uint64_t generation);
  void OnTargetWriteComplete(Socks5UdpTargetKey key,
                             uint64_t generation,
                             int result);
  bool HandleTargetWriteComplete(const Socks5UdpTargetKey& key,
                                 uint64_t generation,
                                 int result);
  void ArmTargetIdleTimer(const Socks5UdpTargetKey& key,
                          uint64_t generation);
  void OnTargetIdleTimeout(Socks5UdpTargetKey key, uint64_t generation);
  void ScheduleTargetRetirement(const Socks5UdpTargetKey& key,
                                uint64_t generation,
                                int error,
                                bool enter_cooldown = true);
  void RetireTarget(Socks5UdpTargetKey key,
                    uint64_t generation,
                    bool enter_cooldown);
  void EraseCooldownTarget(Socks5UdpTargetKey key, uint64_t generation);
  void ClearQueuedDatagrams(TargetEntry* entry);
  void RecordCounterEvent(std::string_view reason, uint64_t count) const;

  const Socks5UdpBackendContext context_;
  const NaiveConnectUdpTargetTunnelFactory tunnel_factory_;
  const base::TimeDelta failed_target_cooldown_;
  ReceiveCallback receive_callback_;
  std::map<Socks5UdpTargetKey, std::unique_ptr<TargetEntry>> targets_;
  size_t queued_datagram_count_ = 0;
  size_t queued_payload_bytes_ = 0;
  uint64_t next_generation_ = 1;
  Stats stats_;

  base::WeakPtrFactory<NaiveConnectUdpDatagramBackend> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_
