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
    bool initialize_external_storage(int capacity);
    // 加密功能测试
    bool testCrypto();
    bool testNodeSerializer();
    bool testRingOramStorage();

    bool testORAMBasic();
    bool testORAMAccess();

    // IRTree 相关方法
    bool initializeIRTree(int dims = 2, int min_cap = 2, int max_cap = 4);
    bool bulkInsertFromFile(const std::string& filename);
    std::vector<std::pair<int, double>> search(
        const std::string& keywords,
        double min_x, double min_y, double max_x, double max_y,
        int k = 10, double alpha = 0.5);
    bool insertDocument(
        const std::string& text,
        double min_x, double min_y, double max_x, double max_y);
    
    // 状态查询
    bool isInitialized() const { return initialized; }
    sgx_enclave_id_t getEnclaveId() const { return eid; }
};

#endif