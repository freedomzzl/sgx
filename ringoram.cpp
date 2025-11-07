#include "ringoram.h"
#include <cstring>
#include "CryptoUtil.h"
#include "param.h"
#include <cmath>
#include <sgx_trts.h>
#include <string.h>
  

extern "C" void ocall_print_string(const char* str);

using namespace std;

int ringoram::round = 0;
int ringoram::G = 0;

ringoram::ringoram(int n, int cache_levels)
    : N(n), L(static_cast<int>(ceil(log2(N)))), num_bucket((1 << (L + 1)) - 1), 
      num_leaves(1 << L), cache_levels(cache_levels) {
    
    c = 0;
    positionmap = new int[N];
    for (int i = 0; i < N; i++) {
        positionmap[i] = get_random();
    }

    // Enclave 内加密工具在 Enclave 初始化时设置
    enclave_crypto = nullptr;

    char msg[100];
    snprintf(msg, sizeof(msg), "[ORAM-SGX] Tree initialized for SGX, N=%d", n);
    ocall_print_string(msg);
}

// 使用 SGX 安全的随机数生成
int ringoram::get_random() {
    if (num_leaves <= 0) {
        return 0;
    }
    
    uint32_t random_value;
    sgx_status_t ret = sgx_read_rand((uint8_t*)&random_value, sizeof(random_value));
    
    if (ret != SGX_SUCCESS) {
        ocall_print_string("Warning: sgx_read_rand failed, using fallback");
        
    }
    
    return (int)(random_value % num_leaves);
}

int ringoram::Path_bucket(int leaf, int level) {
    int result = (1 << level) - 1 + (leaf >> (this->L - level));

    // 添加边界检查
    if (result < 0 || result >= num_bucket) {
        char error_msg[200];
        snprintf(error_msg, sizeof(error_msg), 
                "ERROR: Path_bucket calculated invalid position: %d (leaf=%d, level=%d, num_bucket=%d)", 
                result, leaf, level, num_bucket);
        ocall_print_string(error_msg);
        // 返回一个安全的默认值
        return 0;
    }

    return result;
}

int ringoram::GetlevelFromPos(int pos) {
    return (int)floor(log2(pos + 1));
}

block ringoram::FindBlock(bucket bkt, int offset) const{
    return bkt.blocks[offset];
}

int ringoram::GetBlockOffset(bucket bkt, int blockindex) const{
    for (int i = 0; i < (realBlockEachbkt + dummyBlockEachbkt); i++) {
        if (bkt.ptrs[i] == blockindex && bkt.valids[i] == 1) return i;
    }

    return bkt.GetDummyblockOffset();
}

void ringoram::ReadBucket(int pos) {
    // 直接使用 SGX 方法读取
    bucket bkt = sgx_read_bucket(pos);
    
    for (int j = 0; j < maxblockEachbkt; j++) {
        if (bkt.ptrs[j] != -1 && bkt.valids[j] && !bkt.blocks[j].IsDummy()) {
            block encrypted_block = bkt.blocks[j];
            vector<char> decrypted_data = decrypt_data(encrypted_block.GetData());
            block decrypted_block(encrypted_block.GetLeafid(), 
                                 encrypted_block.GetBlockindex(), 
                                 decrypted_data);
            stash.push_back(decrypted_block);
        }
    }
}

