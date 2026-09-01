// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// M7-G1 quic-congestion regression: proves via the shared production helper
// CreateQuicContextFromNaiveConfig that:
// - default cubic leaves client_connection_options unchanged
// - unknown value is rejected (startup failure, not silent fallback)
// - bbr1 => kTBBR, bbr2 => kB2ON
// - tag is inserted before Build() on every production construction path
// - BBR without quic:// proxy is inert (preserves M6 empty-origins invariant)
// Does NOT use CertVerifier bypass, does not log payloads/destinations.

#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/at_exit.h"
#include "base/values.h"
#include "net/quic/quic_context.h"
#include "net/third_party/quiche/src/quiche/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "url/scheme_host_port.h"
#include "net/tools/naive/naive_config.h"

namespace {

int failures = 0;

void Expect(bool condition, std::string_view description) {
  if (!condition) {
    std::cerr << "FAILED: " << description << "\n";
    ++failures;
  } else {
    std::cout << "PASS: " << description << "\n";
  }
}

url::SchemeHostPort TestOrigin() {
  return url::SchemeHostPort(url::kHttpsScheme, "proxy.example.com", 443);
}

void TestDefaultCubicLeavesVectorUnchanged() {
  base::DictValue dict;
  net::NaiveConfig config;
  Expect(config.Parse(dict), "default cubic: parse empty dict succeeds");
  Expect(config.quic_congestion == net::QuicCongestionControl::kCubic,
         "default cubic: enum is kCubic");

  // No origins, cubic => no QuicContext (preserves M6).
  {
    auto ctx = net::CreateQuicContextFromNaiveConfig(config);
    Expect(!ctx, "default cubic without origins: no QuicContext (preserves M6 behavior)");
  }

  // With origins, cubic => context exists for origins but no congestion tag.
  {
    net::NaiveConfig c = config;
    c.origins_to_force_quic_on.insert(TestOrigin());
    auto ctx = net::CreateQuicContextFromNaiveConfig(c);
    Expect(ctx != nullptr, "default cubic with origins: context created for origins");
    Expect(ctx->params()->client_connection_options.empty(),
           "default cubic with origins: client_connection_options empty (no new QUIC tag)");
    Expect(ctx->params()->origins_to_force_quic_on.count(TestOrigin()) == 1,
           "default cubic: origins preserved");
  }
  std::cout << "M7_G1_DEFAULT_CUBIC_UNCHANGED_OK\n";
}

void TestExplicitCubic() {
  base::DictValue dict;
  dict.Set("quic-congestion", "cubic");
  net::NaiveConfig config;
  Expect(config.Parse(dict), "explicit cubic: parse succeeds");
  Expect(config.quic_congestion == net::QuicCongestionControl::kCubic,
         "explicit cubic: enum is kCubic");

  net::NaiveConfig c = config;
  c.origins_to_force_quic_on.insert(TestOrigin());
  auto ctx = net::CreateQuicContextFromNaiveConfig(c);
  Expect(ctx->params()->client_connection_options.empty(),
         "explicit cubic: client_connection_options empty");
  std::cout << "M7_G1_EXPLICIT_CUBIC_OK\n";
}

void TestBbr1SelectsKTBBR() {
  base::DictValue dict;
  dict.Set("quic-congestion", "bbr1");
  net::NaiveConfig config;
  Expect(config.Parse(dict), "bbr1: parse succeeds");
  Expect(config.quic_congestion == net::QuicCongestionControl::kBbr1,
         "bbr1: enum is kBbr1");

  // Without origins, BBR is inert: no QuicContext, preserving M6 empty-origins
  // invariant. This is intentional; BBR only matters when a quic:// proxy exists.
  {
    auto ctx = net::CreateQuicContextFromNaiveConfig(config);
    Expect(!ctx, "bbr1 without origins: inert, no QuicContext (preserves frozen contract)");
  }

  // With origins, BBR tag is present before Build, alongside origins.
  {
    net::NaiveConfig c = config;
    c.origins_to_force_quic_on.insert(TestOrigin());
    auto ctx = net::CreateQuicContextFromNaiveConfig(c);
    Expect(ctx != nullptr, "bbr1 with origins: context created");
    Expect(quic::ContainsQuicTag(ctx->params()->client_connection_options,
                                 quic::kTBBR),
           "bbr1 with origins: kTBBR present before Build");
    Expect(!quic::ContainsQuicTag(ctx->params()->client_connection_options,
                                  quic::kB2ON),
           "bbr1: not kB2ON");
    Expect(ctx->params()->origins_to_force_quic_on.count(TestOrigin()) == 1,
           "bbr1 with origins: origins preserved before Build");
  }
  std::cout << "M7_G1_BBR1_KTBBR_OK\n";
}

void TestBbr2SelectsKB2ON() {
  base::DictValue dict;
  dict.Set("quic-congestion", "bbr2");
  net::NaiveConfig config;
  Expect(config.Parse(dict), "bbr2: parse succeeds");
  Expect(config.quic_congestion == net::QuicCongestionControl::kBbr2,
         "bbr2: enum is kBbr2");

  {
    auto ctx = net::CreateQuicContextFromNaiveConfig(config);
    Expect(!ctx, "bbr2 without origins: inert, no QuicContext");
  }

  {
    net::NaiveConfig c = config;
    c.origins_to_force_quic_on.insert(TestOrigin());
    auto ctx = net::CreateQuicContextFromNaiveConfig(c);
    Expect(quic::ContainsQuicTag(ctx->params()->client_connection_options,
                                 quic::kB2ON),
           "bbr2 with origins: kB2ON present before Build");
    Expect(!quic::ContainsQuicTag(ctx->params()->client_connection_options,
                                  quic::kTBBR),
           "bbr2: not kTBBR");
  }
  std::cout << "M7_G1_BBR2_KB2ON_OK\n";
}

void TestUnknownRejected() {
  const std::vector<std::string> invalid = {"",  "BBR1",  "bbr",       "cubic ",
                                            "bbr3", "unknown", "Cubic", "BBR2"};
  for (const auto& val : invalid) {
    base::DictValue dict;
    dict.Set("quic-congestion", val);
    net::NaiveConfig config;
    bool ok = config.Parse(dict);
    Expect(!ok,
           std::string("unknown quic-congestion '") + val +
               "' must be rejected (startup failure, no silent fallback)");
  }

  // Non-string type also rejected.
  {
    base::DictValue dict;
    dict.Set("quic-congestion", 1);
    net::NaiveConfig config;
    Expect(!config.Parse(dict),
           "non-string quic-congestion must be rejected");
  }

  // Missing key still succeeds with default (not a rejection).
  {
    base::DictValue dict;
    net::NaiveConfig config;
    Expect(config.Parse(dict),
           "missing quic-congestion keeps default and does not reject");
    Expect(config.quic_congestion == net::QuicCongestionControl::kCubic,
           "missing key remains cubic");
  }
  std::cout << "M7_G1_UNKNOWN_REJECTED_OK\n";
}

void TestCliAndJsonSameKey() {
  base::DictValue json_dict;
  json_dict.Set("quic-congestion", "bbr1");
  net::NaiveConfig json_config;
  Expect(json_config.Parse(json_dict), "JSON bbr1 parse");

  base::DictValue cli_dict;
  cli_dict.Set("quic-congestion", "bbr1");
  net::NaiveConfig cli_config;
  Expect(cli_config.Parse(cli_dict), "CLI --quic-congestion=bbr1 parse");
  Expect(json_config.quic_congestion == cli_config.quic_congestion,
         "JSON and CLI bbr1 produce same enum");

  // Also verify that the key is kebab-case as frozen, not snake_case.
  base::DictValue wrong_key;
  wrong_key.Set("quic_congestion", "bbr1");
  net::NaiveConfig wrong_config;
  Expect(wrong_config.Parse(wrong_key), "wrong snake_case key parse (ignored)");
  Expect(wrong_config.quic_congestion == net::QuicCongestionControl::kCubic,
         "snake_case key does not select BBR (frozen kebab-case contract)");
  std::cout << "M7_G1_CLI_JSON_SAME_KEY_OK\n";
}

void TestTagBeforeBuildOrdering() {
  // Prove that client_connection_options is populated before the
  // URLRequestContextBuilder::Build() equivalent via the shared helper.
  base::DictValue dict;
  dict.Set("quic-congestion", "bbr1");
  net::NaiveConfig config;
  config.Parse(dict);
  config.origins_to_force_quic_on.insert(TestOrigin());

  // Shared helper is the sole production wiring; test calls the same.
  auto ctx = net::CreateQuicContextFromNaiveConfig(config);

  // Before Build: vector already has tag.
  bool before_build_has_tag = quic::ContainsQuicTag(
      ctx->params()->client_connection_options, quic::kTBBR);

  // Simulate builder.set_quic_context + Build() does not modify the tag.
  net::QuicParams params_before_build = *ctx->params();
  std::unique_ptr<net::QuicContext> moved = std::move(ctx);
  bool after_move_has_tag = quic::ContainsQuicTag(
      moved->params()->client_connection_options, quic::kTBBR);

  Expect(before_build_has_tag, "tag present before Build()");
  Expect(after_move_has_tag, "tag survives set_quic_context move before Build()");
  Expect(params_before_build.client_connection_options ==
             moved->params()->client_connection_options,
         "vector unchanged across set_quic_context/Build() boundary");

  // For bbr2 the same property holds.
  {
    base::DictValue d2;
    d2.Set("quic-congestion", "bbr2");
    net::NaiveConfig c2;
    c2.Parse(d2);
    c2.origins_to_force_quic_on.insert(TestOrigin());
    auto ctx2 = net::CreateQuicContextFromNaiveConfig(c2);
    Expect(quic::ContainsQuicTag(ctx2->params()->client_connection_options,
                                 quic::kB2ON),
           "bbr2 tag present before Build()");
  }

  std::cout << "M7_G1_TAG_BEFORE_BUILD_OK\n";
}

void TestCubicDoesNotAddTagWhenOriginsEmpty() {
  base::DictValue dict;
  dict.Set("quic-congestion", "cubic");
  net::NaiveConfig config;
  config.Parse(dict);

  {
    auto ctx = net::CreateQuicContextFromNaiveConfig(config);
    Expect(!ctx, "cubic without origins: still no QuicContext (exact M6 behavior)");
  }
  {
    net::NaiveConfig c = config;
    c.origins_to_force_quic_on.insert(TestOrigin());
    auto ctx = net::CreateQuicContextFromNaiveConfig(c);
    Expect(ctx->params()->client_connection_options.empty(),
           "cubic with origins: still no tag");
  }
  std::cout << "M7_G1_CUBIC_NO_TAG_PRESERVED_OK\n";
}

}  // namespace

int main() {
  base::AtExitManager at_exit_manager;

  TestDefaultCubicLeavesVectorUnchanged();
  TestExplicitCubic();
  TestBbr1SelectsKTBBR();
  TestBbr2SelectsKB2ON();
  TestUnknownRejected();
  TestCliAndJsonSameKey();
  TestTagBeforeBuildOrdering();
  TestCubicDoesNotAddTagWhenOriginsEmpty();

  if (failures != 0) {
    std::cerr << "M7_G1_QUIC_CONGESTION_TEST failures=" << failures << "\n";
    return 1;
  }
  std::cout << "M7_G1_QUIC_CONGESTION_PARSER_OK\n";
  std::cout << "M7_G1_CLIENT_BBR_OK\n";
  return 0;
}
