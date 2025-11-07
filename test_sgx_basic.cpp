// 在 test_sgx_basic.cpp 的 main 函数中添加
#include "SGXEnclaveWrapper.h"
#include <iostream>

int main() {
    try {
        SGXEnclaveWrapper enclave;
        
        // 初始化 Enclave
        if (!enclave.initializeEnclave()) {
            std::cerr << "Failed to initialize enclave" << std::endl;
            return -1;
        }
        
        // 测试基础功能
        std::cout << "Testing basic enclave function..." << std::endl;
        int result = enclave.testEnclave(5);
        std::cout << "Basic test result: " << result << std::endl;
        
        // 测试加密功能
        std::cout << "Testing crypto..." << std::endl;
        if (enclave.testCrypto()) {
            std::cout << "Crypto test passed" << std::endl;
        } else {
            std::cerr << "Crypto test failed" << std::endl;
        }
        
        enclave.testORAMBasic();
        
        // 测试 ORAM 访问功能
        std::cout << "Testing ORAM access..." << std::endl;
        if (enclave.testORAMAccess()) {
            std::cout << "ORAM access test passed" << std::endl;
        } else {
            std::cerr << "ORAM access test failed" << std::endl;
        }
        
        std::cout << "All tests completed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
}