void ringoram::WriteBucket(int position) {
    int level = GetlevelFromPos(position);
    vector<block> blocksTobucket;

    // 从stash中选择blocks
    for (auto it = stash.begin(); it != stash.end() && blocksTobucket.size() < realBlockEachbkt; ) {
        int target_leaf = it->GetLeafid();
        int target_bucket_pos = Path_bucket(target_leaf, level);
        if (target_bucket_pos == position) {
            if (!it->IsDummy()) {
                vector<char> plain_data = it->GetData();
                vector<char> encrypted_data = encrypt_data(plain_data);
                block encrypted_block(it->GetLeafid(), it->GetBlockindex(), encrypted_data);
                blocksTobucket.push_back(encrypted_block);
            }
            it = stash.erase(it);
        } else {
            ++it;
        }
    }

    // 填充dummy blocks
    while (blocksTobucket.size() < realBlockEachbkt + dummyBlockEachbkt) {
        blocksTobucket.push_back(dummyBlock);
    }

    // 随机排列
    for (int i = blocksTobucket.size() - 1; i > 0; --i) {
        uint32_t j;
        sgx_read_rand((uint8_t*)&j, sizeof(j));
        j = j % (i + 1);
        
        // 交换元素
        block temp = blocksTobucket[i];
        blocksTobucket[i] = blocksTobucket[j];
        blocksTobucket[j] = temp;
    }

    // 创建新的bucket
    bucket bktTowrite(realBlockEachbkt, dummyBlockEachbkt);
    bktTowrite.blocks = blocksTobucket;

    for (int i = 0; i < maxblockEachbkt; i++) {
        bktTowrite.ptrs[i] = bktTowrite.blocks[i].GetBlockindex();
        bktTowrite.valids[i] = 1;
    }
    bktTowrite.count = 0;

    // 直接使用 SGX 方法写入
    sgx_write_bucket(position, bktTowrite);
}

block ringoram::ReadPath(int leafid, int blockindex) {
    block interestblock = dummyBlock;
    size_t blocks_this_read = 0;
    
    for (int i = 0; i <= L; i++) {
        int position = Path_bucket(leafid, i);
        bucket bkt = sgx_read_bucket(position);
        int offset = GetBlockOffset(bkt, blockindex);
        
        if (!isPositionCached(position)) {
            blocks_this_read += 1;
        }
        
        block blk = FindBlock(bkt, offset);
        bkt.valids[offset] = 0;
        bkt.count += 1;
        
        // 如果是目标块，记录下来，但仍继续走完路径
        if (blk.GetBlockindex() == blockindex) {
            interestblock = blk;
        }
        
        // 标记目标块为无效
        if (offset >= 0 && offset < maxblockEachbkt) {
            bkt.valids[offset] = 0;
            bkt.count += 1;
        }

        sgx_write_bucket(position, bkt);
    }

    return interestblock;
}

void ringoram::EvictPath() {
    int l = G % (1 << L);
    G += 1;

    for (int i = 0; i <= L; i++) {
        ReadBucket(Path_bucket(l, i));
    }

    for (int i = L; i >= 0; i--) {
        WriteBucket(Path_bucket(l, i));
    }
}

void ringoram::EarlyReshuffle(int l) {
    for (int i = 0; i <= L; i++) {
        int position = Path_bucket(l, i);
        bucket bkt = sgx_read_bucket(position);
        
        if (bkt.count >= dummyBlockEachbkt) {
            ReadBucket(position);
            WriteBucket(position);

            // 重置计数
            bkt = sgx_read_bucket(position);
            bkt.count = 0;
            sgx_write_bucket(position, bkt);
        }
    }
}

std::vector<char> ringoram::encrypt_data(const std::vector<char>& data) {
    if (!enclave_crypto || data.empty()) return data;

    vector<uint8_t> data_u8(data.begin(), data.end());
    vector<uint8_t> encrypted_u8;
    
    sgx_status_t ret = enclave_crypto->encrypt(data_u8, encrypted_u8);
    if (ret != SGX_SUCCESS) {
        ocall_print_string("[ENCRYPT] ERROR: SGX encryption failed");
        return data;
    }
    
    return vector<char>(encrypted_u8.begin(), encrypted_u8.end());
}

std::vector<char> ringoram::decrypt_data(const std::vector<char>& encrypted_data) {
    if (!enclave_crypto || encrypted_data.empty()) return encrypted_data;

    if (encrypted_data.size() % 16 != 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), 
                "[DECRYPT] ERROR: Size %zu not multiple of 16", encrypted_data.size());
        ocall_print_string(error_msg);
        return encrypted_data;
    }

    try {
        vector<uint8_t> encrypted_u8(encrypted_data.begin(), encrypted_data.end());
        vector<uint8_t> decrypted_u8;
        
        sgx_status_t ret = enclave_crypto->decrypt(encrypted_u8, decrypted_u8);
        if (ret != SGX_SUCCESS) {
            ocall_print_string("[DECRYPT] ERROR: SGX decryption failed");
            return encrypted_data;
        }
        
        return vector<char>(decrypted_u8.begin(), decrypted_u8.end());
    } catch (...) {
        ocall_print_string("[DECRYPT] ERROR: Exception during decryption");
        return encrypted_data;
    }
}

