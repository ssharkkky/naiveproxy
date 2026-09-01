// Copyright 2024 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_CONFIG_H_
#define NET_TOOLS_NAIVE_NAIVE_CONFIG_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <memory>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/proxy_chain.h"
#include "net/http/http_request_headers.h"
#include "net/tools/naive/naive_protocol.h"
#include "url/scheme_host_port.h"

namespace net {

struct NaiveListenConfig {
  ClientProtocol protocol = ClientProtocol::kSocks5;
  std::string user;
  std::string pass;
  std::string addr = "0.0.0.0";
  int port = 1080;

  NaiveListenConfig();
  NaiveListenConfig(const NaiveListenConfig&);
  ~NaiveListenConfig();
  bool Parse(const std::string& str);
};

enum class QuicCongestionControl {
  kCubic = 0,
  kBbr1,
  kBbr2,
};

struct NaiveConfig {
  std::vector<NaiveListenConfig> listen = {NaiveListenConfig()};

  int insecure_concurrency = 1;

#if BUILDFLAG(IS_ANDROID)
  int tunnel_timeout = 600;
  int idle_timeout = 300;
#else
  int tunnel_timeout = 1800;
  int idle_timeout = 600;
#endif

  HttpRequestHeaders extra_headers;

  // The last server is assumed to be Naive.
  std::vector<ProxyChain> proxy_chains;
  std::set<url::SchemeHostPort> origins_to_force_quic_on;
  std::map<url::SchemeHostPort, AuthCredentials> auth_store;

  std::string host_resolver_rules;

  IPAddress resolver_range = {100, 64, 0, 0};
  size_t resolver_prefix = 10;

  logging::LoggingSettings log = {.logging_dest = logging::LOG_NONE};
  base::FilePath log_file;

  base::FilePath log_net_log;

  base::FilePath ssl_key_log_file;

  std::optional<bool> no_post_quantum;

  QuicCongestionControl quic_congestion = QuicCongestionControl::kCubic;

  NaiveConfig();
  NaiveConfig(const NaiveConfig&);
  ~NaiveConfig();
  bool Parse(const base::DictValue& value);
};

class QuicContext;

// Shared production helper: builds a QuicContext from NaiveConfig before
// URLRequestContextBuilder::Build(). Called on every production QUIC context
// construction path (naive_proxy_bin.cc). Returns nullptr when no QUIC
// context is needed (no forced origins and no BBR), preserving the M6 frozen
// invariant that empty origins + cubic creates no context. For BBR, the tag
// is pushed only when a context already exists due to origins; BBR without
// quic:// proxy is inert (no new context), avoiding a new frozen-contract
// branch. The returned context is ready before Build() and contains
// origins_to_force_quic_on and client_connection_options (kTBBR/kB2ON).
std::unique_ptr<QuicContext> CreateQuicContextFromNaiveConfig(
    const NaiveConfig& config);

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CONFIG_H_
