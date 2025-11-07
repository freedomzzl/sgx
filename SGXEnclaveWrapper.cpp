#include "SGXEnclaveWrapper.h"
#include "SGXEnclave_u.h"
#include "ringoram.h"
#include "ServerStorage.h"
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
    
    std::cout << "Header size: " << sizeof(SerializedBucketHeader) << std::endl;
    
    for (const auto& blk : bkt.blocks) {
        size_t block_size = sizeof(SerializedBlockHeader) + blk.GetData().size();
        size += block_size;
        std::cout << "Block data size: " << blk.GetData().size() << ", total block size: " << block_size << std::endl;
    }
    
    // 添加 ptrs 和 valids 的大小
    size_t ptrs_valids_size = (bkt.ptrs.size() + bkt.valids.size()) * sizeof(int32_t);
    size += ptrs_valids_size;
    std::cout << "Ptrs+Valids size: " << ptrs_valids_size << std::endl;
    
    std::cout << "Total calculated size: " << size << std::endl;
    
    return size;
}

void serialize_block(const block& blk, uint8_t* buffer, size_t& offset) {
    std::cout << "Serializing block, current offset: " << offset << std::endl;
    
    SerializedBlockHeader* header = reinterpret_cast<SerializedBlockHeader*>(buffer + offset);
    header->leaf_id = blk.GetLeafid();
    header->block_index = blk.GetBlockindex();
    
    const auto& data = blk.GetData();
    header->data_size = static_cast<int32_t>(data.size());
    
    std::cout << "Block header: leaf_id=" << header->leaf_id 
              << ", block_index=" << header->block_index 
              << ", data_size=" << header->data_size << std::endl;
    
    offset += sizeof(SerializedBlockHeader);
    
    if (!data.empty()) {
        std::cout << "Copying block data, size: " << data.size() << std::endl;
        memcpy(buffer + offset, data.data(), data.size());
        offset += data.size();
    }
    
    std::cout << "Block serialized, new offset: " << offset << std::endl;
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
        std::cout << "Starting serialize_bucket..." << std::endl;
        
        size_t total_size = calculate_bucket_size(bkt);
        std::cout << "Total size calculated: " << total_size << std::endl;
        
        std::vector<uint8_t> result(total_size);
        std::cout << "Vector allocated" << std::endl;
        
        // 序列化 bucket header
        SerializedBucketHeader* bucket_header = reinterpret_cast<SerializedBucketHeader*>(result.data());
        bucket_header->Z = bkt.Z;
        bucket_header->S = bkt.S;
        bucket_header->count = bkt.count;
        bucket_header->num_blocks = static_cast<int32_t>(bkt.blocks.size());
        
        std::cout << "Header serialized: Z=" << bkt.Z << ", S=" << bkt.S 
                  << ", num_blocks=" << bkt.blocks.size() << std::endl;
        
        size_t offset = sizeof(SerializedBucketHeader);
        std::cout << "Initial offset: " << offset << std::endl;
        
        // 序列化 blocks
        for (int i = 0; i < bkt.blocks.size(); i++) {
            std::cout << "Serializing block " << i << std::endl;
            serialize_block(bkt.blocks[i], result.data(), offset);
            std::cout << "Block " << i << " serialized, offset now: " << offset << std::endl;
        }
        
        // 序列化 ptrs 和 valids
        int num_slots = bkt.Z + bkt.S;
        std::cout << "Serializing ptrs and valids, num_slots: " << num_slots << std::endl;
        
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
        
        std::cout << "Serialization completed, final offset: " << offset << std::endl;
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "serialize_bucket failed with exception: " << e.what() << std::endl;
        return std::vector<uint8_t>();
    }
}

bucket deserialize_bucket(const uint8_t* data, size_t size) {
    std::cout << "Starting deserialize_bucket, size: " << size << std::endl;
    
    if (size < sizeof(SerializedBucketHeader)) {
        std::cerr << "ERROR: Data too small for header" << std::endl;
        throw std::runtime_error("Invalid bucket data: too small");
    }
    
    const SerializedBucketHeader* bucket_header = reinterpret_cast<const SerializedBucketHeader*>(data);
    std::cout << "Bucket header: Z=" << bucket_header->Z << ", S=" << bucket_header->S 
              << ", count=" << bucket_header->count << ", num_blocks=" << bucket_header->num_blocks << std::endl;
    
    bucket result(bucket_header->Z, bucket_header->S);
    result.count = bucket_header->count;
    
    size_t offset = sizeof(SerializedBucketHeader);
    std::cout << "Initial offset: " << offset << std::endl;
    
    // 反序列化 blocks
    for (int i = 0; i < bucket_header->num_blocks && offset < size; i++) {
        std::cout << "Deserializing block " << i << std::endl;
        result.blocks.push_back(deserialize_block(data, offset));
        std::cout << "Block " << i << " deserialized, offset: " << offset << std::endl;
    }
    
    // 反序列化 ptrs 和 valids
    int num_slots = bucket_header->Z + bucket_header->S;
    std::cout << "Deserializing ptrs and valids, num_slots: " << num_slots << std::endl;
    
    // 反序列化 ptrs
    for (int i = 0; i < num_slots && offset + sizeof(int32_t) <= size; i++) {
        int32_t ptr = *reinterpret_cast<const int32_t*>(data + offset);
        result.ptrs[i] = ptr;
        offset += sizeof(int32_t);
    }
    
    // 反序列化 valids
    for (int i = 0; i < num_slots && offset + sizeof(int32_t) <= size; i++) {
        int32_t valid = *reinterpret_cast<const int32_t*>(data + offset);
        result.valids[i] = valid;
        offset += sizeof(int32_t);
    }
    
    std::cout << "Deserialization completed successfully" << std::endl;
    return result;
}

