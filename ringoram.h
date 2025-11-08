#pragma once
#include "block.h"
#include "bucket.h"
#include "CryptoUtil.h"
#include "param.h"
#include <vector>
#include <cmath>
#include <memory>


using namespace std;

// 序列化结构定义
#pragma pack(push, 1)
struct SerializedBucketHeader {
    int32_t Z;
    int32_t S;
    int32_t count;
    int32_t num_blocks;
};

struct SerializedBlockHeader {
    int32_t leaf_id;
    int32_t block_index;
    int32_t data_size;
    // 变长数据跟在后面
};
#pragma pack(pop)

class ringoram
{
public:
    static int round;
    static int G;
    
    int* positionmap;
    vector<block> stash;
    int c;
    
    
    EnclaveCryptoUtils* enclave_crypto;

    int N;
    int L;
    int num_bucket;
    int num_leaves;
    int cache_levels;

    enum Operation { READ, WRITE };
    
    
    ringoram(int n, int cache_levels = cacheLevel);

    bool isPositionCached(int position) const {
        return position < (1 << cache_levels) - 1;
    }
    
    // 原有方法
    int get_random();
    int Path_bucket(int leaf, int level);
    int GetlevelFromPos(int pos);
    block FindBlock(bucket bkt, int offset) const;
    int GetBlockOffset(bucket bkt, int blockindex) const;
    void ReadBucket(int pos);
    void WriteBucket(int position);
    block ReadPath(int leafid, int blockindex);
    void EvictPath();
    void EarlyReshuffle(int l);
    std::vector<char> encrypt_data(const std::vector<char>& data);
    std::vector<char> decrypt_data(const std::vector<char>& encrypted_data);
    vector<char> access(int blockindex, Operation op, vector<char> data);

    // SGX 存储访问方法
    bucket sgx_read_bucket(int position);
    void sgx_write_bucket(int position, const bucket& bkt);
    std::vector<uint8_t> serialize_bucket(const bucket& bkt);
    bucket deserialize_bucket(const uint8_t* data, size_t size);
    
    // 序列化工具方法
    size_t calculate_bucket_size(const bucket& bkt) const;
    void serialize_block(const block& blk, uint8_t* buffer, size_t& offset) const;
    block deserialize_block(const uint8_t* data, size_t& offset);
};

