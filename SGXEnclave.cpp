#include "SGXEnclave_t.h"
#include <sgx_trts.h>
#include <sgx_tcrypto.h>
#include <sgx_tseal.h>
#include <string>
#include <cstdio>
#include <vector>
#include <memory>
#include "CryptoUtil.h"
#include "NodeSerializer.h"
#include "ringoram.h" 

// 全局状态
static int enclave_initialized = 0;
static sgx_aes_gcm_128bit_key_t master_key;
static EnclaveCryptoUtils* global_crypto = nullptr;
static std::unique_ptr<ringoram> g_oram;

// ================================
// 基础函数
// ================================

sgx_status_t ecall_initialize_enclave() {
    if (enclave_initialized) {
        ocall_print_string("Enclave already initialized");
        return SGX_SUCCESS;
    }

    sgx_status_t ret = sgx_read_rand((uint8_t*)&master_key, sizeof(master_key));
    if (ret != SGX_SUCCESS) {
        ocall_print_string("Failed to generate master key");
        return ret;
    }

    global_crypto = new EnclaveCryptoUtils((uint8_t*)&master_key, sizeof(master_key));

    enclave_initialized = 1;
    ocall_print_string("Enclave initialized successfully with crypto support");
    return SGX_SUCCESS;
}

sgx_status_t ecall_test_enclave(int input_value, int* output_value) {
    if (!enclave_initialized) {
        ocall_print_string("Enclave not initialized");
        return SGX_ERROR_UNEXPECTED;
    }

    *output_value = input_value * 2;
    char msg[100];
    snprintf(msg, sizeof(msg), "ECALL received input: %d, returning: %d", input_value, *output_value);
    ocall_print_string(msg);
    return SGX_SUCCESS;
}

// ================================
// 加密测试函数
// ================================

sgx_status_t ecall_test_crypto() {
    if (!enclave_initialized || !global_crypto) {
        return SGX_ERROR_UNEXPECTED;
    }

    std::string test_data = "Hello, SGX Crypto World!";
    std::vector<uint8_t> plaintext(test_data.begin(), test_data.end());
    std::vector<uint8_t> ciphertext, decrypted;

    sgx_status_t ret = global_crypto->encrypt(plaintext, ciphertext);
    if (ret != SGX_SUCCESS) {
        ocall_print_string("Encryption failed");
        return ret;
    }

    ret = global_crypto->decrypt(ciphertext, decrypted);
    if (ret != SGX_SUCCESS) {
        ocall_print_string("Decryption failed");
        return ret;
    }

    if (plaintext == decrypted)
        ocall_print_string("Crypto test passed successfully!");
    else
        ocall_print_string("Crypto test failed!");

    return SGX_SUCCESS;
}



// ================================
// ORAM ECALL 实现
// ================================

sgx_status_t ecall_oram_initialize(int capacity) {
    if (!enclave_initialized || !global_crypto) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        // 创建 ORAM 实例
        g_oram = std::make_unique<ringoram>(capacity);
        
        // 设置加密工具
        g_oram->enclave_crypto = global_crypto;
        
        char msg[100];
        snprintf(msg, sizeof(msg), "ORAM initialized with capacity: %d", capacity);
        ocall_print_string(msg);
        
        return SGX_SUCCESS;
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "ORAM initialization failed: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_oram_access(int operation_type, int block_index,
                              const uint8_t* data, size_t data_size,
                              uint8_t* result, size_t result_size) {
    if (!g_oram) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        char msg[200];
        snprintf(msg, sizeof(msg), "ECALL_ORAM_ACCESS: op_type=%d, block_index=%d, data_size=%zu, result_size=%zu", 
                 operation_type, block_index, data_size, result_size);
        ocall_print_string(msg);
        
        // 转换操作类型
        ringoram::Operation op = static_cast<ringoram::Operation>(operation_type);
        
        // 转换数据格式
        std::vector<char> data_vec;
        if (data && data_size > 0) {
            data_vec.assign(reinterpret_cast<const char*>(data), 
                           reinterpret_cast<const char*>(data) + data_size);
            snprintf(msg, sizeof(msg), "Data vector created, size=%zu", data_vec.size());
            ocall_print_string(msg);
        } else {
            ocall_print_string("No data provided");
        }
        
        // 执行 ORAM 访问
        std::vector<char> result_vec = g_oram->access(block_index, op, data_vec);
       
        // 返回结果
        if (result && result_size >= result_vec.size()) {
            memcpy(result, result_vec.data(), result_vec.size());
          
        } else if (!result_vec.empty()) {
            snprintf(msg, sizeof(msg), "ERROR: Result buffer too small: need %zu, got %zu", 
                     result_vec.size(), result_size);
            ocall_print_string(msg);
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        return SGX_SUCCESS;
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "ORAM access failed: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_oram_evict() {
    if (!g_oram) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        g_oram->EvictPath();
        ocall_print_string("ORAM eviction completed");
        return SGX_SUCCESS;
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "ORAM eviction failed: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_test_ringoram_serialization() {
    if (!enclave_initialized) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        // 在enclave内创建bucket并测试序列化
        bucket test_bucket(realBlockEachbkt, dummyBlockEachbkt);
        
        // 添加测试数据
        std::string test_data = "Enclave Test Data";
        for (int i = 0; i < 3; i++) {
            std::vector<char> block_data(test_data.begin(), test_data.end());
            block real_block(i * 10, i, block_data);
            test_bucket.blocks[i] = real_block;
            test_bucket.ptrs[i] = i;
            test_bucket.valids[i] = 1;
        }
        
        // 测试序列化
        std::vector<uint8_t> serialized = g_oram->serialize_bucket(test_bucket);
        
        // 测试反序列化
        bucket deserialized = g_oram->deserialize_bucket(serialized.data(), serialized.size());
        
        // 验证结果
        bool success = true;
        for (int i = 0; i < 3; i++) {
            if (test_bucket.blocks[i].GetBlockindex() != deserialized.blocks[i].GetBlockindex() ||
                test_bucket.blocks[i].GetData() != deserialized.blocks[i].GetData()) {
                success = false;
                break;
            }
        }
        
        if (success) {
            ocall_print_string("Enclave ringoram serialization test PASSED");
        } else {
            ocall_print_string("Enclave ringoram serialization test FAILED");
        }
        
        return success ? SGX_SUCCESS : SGX_ERROR_UNEXPECTED;
        
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "Enclave serialization test exception: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}