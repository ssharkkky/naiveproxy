// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma allow_unsafe_buffers

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "net/tools/naive/socks5_udp_codec.h"

namespace {

class DeterministicRandom {
 public:
  explicit DeterministicRandom(uint64_t seed) : state_(seed ? seed : 1) {}

  uint64_t Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
  }

  size_t Bounded(size_t bound) {
    return bound == 0 ? 0 : static_cast<size_t>(Next() % bound);
  }

 private:
  uint64_t state_;
};

bool ReadOption(std::string_view argument,
                std::string_view prefix,
                uint64_t* output) {
  if (!argument.starts_with(prefix)) {
    return false;
  }
  std::string_view value = argument.substr(prefix.size());
  auto result = std::from_chars(value.data(), value.data() + value.size(),
                                *output);
  return result.ec == std::errc() && result.ptr == value.data() + value.size();
}

std::vector<std::vector<uint8_t>> Corpus() {
  return {
      {},
      {0, 0, 0, 1, 127, 0, 0, 1, 0, 53},
      {0, 0, 0, 4, 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 1, 0x01, 0xbb},
      {0, 0, 0, 3, 9, 'f', 'u', 'z', 'z', '.', 't', 'e', 's', 't', 0, 53},
      {0, 0, 1, 1},
      {0, 0, 0, 3, 0, 0, 53},
      {0xff, 0xff, 0xff, 0xff},
  };
}

std::vector<uint8_t> GenerateInput(DeterministicRandom* random,
                                   const std::vector<std::vector<uint8_t>>&
                                       corpus) {
  std::vector<uint8_t> input;
  if (random->Bounded(2) == 0) {
    input = corpus[random->Bounded(corpus.size())];
  } else {
    input.resize(random->Bounded(2049));
    for (uint8_t& byte : input) {
      byte = static_cast<uint8_t>(random->Next());
    }
  }

  const size_t mutations = 1 + random->Bounded(8);
  for (size_t mutation = 0; mutation < mutations; ++mutation) {
    switch (random->Bounded(3)) {
      case 0:
        if (!input.empty()) {
          input[random->Bounded(input.size())] =
              static_cast<uint8_t>(random->Next());
        }
        break;
      case 1:
        if (input.size() < 4096) {
          input.insert(input.begin() + random->Bounded(input.size() + 1),
                       static_cast<uint8_t>(random->Next()));
        }
        break;
      case 2:
        if (!input.empty()) {
          input.erase(input.begin() + random->Bounded(input.size()));
        }
        break;
    }
  }
  return input;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t seed = 0x4d36554450ULL;
  uint64_t iterations = 250000;
  for (int index = 1; index < argc; ++index) {
    std::string_view argument(argv[index]);
    if (ReadOption(argument, "--seed=", &seed) ||
        ReadOption(argument, "--iterations=", &iterations)) {
      continue;
    }
    std::cerr << "unknown or invalid argument\n";
    return 2;
  }
  if (iterations == 0 || iterations > 100000000) {
    std::cerr << "invalid iteration count\n";
    return 2;
  }

  DeterministicRandom random(seed);
  const auto corpus = Corpus();
  uint64_t valid = 0;
  for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
    std::vector<uint8_t> input = GenerateInput(&random, corpus);
    auto parsed = net::ParseSocks5UdpDatagram(input);
    if (!parsed.has_value()) {
      continue;
    }
    ++valid;
    auto rebuilt = net::BuildSocks5UdpDatagram(*parsed);
    if (!rebuilt.has_value()) {
      std::cerr << "fuzz invariant failed: parsed input did not rebuild\n";
      return 1;
    }
    auto reparsed = net::ParseSocks5UdpDatagram(*rebuilt);
    if (!reparsed.has_value() || *reparsed != *parsed) {
      std::cerr << "fuzz invariant failed: rebuild did not round trip\n";
      return 1;
    }
  }

  std::cout << "M6_G4_CODEC_FUZZ_OK seed=" << seed
            << " iterations=" << iterations << " valid=" << valid << "\n";
  return 0;
}
