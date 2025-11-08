#include "SGXEnclaveWrapper.h"
#include "SGXEnclave_u.h"
#include "ringoram.h"
#include "ServerStorage.h"
#include"param.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <memory>

using namespace std;


// 全局外部存储实例
static std::unique_ptr<ServerStorage> g_external_storage;

// OCALL实现 - 在飞地外执行
extern "C" void ocall_print_string(const char* str) {
    std::cout << "[ENCLAVE OCALL]: " << str << std::endl;
}


// ================================
// 序列化工具函数
// ================================

size_t calculate_bucket_size(const bucket& bkt) {
    
    size_t size = sizeof(SerializedBucketHeader);
    
    // 计算blocks大小
    size_t blocks_size = 0;
    for (int i = 0; i < bkt.blocks.size(); i++) {
        const auto& blk = bkt.blocks[i];
        size_t block_size = sizeof(SerializedBlockHeader) + blk.GetData().size();
        blocks_size += block_size;
      
    }
    size += blocks_size;
    
    // 计算ptrs和valids大小
    size_t ptrs_valids_size = (bkt.ptrs.size() + bkt.valids.size()) * sizeof(int32_t);
    size += ptrs_valids_size;
 
    return size;
}

void serialize_block(const block& blk, uint8_t* buffer, size_t& offset) {
  
    SerializedBlockHeader* header = reinterpret_cast<SerializedBlockHeader*>(buffer + offset);
    header->leaf_id = blk.GetLeafid();
    header->block_index = blk.GetBlockindex();
    
    const auto& data = blk.GetData();
    header->data_size = static_cast<int32_t>(data.size());
 
    offset += sizeof(SerializedBlockHeader);
    
    if (!data.empty()) {
        memcpy(buffer + offset, data.data(), data.size());
        offset += data.size();
    }

}

block deserialize_block(const uint8_t* data, size_t& offset) {
    const SerializedBlockHeader* header = reinterpret_cast<const SerializedBlockHeader*>(data + offset);
    offset += sizeof(SerializedBlockHeader);
    
    std::vector<char> block_data;
    if (header->data_size > 0) {
        block_data.resize(header->data_size);
        memcpy(block_data.data(), data + offset, header->data_size);
        offset += header->data_size;
    }
    
    return block(header->leaf_id, header->block_index, block_data);
}

std::vector<uint8_t> serialize_bucket(const bucket& bkt) {
   
    try {
    
        // 先计算大小

        size_t total_size = calculate_bucket_size(bkt);
     
        if (total_size == 0) {
            std::cerr << "ERROR: Calculated size is 0" << std::endl;
            return std::vector<uint8_t>();
        }
       
        std::vector<uint8_t> result(total_size);
      
        // 序列化 bucket header
      
        SerializedBucketHeader* bucket_header = reinterpret_cast<SerializedBucketHeader*>(result.data());
        bucket_header->Z = bkt.Z;
        bucket_header->S = bkt.S;
        bucket_header->count = bkt.count;
        bucket_header->num_blocks = static_cast<int32_t>(bkt.blocks.size());
       
        size_t offset = sizeof(SerializedBucketHeader);
       
        // 序列化 blocks
    
        for (int i = 0; i < bkt.blocks.size(); i++) {
          
            serialize_block(bkt.blocks[i], result.data(), offset);
       
        }
        
        // 序列化 ptrs 和 valids
   
        int num_slots = bkt.Z + bkt.S;
 
        // 检查边界
        if (offset + num_slots * 2 * sizeof(int32_t) > total_size) {
            std::cerr << "ERROR: Not enough space for ptrs and valids" << std::endl;
            return std::vector<uint8_t>();
        }
        
        // 序列化 ptrs
        for (int i = 0; i < num_slots; i++) {
            *reinterpret_cast<int32_t*>(result.data() + offset) = bkt.ptrs[i];
            offset += sizeof(int32_t);
        }
        
        // 序列化 valids
        for (int i = 0; i < num_slots; i++) {
            *reinterpret_cast<int32_t*>(result.data() + offset) = bkt.valids[i];
            offset += sizeof(int32_t);
        }
 
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "serialize_bucket failed with exception: " << e.what() << std::endl;
        return std::vector<uint8_t>();
    }
}

