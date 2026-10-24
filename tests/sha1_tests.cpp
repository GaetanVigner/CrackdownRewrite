// SHA-1 against the NIST vectors, with the message split at every position.

#include <string>

#include "check.h"
#include "sha1.h"

namespace {

std::string Hex(const crackdown::Sha1Digest& digest) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out;
  for (const uint8_t byte : digest) {
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0xF]);
  }
  return out;
}

std::string Hash(const std::string& message) {
  return Hex(crackdown::Sha1Of(message.data(), message.size()));
}

}  // namespace

int main() {
  std::printf("NIST vectors\n");
  CHECK(Hash("") == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
  CHECK(Hash("abc") == "A9993E364706816ABA3E25717850C26C9CD0D89D");
  CHECK(Hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
  CHECK(Hash(std::string(1000000, 'a')) == "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F");

  std::printf("streaming updates match a single update\n");
  std::string message;
  for (int i = 0; i < 300; ++i) {
    message.push_back(char('a' + i % 23));
  }
  const std::string expected = Hash(message);
  for (size_t split = 0; split <= message.size(); ++split) {
    crackdown::Sha1 hash;
    hash.Update(message.data(), split);
    hash.Update(message.data() + split, message.size() - split);
    CHECK(Hex(hash.Finish()) == expected);
  }
  for (size_t size = 0; size <= 130; ++size) {
    crackdown::Sha1 bytewise;
    const std::string part(size, 'x');
    for (const char c : part) {
      bytewise.Update(&c, 1);
    }
    CHECK(Hex(bytewise.Finish()) == Hash(part));
  }
  return check::Finish("sha1_tests");
}
