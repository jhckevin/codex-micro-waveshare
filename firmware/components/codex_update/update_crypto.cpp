#include "codex/update_crypto.h"

#if defined(CODEX_HOST_OPENSSL)
#include <openssl/evp.h>
#include <openssl/x509.h>
#else
#include "mbedtls/gcm.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#endif

namespace codex {
namespace {

bool constant_time_equal(const std::uint8_t* left, const std::uint8_t* right,
                         std::size_t size) {
  unsigned int different = 0;
  for (std::size_t index = 0; index < size; ++index) {
    different |= left[index] ^ right[index];
  }
  return different == 0;
}

bool verify_signature(const std::uint8_t* signed_data,
                      std::size_t signed_size,
                      const std::uint8_t* signature,
                      std::size_t signature_size,
                      const std::uint8_t* public_key_der,
                      std::size_t public_key_der_size) {
  if (signed_data == nullptr || signature == nullptr ||
      public_key_der == nullptr || public_key_der_size == 0) {
    return false;
  }
#if defined(CODEX_HOST_OPENSSL)
  const unsigned char* cursor = public_key_der;
  EVP_PKEY* key =
      d2i_PUBKEY(nullptr, &cursor, static_cast<long>(public_key_der_size));
  if (key == nullptr ||
      cursor != public_key_der + public_key_der_size) {
    EVP_PKEY_free(key);
    return false;
  }
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  const bool ok =
      context != nullptr &&
      EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, key) == 1 &&
      EVP_DigestVerifyUpdate(context, signed_data, signed_size) == 1 &&
      EVP_DigestVerifyFinal(context, signature, signature_size) == 1;
  EVP_MD_CTX_free(context);
  EVP_PKEY_free(key);
  return ok;
#else
  std::uint8_t digest[kUpdateDigestSize]{};
  if (!sha256_bytes(signed_data, signed_size, digest)) return false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const int parse_result =
      mbedtls_pk_parse_public_key(&key, public_key_der, public_key_der_size);
  const int verify_result =
      parse_result == 0
          ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                              signature, signature_size)
          : parse_result;
  mbedtls_pk_free(&key);
  return verify_result == 0;
#endif
}

UpdateVerificationResult verification_failure(
    const ManifestParseResult& parsed, UpdateVerificationError error) {
  UpdateVerificationResult result{};
  result.parsed = parsed;
  result.error = error;
  return result;
}

void make_package_aad(const UpdateManifest& manifest, std::uint8_t output[32]) {
  for (std::size_t index = 0; index < kUpdateDeviceIdSize; ++index) {
    output[index] = manifest.target_device_id[index];
  }
  for (unsigned int index = 0; index < 8; ++index) {
    output[16 + index] =
        static_cast<std::uint8_t>(manifest.release_sequence >> (index * 8U));
  }
  output[24] = static_cast<std::uint8_t>(manifest.package_class);
  for (unsigned int index = 25; index < 32; ++index) output[index] = 0;
}

void make_chunk_nonce(const UpdateManifest& manifest, std::uint32_t sequence,
                      std::uint8_t nonce[kUpdateNonceSize]) {
  for (std::size_t index = 0; index < kUpdateNonceSize; ++index) {
    nonce[index] = manifest.payload_nonce[index];
  }
  for (unsigned int index = 0; index < 4; ++index) {
    nonce[8 + index] ^= static_cast<std::uint8_t>(sequence >> (index * 8U));
  }
}

bool aes_gcm_decrypt(const std::uint8_t key[32],
                     const std::uint8_t nonce[12],
                     const std::uint8_t* aad, std::size_t aad_size,
                     const std::uint8_t* ciphertext, std::size_t size,
                     const std::uint8_t tag[16], std::uint8_t* plaintext) {
  if (key == nullptr || nonce == nullptr || ciphertext == nullptr ||
      tag == nullptr || plaintext == nullptr) return false;
#if defined(CODEX_HOST_OPENSSL)
  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  int written = 0;
  int total = 0;
  const bool ok =
      context != nullptr &&
      EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr,
                         nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
      EVP_DecryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
      EVP_DecryptUpdate(context, nullptr, &written, aad,
                        static_cast<int>(aad_size)) == 1 &&
      EVP_DecryptUpdate(context, plaintext, &written, ciphertext,
                        static_cast<int>(size)) == 1 &&
      ((total = written), true) &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, 16,
                          const_cast<std::uint8_t*>(tag)) == 1 &&
      EVP_DecryptFinal_ex(context, plaintext + total, &written) == 1 &&
      static_cast<std::size_t>(total + written) == size;
  EVP_CIPHER_CTX_free(context);
  return ok;
