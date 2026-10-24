#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crackdown {

using Sha1Digest = std::array<uint8_t, 20>;

// STFS packages carry SHA-1 hashes; this is not used for anything that needs collision resistance.
class Sha1 {
 public:
  Sha1() { Reset(); }

  void Reset() {
    state_ = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    buffered_ = 0;
    total_bytes_ = 0;
  }

  void Update(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    total_bytes_ += size;
    if (buffered_ > 0) {
      const size_t take = size < kBlockBytes - buffered_ ? size : kBlockBytes - buffered_;
      std::memcpy(buffer_.data() + buffered_, bytes, take);
      buffered_ += take;
      bytes += take;
      size -= take;
      if (buffered_ < kBlockBytes) {
        return;
      }
      Compress(buffer_.data());
      buffered_ = 0;
    }
    for (; size >= kBlockBytes; bytes += kBlockBytes, size -= kBlockBytes) {
      Compress(bytes);
    }
    if (size > 0) {
      std::memcpy(buffer_.data(), bytes, size);
      buffered_ = size;
    }
  }

  // The object must be Reset before it hashes another message.
  Sha1Digest Finish() {
    const uint64_t bit_length = total_bytes_ * 8;
    const uint8_t padding_start = 0x80;
    Update(&padding_start, 1);
    const uint8_t zero = 0;
    while (buffered_ != kBlockBytes - 8) {
      Update(&zero, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) {
      length[i] = uint8_t(bit_length >> (56 - 8 * i));
    }
    Update(length, sizeof(length));
    Sha1Digest digest;
    for (size_t i = 0; i < state_.size(); ++i) {
      digest[4 * i] = uint8_t(state_[i] >> 24);
      digest[4 * i + 1] = uint8_t(state_[i] >> 16);
      digest[4 * i + 2] = uint8_t(state_[i] >> 8);
      digest[4 * i + 3] = uint8_t(state_[i]);
    }
    return digest;
  }

 private:
  static constexpr size_t kBlockBytes = 64;

  static uint32_t RotateLeft(uint32_t value, int count) { return (value << count) | (value >> (32 - count)); }

  void Compress(const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 | uint32_t(block[4 * i + 2]) << 8 |
             block[4 * i + 3];
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f;
      uint32_t k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t t = RotateLeft(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = RotateLeft(b, 30);
      b = a;
      a = t;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<uint32_t, 5> state_;
  std::array<uint8_t, kBlockBytes> buffer_;
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

inline Sha1Digest Sha1Of(const void* data, size_t size) {
  Sha1 hash;
  hash.Update(data, size);
  return hash.Finish();
}

}  // namespace crackdown
