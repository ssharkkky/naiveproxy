// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/compiler_specific.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

namespace {

class EchoProbeSession final
    : public quic::MasqueClientSession::EncapsulatedClientSession {
 public:
  explicit EchoProbeSession(std::string expected_payload)
      : expected_payload_(std::move(expected_payload)) {}

  void ProcessPacket(
      absl::string_view packet,
      quic::QuicSocketAddress target_server_address) override {
    received_payload_ = std::string(packet);
    received_from_ = target_server_address.ToString();
    done_ = true;
  }

  void CloseConnection(
      quic::QuicErrorCode error,
      const std::string& details,
      quic::ConnectionCloseBehavior connection_close_behavior) override {
    error_ = absl::StrCat(quic::QuicErrorCodeToString(error), ": ", details);
    done_ = true;
  }

  bool done() const { return done_; }
  bool succeeded() const {
    return error_.empty() && received_payload_ == expected_payload_;
  }
  const std::string& received_payload() const { return received_payload_; }
  const std::string& received_from() const { return received_from_; }
  const std::string& error() const { return error_; }

 private:
  const std::string expected_payload_;
  bool done_ = false;
  std::string received_payload_;
  std::string received_from_;
  std::string error_;
};

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 5) {
    std::cerr << "Usage: naive_masque_probe <proxy-uri-template> "
                 "<target-host> <target-port> <payload>\n";
    return 2;
  }

  const std::string uri_template = UNSAFE_BUFFERS(argv[1]);
  const std::string target_host = UNSAFE_BUFFERS(argv[2]);
  const std::string target_port = UNSAFE_BUFFERS(argv[3]);
  const std::string payload = UNSAFE_BUFFERS(argv[4]);

  uint32_t parsed_port = 0;
  if (!absl::SimpleAtoi(target_port, &parsed_port)) {
    std::cerr << "Invalid target port: " << target_port << "\n";
    return 2;
  }
  if (parsed_port == 0 || parsed_port > 65535) {
    std::cerr << "Target port out of range: " << target_port << "\n";
    return 2;
  }

  quiche::QuicheSystemEventLoop system_event_loop("naive_masque_probe");
  std::unique_ptr<quic::QuicEventLoop> event_loop =
      quic::GetDefaultEventLoop()->Create(quic::QuicDefaultClock::Get());

  EchoProbeSession probe(payload);
  std::unique_ptr<quic::MasqueClient> client = quic::MasqueClient::Create(
      uri_template, quic::MasqueMode::kOpen, event_loop.get(),
      std::make_unique<quic::FakeProofVerifier>(),
      /*proof_source=*/nullptr);
  if (!client) {
    std::cerr << "Failed to connect to MASQUE endpoint\n";
    return 1;
  }

  quic::QuicSocketAddress target =
      quic::tools::LookupAddress(target_host, target_port);
  if (!target.IsInitialized()) {
    std::cerr << "Failed to resolve UDP target " << target_host << ":"
              << target_port << "\n";
    return 1;
  }

  std::cout << "CONNECTED proxy=" << uri_template
            << " target=" << target.ToString() << "\n";
  client->masque_client_session()->SendPacket(payload, target, &probe);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (client->connected() && !probe.done() &&
         std::chrono::steady_clock::now() < deadline) {
    event_loop->RunEventLoopOnce(quic::QuicTime::Delta::FromMilliseconds(50));
  }

  if (!probe.done()) {
    std::cerr << "Timed out waiting for UDP echo\n";
    return 1;
  }
  if (!probe.succeeded()) {
    std::cerr << "Probe failed";
    if (!probe.error().empty()) {
      std::cerr << ": " << probe.error();
    } else {
      std::cerr << ": payload mismatch, received=" << probe.received_payload();
    }
    std::cerr << "\n";
    return 1;
  }

  std::cout << "DATAGRAM_ECHO_OK from=" << probe.received_from()
            << " bytes=" << probe.received_payload().size()
            << " payload=" << probe.received_payload() << "\n";
  return 0;
}
