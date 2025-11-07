#ifndef CRYPTO_UTIL_H
#define CRYPTO_UTIL_H

#include <sgx_trts.h>
#include <sgx_tcrypto.h>
#include <vector>
#include <cstdint>
#include <cstring>

// ================================
// Enclave 内部加解密工具类声明
// ================================
class EnclaveCryptoUtils {
private:
    sgx_aes_gcm_128bit_key_t key;

public:
    EnclaveCryptoUtils(const uint8_t* key_data, size_t key_size);

    // 加密
    sgx_status_t encrypt(const std::vector<uint8_t>& plaintext,
                         std::vector<uint8_t>& ciphertext);

    // 解密
    sgx_status_t decrypt(const std::vector<uint8_t>& ciphertext,
                         std::vector<uint8_t>& plaintext);

    // 工具函数
    static sgx_status_t generate_random_key(std::vector<uint8_t>& key, size_t key_size = 16);
    static sgx_status_t generate_random_iv(std::vector<uint8_t>& iv, size_t iv_size = 16);
};

#endif // CRYPTO_UTIL_H
