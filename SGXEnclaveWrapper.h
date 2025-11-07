#ifndef SGX_ENCLAVE_WRAPPER_H
#define SGX_ENCLAVE_WRAPPER_H

#include <sgx_urts.h>
#include <string>
#include <stdexcept>
#include <vector>  
#include <cstdint> 

class SGXEnclaveWrapper {
private:
    sgx_enclave_id_t eid;
    bool initialized;
    
public:
    SGXEnclaveWrapper();
    ~SGXEnclaveWrapper();
    
    // 基础功能测试
    bool initializeEnclave(const std::string& enclave_path = "enclave.signed.so");
    int testEnclave(int input_value);

    // 加密功能测试
    bool testCrypto();

     // NodeSerializer功能测试
    bool testNodeSerializer();
 
    
    // 状态查询
    bool isInitialized() const { return initialized; }
    sgx_enclave_id_t getEnclaveId() const { return eid; }
};

#endif