vector<char> ringoram::access(int blockindex, Operation op, vector<char> data)
{
    char msg[200];
    snprintf(msg, sizeof(msg), "=== ORAM ACCESS START: blockindex=%d, op=%d ===", blockindex, op);
    ocall_print_string(msg);
    
    if (blockindex < 0 || blockindex >= N) {
        ocall_print_string("ERROR: Invalid block index");
        return {};
    }

    int oldLeaf = positionmap[blockindex];
    positionmap[blockindex] = get_random();

    snprintf(msg, sizeof(msg), "Position map: oldLeaf=%d, newLeaf=%d", oldLeaf, positionmap[blockindex]);
    ocall_print_string(msg);

    // 1. 读取路径获取目标块
    snprintf(msg, sizeof(msg), "Calling ReadPath for leaf=%d, blockindex=%d", oldLeaf, blockindex);
    ocall_print_string(msg);
    
    block interestblock = ReadPath(oldLeaf, blockindex);
    
    snprintf(msg, sizeof(msg), "ReadPath returned, blockindex=%d, isDummy=%d", 
             interestblock.GetBlockindex(), interestblock.IsDummy());
    ocall_print_string(msg);

    vector<char> blockdata;

    // 2. 处理读取到的块
    if (interestblock.GetBlockindex() == blockindex) {
        ocall_print_string("Target block found in path");
        if (!interestblock.IsDummy()) {
            blockdata = decrypt_data(interestblock.GetData());
            ocall_print_string("Block decrypted successfully");
        }
        else {
            blockdata = interestblock.GetData();
            ocall_print_string("Block is dummy");
        }
    }
    else {
        ocall_print_string("Target block not in path, checking stash");
        // 3. 如果不在路径中，检查stash
        for (auto it = stash.begin(); it != stash.end(); ++it) {
            if (it->GetBlockindex() == blockindex) {
                blockdata = it->GetData();
                stash.erase(it);
                ocall_print_string("Block found in stash");
                break;
            }
        }
    }

    // 4. 如果是WRITE操作，更新数据
    if (op == WRITE) {
        ocall_print_string("WRITE operation, updating data");
        blockdata = data;
    }

    // 明文放入stash
    stash.emplace_back(positionmap[blockindex], blockindex, blockdata);
    snprintf(msg, sizeof(msg), "Block added to stash, stash size=%zu", stash.size());
    ocall_print_string(msg);

    // 5. 路径管理和驱逐
    round = (round + 1) % EvictRound;
    if (round == 0) {
        ocall_print_string("Triggering EvictPath");
        EvictPath();
    }

    ocall_print_string("Calling EarlyReshuffle");
    EarlyReshuffle(oldLeaf);

    ocall_print_string("=== ORAM ACCESS COMPLETED ===");
    
    // 调试返回数据
    
    snprintf(msg, sizeof(msg), "Returning data, size=%zu", blockdata.size());
    ocall_print_string(msg);
    
    if (!blockdata.empty()) {
        snprintf(msg, sizeof(msg), "First 10 bytes: ");
        for (int i = 0; i < std::min(10, (int)blockdata.size()); i++) {
            char byte_msg[10];
            snprintf(byte_msg, sizeof(byte_msg), "%02x ", (unsigned char)blockdata[i]);

        }
        ocall_print_string(msg);
    } else {
        ocall_print_string("Returning empty data");
    }
    
    return blockdata;
}

size_t ringoram::calculate_bucket_size(const bucket& bkt) const{
    size_t size = sizeof(SerializedBucketHeader);
    
    for (const auto& blk : bkt.blocks) {
        size += sizeof(SerializedBlockHeader) + blk.GetData().size();
    }
    
    return size;
}

