#include "bucket.h"
#include "param.h"

#ifdef INSIDE_ENCLAVE
    // Enclave 内使用 SGX 安全的随机数
    #include <sgx_trts.h>
    extern "C" void ocall_print_string(const char* str);
#else
    // Host 端使用标准库
    #include <random>
    #include <cmath>
    #include <ctime>
    #include <cstdio>
#endif

bucket::bucket() :Z(realBlockEachbkt), S(dummyBlockEachbkt), blocks(Z + S, dummyBlock), count(0), ptrs(Z + S, -1), valids(Z + S, 1)
{
}

bucket::bucket(int Z, int S)
    :Z(Z), S(S), blocks(Z + S, dummyBlock), count(0), ptrs(Z + S, -1), valids(Z + S, 1)
{
}

int bucket::GetDummyblockOffset() const
{
    vector<int> dummyoffset;
    for (int i = 0; i < (Z + S); i++)
    {
        if (ptrs[i] == -1 && valids[i] == 1)
            dummyoffset.push_back(i);
    }
    if (dummyoffset.empty())
    {
#ifdef INSIDE_ENCLAVE
        ocall_print_string("no valid dummyblock");
#else
        printf("no valid dummyblock");
#endif
        return -1;
    }

#ifdef INSIDE_ENCLAVE
    // Enclave 内使用 SGX 安全的随机数
    uint32_t random_value;
    sgx_status_t ret = sgx_read_rand((uint8_t*)&random_value, sizeof(random_value));
    if (ret != SGX_SUCCESS) {
        return 0; // 失败时返回第一个位置
    }
    return dummyoffset[random_value % dummyoffset.size()];
#else
    // Host 端使用标准随机数
    static std::mt19937 rng(static_cast<unsigned>(time(nullptr)));
    std::uniform_int_distribution<int> dist(0, dummyoffset.size() - 1);
    return dummyoffset[dist(rng)];
#endif
}