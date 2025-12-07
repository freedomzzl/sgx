#include "SGXEnclave_t.h"
#include "ringoram.h"
#include <cstring>
#include "CryptoUtil.h"
#include "param.h"
#include <cmath>
#include <sgx_trts.h>
#include <string.h>

  



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
		// 更严格的检查：只读取真实且有效的块
		if (bkt.ptrs[j] != -1 && bkt.valids[j] && !bkt.blocks[j].IsDummy()) {
			// 读取时解密
			block encrypted_block = bkt.blocks[j];
			vector<char> decrypted_data = decrypt_data(encrypted_block.GetData());
			block decrypted_block(encrypted_block.GetLeafid(), encrypted_block.GetBlockindex(), decrypted_data);
			stash.push_back(decrypted_block);
		}
	}
}

void ringoram::WriteBucket(int position) {
    int level = GetlevelFromPos(position);
	vector<block> blocksTobucket;

	// 从stash中选择可以放在这个bucket的块
	for (auto it = stash.begin(); it != stash.end() && blocksTobucket.size() < realBlockEachbkt; ) {
		int target_leaf = it->GetLeafid();
		int target_bucket_pos = Path_bucket(target_leaf, level);
		if (target_bucket_pos == position) {
			// 对要写回当前bucket的块进行加密
			if (!it->IsDummy()) {
				vector<char> plain_data = it->GetData();  // 当前是明文
				vector<char> encrypted_data = encrypt_data(plain_data);

				// 创建加密后的block
				block encrypted_block(it->GetLeafid(), it->GetBlockindex(), encrypted_data);
				blocksTobucket.push_back(encrypted_block);
			}
			it = stash.erase(it);
		}
		else {
			++it;
		}
	}

	// 填充dummy块
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

// block ringoram::ReadPath(int leafid, int blockindex) {
//     block interestblock = dummyBlock;
//     size_t blocks_this_read = 0;
    
//     for (int i = 0; i <= L; i++) {
//         int position = Path_bucket(leafid, i);
//         bucket bkt = sgx_read_bucket(position);
//         int offset = GetBlockOffset(bkt, blockindex);
        
//         if (!isPositionCached(position)) {
//             blocks_this_read += 1;
//         }
        
//         block blk = FindBlock(bkt, offset);
//         bkt.valids[offset] = 0;
//         bkt.count += 1;
        
//         // 如果是目标块，记录下来，但仍继续走完路径
//         if (blk.GetBlockindex() == blockindex) {
//             interestblock = blk;
//         }
        
//         // 标记目标块为无效
//         if (offset >= 0 && offset < maxblockEachbkt) {
//             bkt.valids[offset] = 0;
//             bkt.count += 1;
//         }
        
//         sgx_write_bucket(position, bkt);
//     }

//     return interestblock;
// }

block ringoram::ReadPath(int leafid, int blockindex)
{
    uint8_t buffer[4096] = {0};
    int is_dummy = 0;
    size_t actual_data_size = 0; 
   
    sgx_status_t ocall_ret = SGX_SUCCESS;
    sgx_status_t ret = ocall_read_path(&ocall_ret, leafid, blockindex, &is_dummy, buffer, &actual_data_size);
 
    if (ret != SGX_SUCCESS || ocall_ret != SGX_SUCCESS) {
        ocall_print_string("ReadPath: OCALL failed");
        return dummyBlock;
    }


    if (is_dummy) {
        return dummyBlock;
    }
 
    std::vector<char> encrypted_data(buffer, buffer + actual_data_size);
    return block(leafid, blockindex, encrypted_data);
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

            for (int j = 0; j < maxblockEachbkt; j++) {
		        // 只读取真实且有效的块
		        if (bkt.ptrs[j] != -1 && bkt.valids[j] && !bkt.blocks[j].IsDummy()) {
			        // 读取时解密
			        block encrypted_block = bkt.blocks[j];
			        vector<char> decrypted_data = decrypt_data(encrypted_block.GetData());
			        block decrypted_block(encrypted_block.GetLeafid(), encrypted_block.GetBlockindex(), decrypted_data);
			        stash.push_back(decrypted_block);
		        }
	        }

            WriteBucket(position);
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
	if (blockindex < 0 || blockindex >= N) {

		return {};
	}

	int oldLeaf = positionmap[blockindex];
	positionmap[blockindex] = get_random();

	// 1. 读取路径获取目标块（加密状态）
	block interestblock = ReadPath(oldLeaf, blockindex);
	vector<char> blockdata;

	// 2. 处理读取到的块
	if (interestblock.GetBlockindex() == blockindex) {
		// 从路径读取到的目标块，需要解密
		if (!interestblock.IsDummy()) {
			blockdata = decrypt_data(interestblock.GetData());
		}
		else {
			blockdata = interestblock.GetData();
		}
	}
	else {
		// 3. 如果不在路径中，检查stash
		for (auto it = stash.begin(); it != stash.end(); ++it) {
			if (it->GetBlockindex() == blockindex) {
				blockdata = it->GetData();   // stash中已经是明文
				stash.erase(it);
				break;
			}
		}
	}

	// 4. 如果是WRITE操作，更新数据
	if (op == WRITE) {
		blockdata = data;
	}

	// 明文放入stash
	stash.emplace_back(positionmap[blockindex], blockindex, blockdata);

	// 5. 路径管理和驱逐
	round = (round + 1) % EvictRound;
	if (round == 0) EvictPath();

	EarlyReshuffle(oldLeaf);

	return blockdata;
}


size_t ringoram::calculate_bucket_size(const bucket& bkt) const{
    size_t size = sizeof(SerializedBucketHeader);
    
    for (const auto& blk : bkt.blocks) {
        size += sizeof(SerializedBlockHeader) + blk.GetData().size();
    }
    
    // 添加ptrs和valids的大小
    size_t ptrs_valids_size = (bkt.ptrs.size() + bkt.valids.size()) * sizeof(int32_t);
    size += ptrs_valids_size;
    
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
        return bucket(realBlockEachbkt, dummyBlockEachbkt);
    }
    
    const SerializedBucketHeader* bucket_header = reinterpret_cast<const SerializedBucketHeader*>(data);
    
    // 创建bucket但不预先分配blocks
    bucket result(0, 0);  // 先创建空的
    result.Z = bucket_header->Z;
    result.S = bucket_header->S;
    result.count = bucket_header->count;
    
    // 清空blocks，重新从序列化数据填充
    result.blocks.clear();
    
    size_t offset = sizeof(SerializedBucketHeader);
    
    // 反序列化 blocks
    for (int i = 0; i < bucket_header->num_blocks && offset < size; i++) {
        result.blocks.push_back(deserialize_block(data, offset));
    }
    
    // 重新初始化ptrs和valids
    int num_slots = result.Z + result.S;
    result.ptrs.resize(num_slots, -1);
    result.valids.resize(num_slots, 0);
    
    // 反序列化 ptrs 和 valids
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

bucket ringoram::sgx_read_bucket(int position) {
    
    const size_t BUFFER_SIZE = 65536;
    uint8_t buffer[BUFFER_SIZE];

    // ocall 的封装函数第一个参数是用于接收 host 实现返回值的 sgx_status_t*
    sgx_status_t ocall_ret = SGX_SUCCESS;
    sgx_status_t ret = ocall_read_bucket(&ocall_ret, position, buffer);

    if (ret != SGX_SUCCESS) {
        ocall_print_string("SGX: ocall_read_bucket failed at runtime level");
        throw std::runtime_error("OCALL runtime failure (ocall_read_bucket)");
    }
    if (ocall_ret != SGX_SUCCESS) {
        ocall_print_string("SGX: ocall_read_bucket reported host-level failure");
        throw std::runtime_error("OCALL host-level failure (ocall_read_bucket)");
    }

    return deserialize_bucket(buffer, BUFFER_SIZE);
}

void ringoram::sgx_write_bucket(int position, const bucket& bkt) {
    const size_t BUFFER_SIZE = 65536;

    // 序列化
    auto serialized = serialize_bucket(bkt);
    if (serialized.empty()) {
        ocall_print_string("SGX: serialize_bucket returned empty");
        throw std::runtime_error("Bucket serialization failed");
    }

    
    if (serialized.size() > BUFFER_SIZE) {
        char errbuf[200];
        snprintf(errbuf, sizeof(errbuf), "SGX: serialized bucket too large: %zu > %zu", serialized.size(), BUFFER_SIZE);
        ocall_print_string(errbuf);
        throw std::runtime_error("Serialized bucket larger than allowed buffer ");
    }

    // 调用 ocall（第一个参数为接收 host 返回值的指针）
    sgx_status_t ocall_ret = SGX_SUCCESS;
    sgx_status_t ret = ocall_write_bucket(&ocall_ret, position, serialized.data());

    if (ret != SGX_SUCCESS) {
        ocall_print_string("SGX: ocall_write_bucket failed at runtime level");
        throw std::runtime_error("OCALL runtime failure (ocall_write_bucket)");
    }
    if (ocall_ret != SGX_SUCCESS) {
        ocall_print_string("SGX: ocall_write_bucket reported host-level failure");
        throw std::runtime_error("OCALL host-level failure (ocall_write_bucket)");
    }
}