void ringoram::serialize_block(const block& blk, uint8_t* buffer, size_t& offset) const{
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

block ringoram::deserialize_block(const uint8_t* data, size_t& offset) {
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

std::vector<uint8_t> ringoram::serialize_bucket(const bucket& bkt) {
    size_t total_size = calculate_bucket_size(bkt);
    std::vector<uint8_t> result(total_size);
    
    SerializedBucketHeader* bucket_header = reinterpret_cast<SerializedBucketHeader*>(result.data());
    bucket_header->Z = bkt.Z;
    bucket_header->S = bkt.S;
    bucket_header->count = bkt.count;
    bucket_header->num_blocks = static_cast<int32_t>(bkt.blocks.size());
    
    size_t offset = sizeof(SerializedBucketHeader);
    
    for (const auto& blk : bkt.blocks) {
        serialize_block(blk, result.data(), offset);
    }
    
    if (offset + (bkt.ptrs.size() + bkt.valids.size()) * sizeof(int32_t) <= total_size) {
        for (int ptr : bkt.ptrs) {
            *reinterpret_cast<int32_t*>(result.data() + offset) = ptr;
            offset += sizeof(int32_t);
        }
        
        for (int valid : bkt.valids) {
            *reinterpret_cast<int32_t*>(result.data() + offset) = valid;
            offset += sizeof(int32_t);
        }
    }
    
    return result;
}

bucket ringoram::deserialize_bucket(const uint8_t* data, size_t size) {
    if (size < sizeof(SerializedBucketHeader)) {
        ocall_print_string("Invalid bucket data: too small");
        // 返回一个默认的bucket而不是抛出异常
        return bucket(realBlockEachbkt, dummyBlockEachbkt);
    }
    
    const SerializedBucketHeader* bucket_header = reinterpret_cast<const SerializedBucketHeader*>(data);
    bucket result(bucket_header->Z, bucket_header->S);
    result.count = bucket_header->count;
    
    size_t offset = sizeof(SerializedBucketHeader);
    
    for (int i = 0; i < bucket_header->num_blocks && offset < size; i++) {
        result.blocks.push_back(deserialize_block(data, offset));
    }
    
    int num_slots = bucket_header->Z + bucket_header->S;
    if (offset + num_slots * 2 * sizeof(int32_t) <= size) {
        for (int i = 0; i < num_slots; i++) {
            int32_t ptr = *reinterpret_cast<const int32_t*>(data + offset);
            result.ptrs[i] = ptr;
            offset += sizeof(int32_t);
        }
        
        for (int i = 0; i < num_slots; i++) {
            int32_t valid = *reinterpret_cast<const int32_t*>(data + offset);
            result.valids[i] = valid;
            offset += sizeof(int32_t);
        }
    }
    
    return result;
}

// ================================
// SGX 存储访问方法
// ================================

extern "C" {
    sgx_status_t ocall_read_bucket(int* position, uint8_t* data);
    sgx_status_t ocall_write_bucket(int* position, const uint8_t* data);
}


bucket ringoram::sgx_read_bucket(int position) {
    char msg[200];
    snprintf(msg, sizeof(msg), "1. Entering sgx_read_bucket, position=%d", position);
    ocall_print_string(msg);
    
    const size_t BUFFER_SIZE = 4096;
    uint8_t buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    
    snprintf(msg, sizeof(msg), "2. Calling ocall_read_bucket with position pointer");
    ocall_print_string(msg);
    
    // 传递位置指针而不是值
    int pos = position;  // 创建局部变量
    sgx_status_t ret = ocall_read_bucket(&pos, buffer);
    
    snprintf(msg, sizeof(msg), "3. ocall_read_bucket returned: %d", ret);
    ocall_print_string(msg);
    
    if (ret != SGX_SUCCESS) {
        throw std::runtime_error("OCALL read bucket failed");
    }
    
    return deserialize_bucket(buffer, BUFFER_SIZE);
}

void ringoram::sgx_write_bucket(int position, const bucket& bkt) {
    char msg[200];
    snprintf(msg, sizeof(msg), "Before OCALL write bucket at position %d", position);
    ocall_print_string(msg);
    
    auto serialized = serialize_bucket(bkt);
    
    if (serialized.empty()) {
        throw std::runtime_error("Bucket serialization failed");
    }
    
    // 传递位置指针而不是值
    int pos = position;  // 创建局部变量
    sgx_status_t ret = ocall_write_bucket(&pos, serialized.data());
    
    if (ret != SGX_SUCCESS) {
        throw std::runtime_error("OCALL write bucket failed");
    }
}