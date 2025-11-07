#include "SGXEnclave_t.h"
#include <sgx_trts.h>
#include <sgx_tcrypto.h>
#include <sgx_tseal.h>
#include <string>
#include <cstdio>
#include <vector>
#include "CryptoUtil.h"
#include "NodeSerializer.h"

// 全局状态
static int enclave_initialized = 0;
static sgx_aes_gcm_128bit_key_t master_key;
static EnclaveCryptoUtils* global_crypto = nullptr;

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
// NodeSerializer 测试函数
// ================================

sgx_status_t ecall_test_nodeserializer() {
    if (!enclave_initialized) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    // 创建测试节点
    MBR test_mbr({10.5, 20.3}, {15.2, 25.8});
    auto test_node = std::make_shared<Node>(123, Node::LEAF, 2, test_mbr);
    
    // 创建测试文档
    MBR doc_mbr({11.0, 21.0}, {12.0, 22.0});
    auto test_doc = std::make_shared<Document>(456, doc_mbr, "Test document content");
    test_node->addDocument(test_doc);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Testing NodeSerializer with node_id=%d", test_node->getId());
    ocall_print_string(msg);
    
    // 测试序列化
    std::vector<uint8_t> serialized_data;
    sgx_status_t ret = NodeSerializer::serialize(*test_node, serialized_data);
    
    if (ret != SGX_SUCCESS) {
        snprintf(msg, sizeof(msg), "Node serialization failed: %d", ret);
        ocall_print_string(msg);
        return ret;
    }
    
    snprintf(msg, sizeof(msg), "Node serialization successful. Size: %zu bytes", 
             serialized_data.size());
    ocall_print_string(msg);
    
    // 测试反序列化
    std::shared_ptr<Node> deserialized_node;
    ret = NodeSerializer::deserialize(serialized_data, deserialized_node);
    
    if (ret != SGX_SUCCESS || !deserialized_node) {
        snprintf(msg, sizeof(msg), "Node deserialization failed: %d", ret);
        ocall_print_string(msg);
        return ret;
    }
    
    snprintf(msg, sizeof(msg), "Node deserialization successful. Node ID: %d", 
             deserialized_node->getId());
    ocall_print_string(msg);
    
    ocall_print_string("NodeSerializer test passed successfully!");
    return SGX_SUCCESS;
}