#else
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  const int key_result =
      mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
  const int decrypt_result =
      key_result == 0
          ? mbedtls_gcm_auth_decrypt(&context, size, nonce, 12, aad, aad_size,
                                     tag, 16, ciphertext, plaintext)
          : key_result;
  mbedtls_gcm_free(&context);
  return decrypt_result == 0;
#endif
}

}  // namespace

bool sha256_bytes(const std::uint8_t* data, std::size_t size,
                  std::uint8_t output[kUpdateDigestSize]) {
  if ((data == nullptr && size != 0) || output == nullptr) return false;
#if defined(CODEX_HOST_OPENSSL)
  unsigned int output_size = 0;
  return EVP_Digest(data, size, output, &output_size, EVP_sha256(), nullptr) ==
             1 &&
         output_size == kUpdateDigestSize;
#else
  return mbedtls_sha256(data, size, output, 0) == 0;
#endif
}

bool verify_update_payload_digest(const UpdateManifest& manifest,
                                  const std::uint8_t* plaintext,
                                  std::size_t plaintext_size) {
  if (plaintext_size != manifest.payload_size) return false;
  std::uint8_t digest[kUpdateDigestSize]{};
  return sha256_bytes(plaintext, plaintext_size, digest) &&
         constant_time_equal(digest, manifest.payload_sha256, sizeof(digest));
}

bool unwrap_update_content_key(
    const UpdateManifest& manifest,
    const std::uint8_t device_kek[kUpdateDigestSize],
    std::uint8_t content_key[kUpdateDigestSize]) {
  std::uint8_t aad[32]{};
  make_package_aad(manifest, aad);
  return aes_gcm_decrypt(device_kek, manifest.payload_nonce, aad, sizeof(aad),
                         manifest.wrapped_content_key, kUpdateDigestSize,
                         manifest.wrapped_content_key + kUpdateDigestSize,
                         content_key);
}

bool decrypt_update_chunk(
    const UpdateManifest& manifest,
    const std::uint8_t content_key[kUpdateDigestSize],
    std::uint32_t offset, std::uint32_t sequence,
    const std::uint8_t* ciphertext, std::size_t size,
    const std::uint8_t tag[16], std::uint8_t* plaintext) {
  std::uint8_t nonce[kUpdateNonceSize]{};
  make_chunk_nonce(manifest, sequence, nonce);
  std::uint8_t aad[40]{};
  make_package_aad(manifest, aad);
  for (unsigned int index = 0; index < 4; ++index) {
    aad[32 + index] = static_cast<std::uint8_t>(offset >> (index * 8U));
    aad[36 + index] = static_cast<std::uint8_t>(sequence >> (index * 8U));
  }
  return aes_gcm_decrypt(content_key, nonce, aad, sizeof(aad), ciphertext,
                         size, tag, plaintext);
}

UpdateVerificationResult verify_update_manifest(
    const std::uint8_t* wire, std::size_t wire_size,
    const UpdateTrustPolicy& policy) {
  const ManifestParseResult parsed = parse_update_manifest(wire, wire_size);
  if (!parsed.ok) {
    return verification_failure(parsed,
                                UpdateVerificationError::ManifestInvalid);
  }
  if (!constant_time_equal(parsed.manifest.target_device_id,
                           policy.expected_device_id,
                           kUpdateDeviceIdSize)) {
    return verification_failure(parsed,
                                UpdateVerificationError::DeviceMismatch);
  }
  const std::size_t class_index =
      update_class_index(parsed.manifest.package_class);
  if (class_index >= 5 ||
      parsed.manifest.release_sequence <=
          policy.minimum_release_sequence_by_class[class_index]) {
    return verification_failure(parsed, UpdateVerificationError::Replay);
  }
  if ((policy.allowed_class_mask &
       update_class_bit(parsed.manifest.package_class)) == 0) {
    return verification_failure(parsed,
                                UpdateVerificationError::ClassNotAllowed);
  }
  if (!verify_signature(wire, kUpdateManifestSignedSize,
                        parsed.manifest.signature,
                        parsed.manifest.signature_size,
                        policy.vendor_public_key_der,
                        policy.vendor_public_key_der_size)) {
    return verification_failure(parsed,
                                UpdateVerificationError::SignatureInvalid);
  }
  UpdateVerificationResult result{};
  result.parsed = parsed;
  result.error = UpdateVerificationError::None;
  result.ok = true;
  return result;
}

}  // namespace codex
