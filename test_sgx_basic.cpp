#include "SGXEnclaveWrapper.h"
#include <iostream>

int main() {
    try {
        SGXEnclaveWrapper enclave;
        
        // 初始化enclave
        if (!enclave.initializeEnclave()) {
            return 1;
        }
        
        // 测试基础功能
        std::cout << "\n=== Testing Basic Function ===" << std::endl;
        int result = enclave.testEnclave(42);
        std::cout << "Basic test result: " << result << std::endl;
        
        // 测试加密功能
        std::cout << "\n=== Testing Crypto ===" << std::endl;
        if (!enclave.testCrypto()) {
            return 1;
        }
        
        // 测试NodeSerializer
        std::cout << "\n=== Testing NodeSerializer ===" << std::endl;
        if (!enclave.testNodeSerializer()) {
            return 1;
        }
        
        std::cout << "\n=== All Tests PASSED! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}