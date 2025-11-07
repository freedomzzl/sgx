#include "CryptoUtil.h"

EnclaveCryptoUtils::EnclaveCryptoUtils(const uint8_t* key_data, size_t key_size) {
    if (key_data == nullptr || key_size != 16) {
        sgx_read_rand((uint8_t*)&key, sizeof(key));
    } else {
        memcpy(&key, key_data, sizeof(key));
    }
}

sgx_status_t EnclaveCryptoUtils::encrypt(const std::vector<uint8_t>& plaintext,
                                         std::vector<uint8_t>& ciphertext) {
    if (plaintext.empty()) {
        ciphertext.clear();
        return SGX_SUCCESS;
    }

    uint8_t iv[SGX_AESGCM_IV_SIZE];
    sgx_status_t ret = sgx_read_rand(iv, sizeof(iv));
    if (ret != SGX_SUCCESS) return ret;

    size_t ciphertext_size = plaintext.size() + SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;
    ciphertext.resize(ciphertext_size);

    memcpy(ciphertext.data(), iv, SGX_AESGCM_IV_SIZE);

    sgx_aes_gcm_128bit_tag_t mac;
    ret = sgx_rijndael128GCM_encrypt(
        &key,
        plaintext.data(), plaintext.size(),
        ciphertext.data() + SGX_AESGCM_IV_SIZE,
        iv, SGX_AESGCM_IV_SIZE,
        nullptr, 0,
        &mac
    );

    if (ret != SGX_SUCCESS) return ret;

    memcpy(ciphertext.data() + SGX_AESGCM_IV_SIZE + plaintext.size(), &mac, SGX_AESGCM_MAC_SIZE);
    return SGX_SUCCESS;
}

sgx_status_t EnclaveCryptoUtils::decrypt(const std::vector<uint8_t>& ciphertext,
                                         std::vector<uint8_t>& plaintext) {
    if (ciphertext.size() < SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE)
        return SGX_ERROR_INVALID_PARAMETER;

    const uint8_t* iv = ciphertext.data();
    const uint8_t* enc_data = ciphertext.data() + SGX_AESGCM_IV_SIZE;
    size_t enc_size = ciphertext.size() - SGX_AESGCM_IV_SIZE - SGX_AESGCM_MAC_SIZE;
    const sgx_aes_gcm_128bit_tag_t* tag =
        (const sgx_aes_gcm_128bit_tag_t*)(ciphertext.data() + SGX_AESGCM_IV_SIZE + enc_size);

    plaintext.resize(enc_size);

    sgx_status_t ret = sgx_rijndael128GCM_decrypt(
        &key,
        enc_data, enc_size,
        plaintext.data(),
        iv, SGX_AESGCM_IV_SIZE,
        nullptr, 0,
        tag
    );

    if (ret != SGX_SUCCESS) plaintext.clear();
    return ret;
}

sgx_status_t EnclaveCryptoUtils::generate_random_key(std::vector<uint8_t>& key, size_t key_size) {
    if (key_size != 16 && key_size != 24 && key_size != 32)
        return SGX_ERROR_INVALID_PARAMETER;

    key.resize(key_size);
    return sgx_read_rand(key.data(), key_size);
}

sgx_status_t EnclaveCryptoUtils::generate_random_iv(std::vector<uint8_t>& iv, size_t iv_size) {
    if (iv_size != 16)
        return SGX_ERROR_INVALID_PARAMETER;

    iv.resize(iv_size);
    return sgx_read_rand(iv.data(), iv_size);
}