bucket deserialize_bucket(const uint8_t* data, size_t size) {
 
    if (size < sizeof(SerializedBucketHeader)) {
        std::cerr << "  ERROR: Data too small for header" << std::endl;
        throw std::runtime_error("Invalid bucket data: too small");
    }
    
    const SerializedBucketHeader* bucket_header = reinterpret_cast<const SerializedBucketHeader*>(data);
  
    // 创建空的bucket
    bucket result(0, 0);
    result.Z = bucket_header->Z;
    result.S = bucket_header->S;
    result.count = bucket_header->count;
    
    size_t offset = sizeof(SerializedBucketHeader);
 
    // 反序列化 blocks
  
    for (int i = 0; i < bucket_header->num_blocks && offset < size; i++) {
        result.blocks.push_back(deserialize_block(data, offset));
    }
 
    //从序列化数据中恢复ptrs和valids
    int num_slots = result.Z + result.S;
    result.ptrs.resize(num_slots, -1);
    result.valids.resize(num_slots, 0);
    
    // 检查是否有足够的空间来读取ptrs和valids
    if (offset + num_slots * 2 * sizeof(int32_t) <= size) {
     
        // 反序列化 ptrs
        for (int i = 0; i < num_slots; i++) {
            int32_t ptr = *reinterpret_cast<const int32_t*>(data + offset);
            result.ptrs[i] = ptr;
            offset += sizeof(int32_t);
        }
        
        // 反序列化 valids
        for (int i = 0; i < num_slots; i++) {
            int32_t valid = *reinterpret_cast<const int32_t*>(data + offset);
            result.valids[i] = valid;
            offset += sizeof(int32_t);
        }
   
    } else {
        std::cout << "  WARNING: No ptrs and valids data in serialized bucket" << std::endl;
    }
   
    return result;
}

void checkServerStorageState(int position, const std::string& context) {
    if (!g_external_storage) {
        std::cout << "[" << context << "] ServerStorage not initialized" << std::endl;
        return;
    }
    
    std::cout << "=== SERVERSTORAGE CHECK: " << context << " ===" << std::endl;
    
    try {
        bucket stored_bucket = g_external_storage->GetBucket(position);
       
        int real_blocks = 0;
        for (int i = 0; i < stored_bucket.blocks.size(); i++) {
            const auto& blk = stored_bucket.blocks[i];
            if (blk.GetBlockindex() != -1) {
                real_blocks++;
                std::vector<char> data = blk.GetData();
                std::string data_str(data.begin(), data.end());
                std::cout << "  REAL Block " << i << ": index=" << blk.GetBlockindex() 
                          << ", data_size=" << data.size();
                if (data.size() <= 50) {  // 只显示小数据
                    std::cout << ", data='" << data_str << "'";
                }
                std::cout << std::endl;
            }
        }
        std::cout << "Total real blocks: " << real_blocks << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR reading ServerStorage: " << e.what() << std::endl;
    }
}
// ================================
// ORAM 存储访问 OCALL 实现（使用统一的序列化）
// ================================

