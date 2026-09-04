#include "codex/update_crypto.h"

#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) {
  if (!value) ++failures;
}
bool equal(const std::uint8_t* left, const std::uint8_t* right,
           std::size_t size) {
  unsigned int different = 0;
  for (std::size_t index = 0; index < size; ++index) {
    different |= left[index] ^ right[index];
  }
  return different == 0;
}
static constexpr std::uint8_t kPublicKey[91] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x27, 0x28, 0xd1, 0x18, 0xfa, 0xa9, 0x31, 0xd1, 0x3b,
    0x03, 0xb3, 0x53, 0x32, 0x91, 0x06, 0xa5, 0xd4, 0x3a, 0x1a, 0xc7, 0x4a,
    0x01, 0xc1, 0xe4, 0xd8, 0x29, 0x47, 0x67, 0x5c, 0xb8, 0xf6, 0x74, 0xfe,
    0x9b, 0xea, 0x73, 0x32, 0xe7, 0x31, 0xa1, 0x79, 0x2d, 0x66, 0x56, 0x3c,
    0xac, 0xd7, 0x4b, 0x5f, 0x4f, 0x45, 0xab, 0x97, 0x6c, 0x0a, 0xda, 0x9e,
    0xa8, 0xfd, 0x72, 0xeb, 0x9a, 0x1d, 0xc1};
static constexpr std::uint8_t kSignature[71] = {
    0x30, 0x45, 0x02, 0x21, 0x00, 0xfd, 0x4b, 0xb4, 0x27, 0x2f, 0xb7, 0x2b,
    0x96, 0xe2, 0xad, 0x18, 0x55, 0xdd, 0x91, 0x0e, 0xac, 0x2e, 0x4e, 0xd3,
    0x33, 0x7f, 0xb1, 0x19, 0x7a, 0x97, 0x96, 0x61, 0x9a, 0x0a, 0xc9, 0x61,
    0xdf, 0x02, 0x20, 0x38, 0xb9, 0x61, 0x4f, 0xcc, 0x66, 0x1b, 0x4e, 0x14,
    0xf0, 0x1c, 0x19, 0x02, 0x73, 0xa7, 0xcd, 0xd0, 0x98, 0x62, 0x7b, 0x2d,
    0x2b, 0x64, 0x4c, 0x34, 0x37, 0x21, 0x63, 0xf0, 0xb8, 0x91, 0xb3};
