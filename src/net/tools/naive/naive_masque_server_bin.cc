// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/escaping.h"
#include "quiche/quic/masque/masque_server.h"
#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, port, 9661,
                                "UDP port for the controlled MASQUE server.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, cache_dir, "",
    "Optional response-cache directory for non-MASQUE requests.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, server_authority, "",
    "Only accept MASQUE requests for this authority.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, masque_mode, "open",
    "MASQUE mode. The controlled M1 endpoint only accepts open.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, basic_user, "",
    "Optional Basic proxy-auth username for the controlled endpoint.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, basic_pass, "",
    "Optional Basic proxy-auth password for the controlled endpoint.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, ignore_connect_requests, false,
    "Leave CONNECT requests pending for lifecycle testing.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, fail_connects, false,
    "Respond 502 (delayed 500 ms, without FIN) to every CONNECT request, to "
    "exercise async CONNECT failure with or without Fast Open early data.");

namespace {

std::string_view HeaderValue(const quiche::HttpHeaderBlock& headers,
                             std::string_view name) {
  auto it = headers.find(name);
  return it == headers.end() ? std::string_view() : it->second;
}

class LoggingMasqueServerBackend final : public quic::MasqueServerBackend {
 public:
  LoggingMasqueServerBackend(quic::MasqueMode mode,
                             const std::string& server_authority,
                             const std::string& cache_directory,
                             std::string expected_proxy_authorization,
                             bool ignore_connect_requests,
                             bool fail_connects)
      : quic::MasqueServerBackend(mode, server_authority, cache_directory),
        expected_proxy_authorization_(
            std::move(expected_proxy_authorization)),
        ignore_connect_requests_(ignore_connect_requests),
        fail_connects_(fail_connects) {}

  void HandleConnectHeaders(
      const quiche::HttpHeaderBlock& request_headers,
      RequestHandler* request_handler) override {
    std::cout << "CONNECT_HEADERS"
              << " method=" << HeaderValue(request_headers, ":method")
              << " protocol=" << HeaderValue(request_headers, ":protocol")
              << " scheme=" << HeaderValue(request_headers, ":scheme")
              << " authority=" << HeaderValue(request_headers, ":authority")
              << " path=" << HeaderValue(request_headers, ":path")
              << " capsule_protocol="
              << HeaderValue(request_headers, "capsule-protocol")
              << " proxy_authorization="
              << (request_headers.contains("proxy-authorization") ? "present"
                                                                   : "absent")
              << std::endl;

    if (ignore_connect_requests_) {
      std::cout << "CONNECT_ACTION ignored" << std::endl;
      return;
    }

    if (fail_connects_) {
      std::cout << "CONNECT_ACTION fail_502" << std::endl;
      quiche::HttpHeaderBlock headers;
      headers[":status"] = "502";
      headers["content-type"] = "text/plain";
      quic::QuicBackendResponse response;
      response.set_headers(std::move(headers));
      // Deliver the failed CONNECT response without FIN so the stream stays
      // open: a client must not leave pending application I/O (Fast Open
      // early data or a data read issued before the response) waiting for a
      // close that never comes.
      response.set_response_type(
          quic::QuicBackendResponse::INCOMPLETE_RESPONSE);
      response.set_delay(quic::QuicTime::Delta::FromMilliseconds(500));
      request_handler->OnResponseBackendComplete(&response);
      return;
    }

    if (!expected_proxy_authorization_.empty() &&
        HeaderValue(request_headers, "proxy-authorization") !=
            expected_proxy_authorization_) {
      std::cout << "AUTH_DECISION rejected" << std::endl;
      quiche::HttpHeaderBlock headers;
      headers[":status"] = "407";
      headers["proxy-authenticate"] = "Basic realm=\"naive-m1\"";
      quic::QuicBackendResponse response;
      response.set_headers(std::move(headers));
      request_handler->OnResponseBackendComplete(&response);
      return;
    }

    if (!expected_proxy_authorization_.empty()) {
      std::cout << "AUTH_DECISION accepted" << std::endl;
    }
    quic::MasqueServerBackend::HandleConnectHeaders(request_headers,
                                                     request_handler);
  }

 private:
  const std::string expected_proxy_authorization_;
  const bool ignore_connect_requests_;
  const bool fail_connects_;
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* usage = "Usage: naive_masque_server [options]";
  std::vector<std::string> positional =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (!positional.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 2;
  }

  const std::string mode_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_masque_mode);
  if (mode_string != "open") {
    std::cerr << "Unsupported MASQUE mode: " << mode_string << std::endl;
    return 2;
  }

  quiche::QuicheSystemEventLoop event_loop("naive_masque_server");
  constexpr quic::MasqueMode kMode = quic::MasqueMode::kOpen;
  const int32_t port = quiche::GetQuicheCommandLineFlag(FLAGS_port);
  const std::string authority =
      quiche::GetQuicheCommandLineFlag(FLAGS_server_authority);
  const std::string basic_user =
      quiche::GetQuicheCommandLineFlag(FLAGS_basic_user);
  const std::string basic_pass =
      quiche::GetQuicheCommandLineFlag(FLAGS_basic_pass);
  if (basic_user.empty() != basic_pass.empty()) {
    std::cerr << "basic_user and basic_pass must be set together" << std::endl;
    return 2;
  }
  std::string expected_proxy_authorization;
  if (!basic_user.empty()) {
    expected_proxy_authorization =
        "Basic " + absl::Base64Escape(basic_user + ":" + basic_pass);
  }

  auto backend = std::make_unique<LoggingMasqueServerBackend>(
      kMode, authority, quiche::GetQuicheCommandLineFlag(FLAGS_cache_dir),
      std::move(expected_proxy_authorization),
      quiche::GetQuicheCommandLineFlag(FLAGS_ignore_connect_requests),
      quiche::GetQuicheCommandLineFlag(FLAGS_fail_connects));
  auto server = std::make_unique<quic::MasqueServer>(kMode, backend.get());
  if (!server->CreateUDPSocketAndListen(
          quic::QuicSocketAddress(quic::QuicIpAddress::Any6(), port))) {
    std::cerr << "Failed to listen on UDP port " << port << std::endl;
    return 1;
  }

  std::cout << "READY masque=h3-connect-udp bind=[::]:" << port
            << " authority=" << (authority.empty() ? "*" : authority)
            << " basic_auth=" << (basic_user.empty() ? "disabled" : "required")
            << " ignore_connect="
            << (quiche::GetQuicheCommandLineFlag(FLAGS_ignore_connect_requests)
                    ? "true"
                    : "false")
            << " fail_connects="
            << (quiche::GetQuicheCommandLineFlag(FLAGS_fail_connects) ? "true"
                                                                      : "false")
            << std::endl;
  server->HandleEventsForever();
  return 0;
}