extern "C" sgx_status_t ocall_read_bucket(
    int position,  // 位置指针
    uint8_t* data) {
    
 
    try {
        if (!g_external_storage) {
            std::cerr << "ERROR: External storage not initialized" << std::endl;
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 使用解引用的位置值
        int actual_position = position;
 
        if (actual_position < 0 || actual_position >= g_external_storage->GetCapacity()) {
            std::cerr << "ERROR: Invalid bucket position: " << actual_position << std::endl;
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        bucket bkt = g_external_storage->GetBucket(actual_position);
        std::vector<uint8_t> serialized = serialize_bucket(bkt);
        
        const size_t BUFFER_SIZE = 4096;
        if (serialized.size() > BUFFER_SIZE) {
            std::cerr << "ERROR: Serialized data too large" << std::endl;
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        memcpy(data, serialized.data(), serialized.size());
 
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return SGX_ERROR_UNEXPECTED;
    }
}

extern "C" sgx_status_t ocall_write_bucket(int position, const uint8_t* data) {
    
    
    try {
        if (!g_external_storage) {
            std::cerr << "ERROR: External storage not initialized" << std::endl;
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 反序列化要写入的数据
        bucket bkt_to_write = deserialize_bucket(data, 4096);
        
        // 检查要写入的块
        int real_blocks_to_write = 0;
        for (int i = 0; i < bkt_to_write.blocks.size(); i++) {
            const auto& blk = bkt_to_write.blocks[i];
            if (blk.GetBlockindex() != -1) {
                real_blocks_to_write++;
               
            }
        }
        
        // // 写入前检查该位置当前状态
        // std::cout << "Before write - ";
        // checkServerStorageState(position, "BEFORE_WRITE");
        
        // 执行写入
        g_external_storage->SetBucket(position, bkt_to_write);
        
        // // 写入后立即验证
        // std::cout << "After write - ";
        // checkServerStorageState(position, "AFTER_WRITE");
        
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in ocall_write_bucket: " << e.what() << std::endl;
        return SGX_ERROR_UNEXPECTED;
    }
}

// ================================
// 外部存储初始化函数
// ================================

bool SGXEnclaveWrapper::initialize_external_storage(int capacity) {
    try {
        g_external_storage = std::make_unique<ServerStorage>();
        g_external_storage->setCapacity(capacity);
        
        // 初始化所有 bucket
        for (int i = 0; i < capacity; i++) {
            bucket init_bkt(realBlockEachbkt, dummyBlockEachbkt);
            g_external_storage->SetBucket(i, init_bkt);
        }
        
        std::cout << "External storage initialized with capacity: " << capacity << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize external storage: " << e.what() << std::endl;
        return false;
    }
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

bool SGXEnclaveWrapper::testORAMBasic() {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    // 初始化外部存储
    if (!initialize_external_storage(capacity)) {
        return false;
    }
    
    // 初始化 Enclave 内的 ORAM
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_oram_initialize(eid, &ecall_ret, 100);
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "ORAM initialization failed: sgx_ret=" << std::hex << ret 
                  << ", ecall_ret=" << ecall_ret << std::endl;
        return false;
    }
    
    std::cout << "ORAM initialization successful" << std::endl;
    return true;
}

bool SGXEnclaveWrapper::testORAMAccess() {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    // 初始化外部存储
    if (!initialize_external_storage(capacity)) {
        return false;
    }
    
    // 测试数据
    std::string test_data = "Hello ORAM Test Data";
    std::vector<uint8_t> write_data(test_data.begin(), test_data.end());
    uint8_t write_result[256];
    uint8_t read_result[256];
 
    // 写入blocks
    for(int i = 0; i < 5; i++) {
        
        memset(write_result, 0, sizeof(write_result));
        sgx_status_t ecall_ret = SGX_SUCCESS;
        std::cout<<"access write"<<std::endl;
        sgx_status_t ret = ecall_oram_access(eid, &ecall_ret, 
                                            1,  // WRITE
                                            i,  
                                            write_data.data(), 
                                            write_data.size(),
                                            write_result,  
                                            sizeof(write_result));
        
        if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
            std::cerr << "Write failed at block " << i << std::endl;
            return false;
        }
        
        // // 每次写入后检查ServerStorage状态
        // std::cout << "After writing block " << i << ":" << std::endl;
        // for (int j = 0; j < 5; j++) {
        //     checkServerStorageState(j, "AFTER_WRITE_" + std::to_string(i));
        // }
    }
    
    // 读取blocks
    for(int i = 0; i < 5; i++) {
        std::cout << "\n=== READING BLOCK " << i << " =========================" << std::endl;
        
        memset(read_result, 0, sizeof(read_result));
        sgx_status_t ecall_ret = SGX_SUCCESS;
        sgx_status_t ret = ecall_oram_access(eid, &ecall_ret,
                                            0,  // READ
                                            i,  
                                            nullptr,
                                            0,
                                            read_result,
                                            sizeof(read_result));
        
        if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
            std::cerr << "Read failed at block " << i << std::endl;
            return false;
        }
        
        std::string read_string(reinterpret_cast<char*>(read_result));
        std::cout << "Read block" << i << ": '" << read_string << "'" << std::endl;
        
        if (read_string != test_data) {
            std::cerr << "Data mismatch at block " << i << std::endl;
            return false;
        }
    }
    
    std::cout << "=== ORAM BULK ACCESS TEST COMPLETED ===" << std::endl;
    return true;
}

