#include "SGXEnclaveWrapper.h"
#include"ringoram.h"
#include"param.h"
#include <iostream>

int main() {
    try {
        SGXEnclaveWrapper enclave;
        
        // 初始化 Enclave
        if (!enclave.initializeEnclave()) {
            std::cerr << "Failed to initialize enclave" << std::endl;
            return -1;
        }
 
        enclave.testORAMBasic();
        enclave.testORAMAccess();
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
}