// ================================
// ORAM 存储访问 OCALL 实现（使用统一的序列化）
// ================================

extern "C" sgx_status_t ocall_read_bucket(
    int* position,  // 位置指针
    uint8_t* data) {
    
    std::cout << "=== OCALL_READ_BUCKET CALLED ===" << std::endl;
    std::cout << "Position pointer: " << position << ", Value: " << *position << std::endl;
    
    try {
        if (!g_external_storage) {
            std::cerr << "ERROR: External storage not initialized" << std::endl;
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 使用解引用的位置值
        int actual_position = *position;
        std::cout << "Actual position: " << actual_position << std::endl;
        
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
        std::cout << "OCALL_READ_BUCKET SUCCESS" << std::endl;
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return SGX_ERROR_UNEXPECTED;
    }
}

extern "C" sgx_status_t ocall_write_bucket(
    int* position,  // 位置指针
    const uint8_t* data) {
    
    std::cout << "=== OCALL_WRITE_BUCKET CALLED ===" << std::endl;
    std::cout << "Position pointer: " << position << ", Value: " << *position << std::endl;
    
    try {
        if (!g_external_storage) {
            std::cerr << "ERROR: External storage not initialized" << std::endl;
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 使用解引用的位置值
        int actual_position = *position;
        std::cout << "Actual position: " << actual_position << std::endl;
        
        if (actual_position < 0 || actual_position >= g_external_storage->GetCapacity()) {
            std::cerr << "ERROR: Invalid bucket position: " << actual_position << std::endl;
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        // 使用固定大小反序列化
        const size_t BUFFER_SIZE = 4096;
        bucket bkt_to_write = deserialize_bucket(data, BUFFER_SIZE);
        
        g_external_storage->SetBucket(actual_position, bkt_to_write);
        
        std::cout << "OCALL_WRITE_BUCKET SUCCESS" << std::endl;
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return SGX_ERROR_UNEXPECTED;
    }
}

// ================================
// 外部存储初始化函数
// ================================

bool initialize_external_storage(int capacity) {
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
    int capacity = 1024;
    if (!initialize_external_storage(capacity)) {
        return false;
    }
    
    // 初始化 Enclave 内的 ORAM
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_oram_initialize(eid, &ecall_ret, capacity);
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "ORAM initialization failed: sgx_ret=" << std::hex << ret 
                  << ", ecall_ret=" << ecall_ret << std::endl;
        return false;
    }
    
    std::cout << "ORAM basic test passed: initialization successful" << std::endl;
    return true;
}

bool SGXEnclaveWrapper::testORAMAccess() {
    if (!initialized) {
        throw std::runtime_error("Enclave not initialized");
    }
    
    // 测试数据
    std::string test_data = "Hello ORAM Test Data";
    std::vector<uint8_t> write_data(test_data.begin(), test_data.end());
    uint8_t write_result[256];  // 为 WRITE 操作添加结果缓冲区
    uint8_t read_result[256];
    memset(write_result, 0, sizeof(write_result));
    memset(read_result, 0, sizeof(read_result));
    
    std::cout << "=== ORAM ACCESS TEST START ===" << std::endl;
    std::cout << "Test data: '" << test_data << "'" << std::endl;
    std::cout << "Write data size: " << write_data.size() << std::endl;
    
    // 测试写入操作 - 提供结果缓冲区
    sgx_status_t ecall_ret = SGX_SUCCESS;
    sgx_status_t ret = ecall_oram_access(eid, &ecall_ret, 
                                        1,  // WRITE operation
                                        0,  // block index 0
                                        write_data.data(), 
                                        write_data.size(),
                                        write_result,  // 提供结果缓冲区
                                        sizeof(write_result));
    
    std::cout << "Write operation - sgx_ret: " << ret << ", ecall_ret: " << ecall_ret << std::endl;
    
    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        std::cerr << "ORAM write test failed" << std::endl;
        return false;
    }
    
    std::cout << "Write operation completed successfully" << std::endl;
    
    // 检查 WRITE 操作的返回数据
    std::string write_return_string(reinterpret_cast<char*>(write_result));
    std::cout << "Write operation returned: '" << write_return_string << "'" << std::endl;
    
    // 测试读取操作
    ret = ecall_oram_access(eid, &ecall_ret,
                           0,  // READ operation  
                           0,  // block index 0
                           nullptr,
                           0,
                           read_result,
                           sizeof(read_result));
    
    std::cout << "Read operation - sgx_ret: " << ret << ", ecall_ret: " << ecall_ret << std::endl;
    
    if (ret == SGX_SUCCESS && ecall_ret == SGX_SUCCESS) {
        // 检查读取结果
        std::string read_string(reinterpret_cast<char*>(read_result));
        std::cout << "Read result: '" << read_string << "'" << std::endl;
        
        if (read_string == test_data) {
            std::cout << "ORAM access test passed: write and read successful" << std::endl;
            return true;
        } else {
            std::cerr << "ORAM read test failed: data mismatch" << std::endl;
            return false;
        }
    } else {
        std::cerr << "ORAM read test failed: operation failed" << std::endl;
        return false;
    }
}