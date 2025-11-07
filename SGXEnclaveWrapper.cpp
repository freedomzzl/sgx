#include "SGXEnclaveWrapper.h"
#include "SGXEnclave_u.h"
#include <iostream>
#include <cstring>
#include <vector>

// OCALL实现 - 在飞地外执行
extern "C" void ocall_print_string(const char* str) {
    std::cout << "[ENCLAVE OCALL]: " << str << std::endl;
}

SGXEnclaveWrapper::SGXEnclaveWrapper() : eid(0), initialized(false) {
}

SGXEnclaveWrapper::~SGXEnclaveWrapper() {
    if (initialized) {
        sgx_destroy_enclave(eid);
        std::cout << "SGX enclave destroyed" << std::endl;
    }
}

bool SGXEnclaveWrapper::initializeEnclave(const std::string& enclave_path) {
    sgx_status_t ret = SGX_SUCCESS;
    
    ret = sgx_create_enclave(enclave_path.c_str(), SGX_DEBUG_FLAG, NULL, NULL, &eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: " << std::hex << ret << std::endl;
        return false;
    }
    
    sgx_status_t ecall_ret = SGX_SUCCESS;
    ret = ecall_initialize_enclave(eid, &ecall_ret);
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "Failed to initialize enclave: sgx_ret=" << std::hex << ret 
                  << ", ecall_ret=" << ecall_ret << std::endl;
        sgx_destroy_enclave(eid);
        return false;
    }
    
    initialized = true;
    std::cout << "SGX enclave initialized successfully" << std::endl;
    return true;
}

int SGXEnclaveWrapper::testEnclave(int input_value) {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    int output_value = 0;
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_test_enclave(eid, &ecall_ret, input_value, &output_value);
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        throw std::runtime_error("ECALL failed: sgx_ret=" + std::to_string(ret) + 
                                ", ecall_ret=" + std::to_string(ecall_ret));
    }
    
    return output_value;
}

bool SGXEnclaveWrapper::testCrypto() {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_test_crypto(eid, &ecall_ret);
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "Crypto test failed: sgx_ret=" << std::hex << ret 
                  << ", ecall_ret=" << ecall_ret << std::endl;
        return false;
    }
    
    std::cout << "Crypto test passed successfully!" << std::endl;
    return true;
}


bool SGXEnclaveWrapper::testNodeSerializer() {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_test_nodeserializer(eid, &ecall_ret);
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "NodeSerializer test failed: sgx_ret=" << std::hex << ret 
                  << ", ecall_ret=" << ecall_ret << std::endl;
        return false;
    }
    
    std::cout << "NodeSerializer test passed successfully!" << std::endl;
    return true;
}
