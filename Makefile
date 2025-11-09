# ======================================
# Intel SGX Makefile (Unified Build)
# ======================================

SGX_SDK ?= /opt/intel/sgxsdk
SGX_ARCH ?= x64
SGX_MODE ?= HW

CXX = g++
CC = gcc

.PHONY: all clean keys test

# ======================================
# 源文件分类
# ======================================

# Enclave 专属源文件（在 Enclave 内运行的算法）
ENCLAVE_SRC_CPP := SGXEnclave.cpp CryptoUtil.cpp NodeSerializer.cpp Node.cpp MBR.cpp Document.cpp ringoram.cpp Vocabulary.cpp Vector.cpp Query.cpp InvertedIndex.cpp RingoramStorage.cpp IRTree.cpp
ENCLAVE_SRC_C   := SGXEnclave_t.c

# Host 专属源文件（在外部运行的服务）
HOST_SRC_CPP := SGXEnclaveWrapper.cpp ServerStorage.cpp test_sgx_basic.cpp
HOST_SRC_C   := SGXEnclave_u.c

# 共享源文件（在两边都需要编译）
SHARED_SRC_CPP := bucket.cpp block.cpp param.cpp 

# 对象文件（避免命名冲突）
ENCLAVE_OBJS := $(ENCLAVE_SRC_CPP:.cpp=.enclave.o) $(SHARED_SRC_CPP:.cpp=.enclave.o) $(ENCLAVE_SRC_C:.c=.enclave.o)
APP_OBJS     := $(HOST_SRC_CPP:.cpp=.host.o) $(SHARED_SRC_CPP:.cpp=.host.o) $(HOST_SRC_C:.c=.host.o)

HEADERS := $(wildcard *.h)

# ======================================
# 编译标志
# ======================================

# Host 编译标志
HOST_CXXFLAGS = -I$(SGX_SDK)/include -I. -fPIC -O0 -g -std=c++14
HOST_CFLAGS = -I$(SGX_SDK)/include -I. -fPIC -O0 -g

# Enclave 编译标志
ENCLAVE_CXXFLAGS = -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc -I$(SGX_SDK)/include/libcxx -fpie -fstack-protector -O0 -g -std=c++14 -DINSIDE_ENCLAVE
ENCLAVE_CFLAGS = -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc -I$(SGX_SDK)/include/libcxx -nostdinc -fpie -fstack-protector -O0 -g
# ======================================
# 默认目标
# ======================================
all: enclave.signed.so test_sgx_basic

# ======================================
# EDL 边界生成
# ======================================
SGXEnclave_t.h SGXEnclave_t.c SGXEnclave_u.h SGXEnclave_u.c: SGXEnclave.edl
	@echo "Generating SGX edge routines..."
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_edger8r --trusted SGXEnclave.edl --search-path $(SGX_SDK)/include
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_edger8r --untrusted SGXEnclave.edl --search-path $(SGX_SDK)/include
	@echo "Edge code generated."
# ======================================
# Host 专属文件编译
# ======================================
$(HOST_SRC_CPP:.cpp=.host.o): %.host.o: %.cpp $(HEADERS) SGXEnclave_u.h
	@echo "Compiling HOST $< ..."
	@$(CXX) -c $< -o $@ $(HOST_CXXFLAGS)

$(HOST_SRC_C:.c=.host.o): %.host.o: %.c $(HEADERS) SGXEnclave_u.h
	@echo "Compiling HOST $< ..."
	@$(CC) -c $< -o $@ $(HOST_CFLAGS)

# ======================================
# Enclave 专属文件编译
# ======================================
$(ENCLAVE_SRC_CPP:.cpp=.enclave.o): %.enclave.o: %.cpp $(HEADERS) SGXEnclave_t.h
	@echo "Compiling ENCLAVE $< ..."
	@$(CXX) -c $< -o $@ $(ENCLAVE_CXXFLAGS)

$(ENCLAVE_SRC_C:.c=.enclave.o): %.enclave.o: %.c $(HEADERS) SGXEnclave_t.h
	@echo "Compiling ENCLAVE $< ..."
	@$(CC) -c $< -o $@ $(ENCLAVE_CFLAGS)

# ======================================
# 共享文件编译（两边都需要）
# ======================================
$(SHARED_SRC_CPP:.cpp=.enclave.o): %.enclave.o: %.cpp $(HEADERS) SGXEnclave_t.h
	@echo "Compiling SHARED(ENCLAVE) $< ..."
	@$(CXX) -c $< -o $@ $(ENCLAVE_CXXFLAGS)

$(SHARED_SRC_CPP:.cpp=.host.o): %.host.o: %.cpp $(HEADERS) SGXEnclave_u.h
	@echo "Compiling SHARED(HOST) $< ..."
	@$(CXX) -c $< -o $@ $(HOST_CXXFLAGS)

# ======================================
# 链接 Enclave .so
# ======================================
SGXEnclave.so: $(ENCLAVE_OBJS)
	@echo "Linking enclave..."
	@$(CXX) -o $@ $^ \
		-Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles \
		-Wl,--whole-archive -lsgx_trts -Wl,--no-whole-archive \
		-Wl,--start-group -lsgx_tstdc -lsgx_tcxx -lsgx_tcrypto -lsgx_tservice -Wl,--end-group \
		-L$(SGX_SDK)/lib64 \
		-Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
		-Wl,-pie,-eenclave_entry -Wl,--export-dynamic \
		-Wl,--defsym,__ImageBase=0
	@echo "Built enclave: SGXEnclave.so"

# ======================================
# 签名 Enclave
# ======================================
enclave.signed.so: SGXEnclave.so
	@echo "Signing enclave..."
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_sign sign \
		-key Enclave_private.pem \
		-enclave SGXEnclave.so \
		-out $@ \
		-config SGXEnclave.config.xml
	@echo "Built enclave: enclave.signed.so"

# ======================================
# Host 应用
# ======================================
test_sgx_basic: $(APP_OBJS)
	@echo "Linking host application..."
	@$(CXX) -o $@ $^ -L$(SGX_SDK)/lib64 -lsgx_urts -lsgx_uae_service -lpthread
	@echo "Built host: test_sgx_basic"

# ======================================
# 生成签名密钥
# ======================================
keys:
	@echo "Generating keys..."
	@openssl genrsa -out Enclave_private.pem -3 3072
	@openssl rsa -in Enclave_private.pem -pubout -out Enclave_public.pem
	@echo "Keys generated."

# ======================================
# 清理
# ======================================
clean:
	@echo "Cleaning..."
	@rm -f *.o *.so *.pem *.signed.so SGXEnclave_t.* SGXEnclave_u.* test_sgx_basic

# ======================================
# 测试
# ======================================
test: all
	@echo "=== Running SGX Test ==="
	@./test_sgx_basic