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
#include"RingoramStorage.h"
#include "IRTree.h"
#include"param.h"

// 全局状态
static int enclave_initialized = 0;
static sgx_aes_gcm_128bit_key_t master_key;
static EnclaveCryptoUtils* global_crypto = nullptr;
static std::unique_ptr<ringoram> g_oram;
static std::unique_ptr<IRTree> g_irtree;


// ================================
// IRTree ECALL 实现
// ================================

sgx_status_t ecall_irtree_initialize(int dims, int min_cap, int max_cap) {
    if (!enclave_initialized) {
        ocall_print_string("Enclave not initialized");
        return SGX_ERROR_UNEXPECTED;
    }
    
    ocall_print_string("=== Starting IRTree initialization ===");
    
    try {
        // 检查是否已经有 IRTree 实例
        if (g_irtree) {
            ocall_print_string("IRTree already exists, cleaning up...");
            g_irtree.reset();
        }
        
        // 简化存储创建
        ocall_print_string("Creating RingOramStorage...");
        auto storage = std::make_shared<RingOramStorage>(totalnumRealblock, blocksize); // 使用较小的参数
        
        ocall_print_string("Creating IRTree instance...");
        g_irtree = std::make_unique<IRTree>(storage, dims, min_cap, max_cap);
        
        ocall_print_string("IRTree initialization completed successfully");
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "IRTree initialization exception: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_irtree_bulk_insert(const char* filename) {
    if (!g_irtree) {
        ocall_print_string("IRTree not initialized");
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        char msg[100];
        snprintf(msg, sizeof(msg), "Starting bulk insert from file: %s", filename);
        ocall_print_string(msg);
        
        g_irtree->optimizedBulkInsertFromFile(filename);
        
        snprintf(msg, sizeof(msg), "Bulk insert completed for file: %s", filename);
        ocall_print_string(msg);
        
        return SGX_SUCCESS;
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "Bulk insert failed: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_irtree_search(
    const char* keywords,
    const double* spatial_scope,
    int k,
    double alpha,
    int* result_count,
    int* doc_ids,     // 现在需要手动检查大小
    double* scores) { // 现在需要手动检查大小
    
    if (!g_irtree) {
        ocall_print_string("ERROR: IRTree not initialized");
        return SGX_ERROR_UNEXPECTED;
    }
    
    // 手动验证所有指针参数
    if (!keywords) {
        ocall_print_string("ERROR: Null keywords pointer");
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    if (!spatial_scope) {
        ocall_print_string("ERROR: Null spatial_scope pointer");
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    if (!result_count) {
        ocall_print_string("ERROR: Null result_count pointer");
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // 验证 k 值
    if (k <= 0) {
        ocall_print_string("ERROR: Invalid k value");
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // 验证输出缓冲区 - 现在需要手动检查
    if (k > 0) {
        if (!doc_ids) {
            ocall_print_string("ERROR: Null doc_ids pointer but k > 0");
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        if (!scores) {
            ocall_print_string("ERROR: Null scores pointer but k > 0");
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        // 手动验证缓冲区是否可写（简单检查）
        // 注意：在真实环境中可能需要更复杂的检查
        try {
            // 尝试写入第一个元素来测试缓冲区
            doc_ids[0] = -1;
            scores[0] = 0.0;
        } catch (...) {
            ocall_print_string("ERROR: Output buffers are not writable");
            return SGX_ERROR_INVALID_PARAMETER;
        }
    }
    
    // 验证 alpha 值
    if (alpha < 0.0 || alpha > 1.0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "ERROR: Invalid alpha value: %f", alpha);
        ocall_print_string(msg);
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    try {
        // 构建空间查询范围
        MBR query_scope(
            std::vector<double>{spatial_scope[0], spatial_scope[1]},  // min_x, min_y
            std::vector<double>{spatial_scope[2], spatial_scope[3]}   // max_x, max_y
        );
        
        // 验证空间范围
        if (query_scope.getMin()[0] >= query_scope.getMax()[0] || 
            query_scope.getMin()[1] >= query_scope.getMax()[1]) {
            ocall_print_string("ERROR: Invalid spatial scope dimensions");
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        // 将关键词字符串分割为vector<string>
        std::vector<std::string> keyword_list;
        if (strlen(keywords) > 0) {
            const char* str = keywords;
            const char* start = str;
            while (*str) {
                if (*str == ' ' || *str == ',') {
                    if (str > start) {
                        std::string term(start, str - start);
                        if (!term.empty()) {
                            keyword_list.push_back(term);
                        }
                    }
                    start = str + 1;
                }
                str++;
            }
            if (str > start) {
                std::string term(start, str - start);
                if (!term.empty()) {
                    keyword_list.push_back(term);
                }
            }
        }
        
        if (keyword_list.empty()) {
            ocall_print_string("WARNING: No valid keywords provided");
            *result_count = 0;
            return SGX_SUCCESS;
        }
        
        char msg[256];
        snprintf(msg, sizeof(msg), "Searching for %zu keywords: %s", 
                 keyword_list.size(), keywords);
        ocall_print_string(msg);
        
        // 执行搜索
        auto results = g_irtree->search(keyword_list, query_scope, k, alpha);
        
        // 设置结果计数
        *result_count = std::min(k, (int)results.size());
        
        // 手动填充输出数组 - 现在需要确保不越界
        for (int i = 0; i < *result_count; i++) {
            if (i < k) {  // 手动边界检查
                if (results[i].isData()) {
                    doc_ids[i] = results[i].document->getId();
                    scores[i] = results[i].score;
                } else {
                    doc_ids[i] = -1;
                    scores[i] = 0.0;
                }
            }
        }
        
        snprintf(msg, sizeof(msg), "Search completed: %d results found", *result_count);
        ocall_print_string(msg);
        
        return SGX_SUCCESS;
        
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "Search failed with exception: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_irtree_insert_document(
    const char* text,
    const double* location_min,
    const double* location_max) {
    
    if (!g_irtree) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        MBR doc_location(
            std::vector<double>{location_min[0], location_min[1]},
            std::vector<double>{location_max[0], location_max[1]}
        );
        
        g_irtree->insertDocument(text, doc_location);
        
        return SGX_SUCCESS;
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "Document insertion failed: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

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
        snprintf(msg, sizeof(msg), "ECALL_ORAM_ACCESS: op_type=%d, block_index=%d", 
                 operation_type, block_index);
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


sgx_status_t ecall_test_nodeserializer() {
    if (!enclave_initialized) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        ocall_print_string("=== Testing NodeSerializer inside Enclave ===");
        
        // 在enclave内创建和测试NodeSerializer
        MBR test_mbr({0.0, 0.0}, {10.0, 10.0});
        Node node(1, Node::LEAF, 0, test_mbr);
        
        // 创建测试文档
        MBR doc_mbr({1.0, 1.0}, {2.0, 2.0});
        auto doc = std::make_shared<Document>(100, doc_mbr, "SGX Enclave Test Document");
        node.addDocument(doc);
        
        // 序列化
        std::vector<uint8_t> serialized = NodeSerializer::serialize(node);
        
        char msg[100];
        snprintf(msg, sizeof(msg), "Node serialized, size: %zu bytes", serialized.size());
        ocall_print_string(msg);
        
        // 反序列化
        auto deserialized = NodeSerializer::deserialize(serialized);
        if (deserialized && deserialized->getId() == node.getId()) {
            ocall_print_string("NodeSerializer test PASSED inside enclave");
            return SGX_SUCCESS;
        } else {
            ocall_print_string("NodeSerializer test FAILED inside enclave");
            return SGX_ERROR_UNEXPECTED;
        }
        
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "NodeSerializer test exception: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

sgx_status_t ecall_test_ringoram_storage() {
    if (!enclave_initialized || !g_oram) {
        return SGX_ERROR_UNEXPECTED;
    }
    
    try {
        ocall_print_string("=== Testing RingOramStorage inside Enclave ===");
        
        // 在enclave内创建RingOramStorage实例
        RingOramStorage storage(1000, 1024); // capacity=1000, block_size=1024
        
        // 测试节点存储
        MBR test_mbr({0.0, 0.0}, {5.0, 5.0});
        Node test_node(1, Node::LEAF, 0, test_mbr);
        
        std::vector<uint8_t> node_data = NodeSerializer::serialize(test_node);
        
        // 存储节点
        bool store_result = storage.storeNode(1, node_data);
        if (!store_result) {
            ocall_print_string("Failed to store node in RingOramStorage");
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 读取节点
        std::vector<uint8_t> read_data = storage.readNode(1);
        if (read_data.empty()) {
            ocall_print_string("Failed to read node from RingOramStorage");
            return SGX_ERROR_UNEXPECTED;
        }
        
        // 验证数据
        auto read_node = NodeSerializer::deserialize(read_data);
        if (read_node && read_node->getId() == 1) {
            char msg[100];
            snprintf(msg, sizeof(msg), "RingOramStorage test PASSED, stored nodes: %d", 
                    storage.getStoredNodeCount());
            ocall_print_string(msg);
            return SGX_SUCCESS;
        } else {
            ocall_print_string("RingOramStorage test FAILED - data corruption");
            return SGX_ERROR_UNEXPECTED;
        }
        
    } catch (const std::exception& e) {
        char msg[200];
        snprintf(msg, sizeof(msg), "RingOramStorage test exception: %s", e.what());
        ocall_print_string(msg);
        return SGX_ERROR_UNEXPECTED;
    }
}

