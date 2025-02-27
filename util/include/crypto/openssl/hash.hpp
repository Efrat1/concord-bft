// Concord
//
// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0
// License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the
// LICENSE file.

#pragma once

#include <array>
#include <iomanip>

#include <openssl/evp.h>

#include "types.hpp"
#include "hex_tools.h"
#include "assertUtils.hpp"

namespace concord::crypto::openssl {

// A simple wrapper class around OpenSSL versions > 1.1.1 that implements EVP hash functions.
template <const EVP_MD* (*EVPMethod)(), size_t DIGEST_SIZE_IN_BYTES>
class EVPHash {
 public:
  static constexpr size_t SIZE_IN_BYTES = DIGEST_SIZE_IN_BYTES;
  using Digest = std::array<concord::Byte, SIZE_IN_BYTES>;

  EVPHash() noexcept : ctx_(EVP_MD_CTX_new()) { ConcordAssert(ctx_ != nullptr); }

  ~EVPHash() noexcept {
    if (nullptr != ctx_) {
      EVP_MD_CTX_destroy(ctx_);
    }
  }

  EVPHash(EVPHash&& other) noexcept {
    ctx_ = other.ctx_;
    updating_ = other.updating_;
    other.ctx_ = nullptr;
  };

  EVPHash& operator=(EVPHash&& other) noexcept {
    *this = EVPHash{std::move(other)};
    return *this;
  }

  // Don't allow copying.
  EVPHash(const EVPHash&) = delete;
  EVPHash& operator=(const EVPHash&) = delete;

  // Compute a digest for an entire buffer and return an array containing the
  // digest.
  //
  // This is the simplest way to hash something as all setup is done for you.
  // Use the 3 method mechanism below if you need to hash multiple buffers.
  Digest digest(const void* buf, size_t size) noexcept {
    ConcordAssert(!updating_);
    ConcordAssert(EVP_MD_CTX_reset(ctx_) == 1);
    ConcordAssert(EVP_DigestInit_ex(ctx_, EVPMethod(), nullptr) == 1);

    ConcordAssert(EVP_DigestUpdate(ctx_, buf, size) == 1);

    Digest digest;
    unsigned int _digest_len{0};
    ConcordAssert(EVP_DigestFinal_ex(ctx_, digest.data(), &_digest_len) == 1);
    ConcordAssert(_digest_len == SIZE_IN_BYTES);
    return digest;
  }

  // The following 3 methods are used to compute digests piecemeal, by
  // continuously appending new data to be hashed.

  void init() noexcept {
    ConcordAssert(!updating_);
    ConcordAssert(EVP_MD_CTX_reset(ctx_) == 1);
    ConcordAssert(EVP_DigestInit_ex(ctx_, EVPMethod(), nullptr) == 1);
    updating_ = true;
  }

  // Add more data to a digest.
  void update(const void* buf, size_t size) noexcept {
    ConcordAssert(updating_);
    ConcordAssert(EVP_DigestUpdate(ctx_, buf, size) == 1);
  }

  Digest finish() noexcept {
    Digest digest;
    unsigned int _digest_len{0};
    ConcordAssert(EVP_DigestFinal_ex(ctx_, digest.data(), &_digest_len) == 1);
    ConcordAssert(_digest_len == SIZE_IN_BYTES);
    updating_ = false;
    return digest;
  }

  std::string toHexString(const Digest& digest) {
    std::ostringstream oss;
    for (size_t i = 0; i < SIZE_IN_BYTES; ++i)
      oss << std::setfill('0') << std::setw(2) << std::hex << std::uppercase << (0xff & (unsigned int)digest[i]);
    return oss.str();
  }

 private:
  EVP_MD_CTX* ctx_{nullptr};
  bool updating_{false};
};

using SHA3_256 = EVPHash<EVP_sha3_256, 32>;
using SHA2_256 = EVPHash<EVP_sha256, 32>;

}  // namespace concord::crypto::openssl
