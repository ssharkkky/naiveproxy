// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_SOCKS5_UDP_ASSOCIATION_H_
#define NET_TOOLS_NAIVE_SOCKS5_UDP_ASSOCIATION_H_

#include <stddef.h>

#include <deque>
#include <memory>
#include <optional>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/ip_endpoint.h"
#include "net/tools/naive/socks5_udp_datagram_backend.h"

namespace net {

class DatagramServerSocket;
class IOBufferWithSize;
class Socks5ServerSocket;

// Owns one RFC 1928 UDP ASSOCIATE control connection and local UDP relay.
// Client datagrams are decoded and passed to an injected backend; backend
// datagrams are re-encoded and sent only to the authenticated client endpoint.
class Socks5UdpAssociation {
 public:
  Socks5UdpAssociation(
      unsigned int id,
      std::unique_ptr<Socks5ServerSocket> control_socket,
      std::unique_ptr<DatagramServerSocket> relay_socket,
      std::unique_ptr<Socks5UdpDatagramBackend> backend,
      const IPEndPoint& control_peer);
  ~Socks5UdpAssociation();

  Socks5UdpAssociation(const Socks5UdpAssociation&) = delete;
  Socks5UdpAssociation& operator=(const Socks5UdpAssociation&) = delete;

  unsigned int id() const { return id_; }
  base::TimeTicks last_activity() const { return last_activity_; }

  // Returns ERR_IO_PENDING while the association is active. The callback is
  // invoked once when the TCP control connection or UDP relay terminates.
  int Start(CompletionOnceCallback callback);

 private:
  struct QueuedResponse {
    QueuedResponse(scoped_refptr<IOBufferWithSize> buffer, int size);
    QueuedResponse(QueuedResponse&&);
    QueuedResponse& operator=(QueuedResponse&&);
    ~QueuedResponse();

    scoped_refptr<IOBufferWithSize> buffer;
    int size;
  };

  void StartPumps();
  void PumpControlReads();
  void RunScheduledControlReadPump();
  int StartControlRead();
  void OnControlReadComplete(int result);
  void PumpRelayReads();
  void RunScheduledRelayReadPump();
  int StartRelayRead();
  void OnRelayReadComplete(int result);
  bool HandleRelayRead(int result);
  bool IsAuthorizedClient(const IPEndPoint& source) const;
  bool HandleBackendSendComplete(int result);
  void OnBackendSendComplete(int result);
  void OnBackendDatagram(Socks5UdpDatagram datagram);
  void PumpRelayWrites();
  void OnRelayWriteComplete(int result);
  void Finish(int result);

  const unsigned int id_;
  std::unique_ptr<Socks5ServerSocket> control_socket_;
  std::unique_ptr<DatagramServerSocket> relay_socket_;
  std::unique_ptr<Socks5UdpDatagramBackend> backend_;

  const IPEndPoint control_peer_;
  const uint16_t requested_client_port_;
  std::optional<IPEndPoint> client_endpoint_;

  scoped_refptr<IOBufferWithSize> control_read_buffer_;
  bool control_read_pending_ = false;
  bool control_read_pump_scheduled_ = false;
  scoped_refptr<IOBufferWithSize> relay_read_buffer_;
  IPEndPoint relay_read_source_;
  bool relay_read_pending_ = false;
  bool relay_read_pump_scheduled_ = false;
  bool backend_send_pending_ = false;
  size_t dropped_fragment_count_ = 0;
  size_t dropped_response_count_ = 0;

  std::deque<QueuedResponse> response_queue_;
  bool relay_write_pending_ = false;

  CompletionOnceCallback completion_callback_;
  bool finished_ = false;
  base::TimeTicks last_activity_ = base::TimeTicks::Now();

  base::WeakPtrFactory<Socks5UdpAssociation> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_SOCKS5_UDP_ASSOCIATION_H_