void put32(std::uint8_t* output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
void put64(std::uint8_t* output, std::uint64_t value) {
  for (unsigned int index = 0; index < 8; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
void make_signed_wire(std::uint8_t* wire) {
  wire[0] = 'C'; wire[1] = 'M'; wire[2] = 'U'; wire[3] = '1';
  wire[4] = 1; wire[5] = 1; wire[6] = 3;
  for (unsigned int index = 0; index < 16; ++index) wire[8 + index] = index;
  put64(wire + 24, 42);
  put32(wire + 32, 3);
  static constexpr std::uint8_t kDigest[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
      0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
      0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  for (unsigned int index = 0; index < 32; ++index) wire[36 + index] = kDigest[index];
  for (unsigned int index = 0; index < 12; ++index) wire[68 + index] = index;
  for (unsigned int index = 0; index < 48; ++index) wire[80 + index] = index;
  wire[128] = 7;
  const char version[] = "1.2.3.4";
  for (unsigned int index = 0; index < 7; ++index) wire[136 + index] = version[index];
  wire[168] = sizeof(kSignature);
  for (unsigned int index = 0; index < sizeof(kSignature); ++index) {
    wire[176 + index] = kSignature[index];
  }
}
bool encrypt_gcm(const std::uint8_t key[32], const std::uint8_t nonce[12],
                 const std::uint8_t* aad, std::size_t aad_size,
                 const std::uint8_t* plaintext, std::size_t size,
                 std::uint8_t* ciphertext, std::uint8_t tag[16]) {
  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  int written = 0, total = 0;
  const bool ok =
      context != nullptr &&
      EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
      EVP_EncryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
      EVP_EncryptUpdate(context, nullptr, &written, aad,
                        static_cast<int>(aad_size)) == 1 &&
      EVP_EncryptUpdate(context, ciphertext, &written, plaintext,
                        static_cast<int>(size)) == 1 &&
      ((total = written), true) &&
      EVP_EncryptFinal_ex(context, ciphertext + total, &written) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
  EVP_CIPHER_CTX_free(context);
  return ok;
}
}  // namespace

extern "C" void mainCRTStartup() {
  static constexpr std::uint8_t kExpectedSha256[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
      0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
      0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  const std::uint8_t input[] = {'a', 'b', 'c'};
  std::uint8_t digest[32]{};
  require(codex::sha256_bytes(input, sizeof(input), digest));
  require(equal(digest, kExpectedSha256, sizeof(digest)));

  codex::UpdateManifest manifest{};
  manifest.payload_size = sizeof(input);
  for (unsigned int index = 0; index < 32; ++index) {
    manifest.payload_sha256[index] = kExpectedSha256[index];
  }
  require(codex::verify_update_payload_digest(manifest, input, sizeof(input)));
  const std::uint8_t changed[] = {'a', 'b', 'd'};
  require(!codex::verify_update_payload_digest(manifest, changed,
                                                sizeof(changed)));
  require(!codex::verify_update_payload_digest(manifest, input,
                                                sizeof(input) - 1));

  std::uint8_t wire[codex::kUpdateManifestWireSize]{};
  make_signed_wire(wire);
  codex::UpdateTrustPolicy policy{};
  for (unsigned int index = 0; index < 16; ++index) {
    policy.expected_device_id[index] = index;
  }
  policy.minimum_release_sequence_by_class[
      codex::update_class_index(codex::UpdatePackageClass::Compatibility)] = 41;
  policy.allowed_class_mask =
      codex::update_class_bit(codex::UpdatePackageClass::Compatibility);
  policy.vendor_public_key_der = kPublicKey;
  policy.vendor_public_key_der_size = sizeof(kPublicKey);
  require(codex::verify_update_manifest(wire, sizeof(wire), policy).ok);

  policy.expected_device_id[0] ^= 1U;
  require(codex::verify_update_manifest(wire, sizeof(wire), policy).error ==
          codex::UpdateVerificationError::DeviceMismatch);
  policy.expected_device_id[0] ^= 1U;
  policy.minimum_release_sequence_by_class[
      codex::update_class_index(codex::UpdatePackageClass::Compatibility)] = 42;
  require(codex::verify_update_manifest(wire, sizeof(wire), policy).error ==
          codex::UpdateVerificationError::Replay);
  policy.minimum_release_sequence_by_class[
      codex::update_class_index(codex::UpdatePackageClass::Compatibility)] = 41;
  wire[36] ^= 1U;
  require(codex::verify_update_manifest(wire, sizeof(wire), policy).error ==
          codex::UpdateVerificationError::SignatureInvalid);

  codex::UpdateManifest encrypted{};
  encrypted.package_class = codex::UpdatePackageClass::Compatibility;
  encrypted.release_sequence = 77;
  for (unsigned int i = 0; i < 16; ++i) encrypted.target_device_id[i] = i;
  for (unsigned int i = 0; i < 12; ++i) encrypted.payload_nonce[i] = i + 3;
  std::uint8_t device_kek[32]{}, content_key[32]{};
  for (unsigned int i = 0; i < 32; ++i) {
    device_kek[i] = i + 10; content_key[i] = 255 - i;
  }
  std::uint8_t package_aad[32]{};
  for (unsigned int i = 0; i < 16; ++i) package_aad[i] = i;
  for (unsigned int i = 0; i < 8; ++i) {
    package_aad[16+i] = static_cast<std::uint64_t>(77) >> (i*8);
  }
  package_aad[24] = 1;
  require(encrypt_gcm(device_kek, encrypted.payload_nonce, package_aad,
                      sizeof(package_aad), content_key, sizeof(content_key),
                      encrypted.wrapped_content_key,
                      encrypted.wrapped_content_key + 32));
  std::uint8_t unwrapped[32]{};
  require(codex::unwrap_update_content_key(encrypted, device_kek, unwrapped));
  require(equal(unwrapped, content_key, sizeof(content_key)));
  device_kek[0] ^= 1;
  require(!codex::unwrap_update_content_key(encrypted, device_kek, unwrapped));
  device_kek[0] ^= 1;

  const std::uint8_t plain_chunk[] = {'s','e','c','r','e','t'};
  std::uint8_t cipher_chunk[sizeof(plain_chunk)]{}, chunk_tag[16]{};
  std::uint8_t chunk_nonce[12]{};
  for (unsigned int i=0;i<12;++i) chunk_nonce[i]=encrypted.payload_nonce[i];
  const std::uint32_t chunk_sequence=5, chunk_offset=1024;
  for(unsigned int i=0;i<4;++i) chunk_nonce[8+i]^=chunk_sequence>>(i*8);
  std::uint8_t chunk_aad[40]{};
  for(unsigned int i=0;i<32;++i) chunk_aad[i]=package_aad[i];
  for(unsigned int i=0;i<4;++i) {
    chunk_aad[32+i]=chunk_offset>>(i*8);
    chunk_aad[36+i]=chunk_sequence>>(i*8);
  }
  require(encrypt_gcm(content_key, chunk_nonce, chunk_aad, sizeof(chunk_aad),
                      plain_chunk, sizeof(plain_chunk), cipher_chunk, chunk_tag));
  std::uint8_t decrypted[sizeof(plain_chunk)]{};
  require(codex::decrypt_update_chunk(encrypted, content_key, chunk_offset,
                                      chunk_sequence, cipher_chunk,
                                      sizeof(cipher_chunk), chunk_tag,
                                      decrypted));
  require(equal(decrypted, plain_chunk, sizeof(plain_chunk)));
  chunk_tag[0]^=1;
  require(!codex::decrypt_update_chunk(encrypted, content_key, chunk_offset,
                                       chunk_sequence, cipher_chunk,
                                       sizeof(cipher_chunk), chunk_tag,
                                       decrypted));

  ExitProcess(failures);
}
