# =======================
# SGX Makefile (clean & organized)
# =======================

SGX_SDK ?= /opt/intel/sgxsdk
SGX_ARCH ?= x64
SGX_MODE ?= HW

CXX = g++
CC = gcc

.PHONY: all clean keys test

# =======================
# 源文件分类
# =======================

# Enclave 源文件（只在 Enclave 内编译）
ENCLAVE_SRC_CPP := SGXEnclave.cpp CryptoUtil.cpp NodeSerializer.cpp node.cpp MBR.cpp document.cpp
ENCLAVE_SRC_C   := SGXEnclave_t.c
ENCLAVE_OBJS    := $(ENCLAVE_SRC_CPP:.cpp=.o) $(ENCLAVE_SRC_C:.c=.o)

# Host 源文件（排除所有 Enclave 相关文件）
APP_SRC_CPP := $(filter-out $(ENCLAVE_SRC_CPP), $(wildcard *.cpp))
APP_SRC_C   := $(filter-out $(ENCLAVE_SRC_C), $(wildcard *.c))
APP_OBJS    := $(APP_SRC_CPP:.cpp=.o) $(APP_SRC_C:.c=.o)

HEADERS := $(wildcard *.h)

# =======================
# 默认目标
# =======================
all: enclave.signed.so test_sgx_basic

# =======================
# 生成 SGX 边界代码
# =======================
SGXEnclave_t.h SGXEnclave_u.h: SGXEnclave.edl
	@echo "Generating edge routines..."
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_edger8r --trusted $< --search-path $(SGX_SDK)/include
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_edger8r --untrusted $< --search-path $(SGX_SDK)/include
	@echo "Edge routines generated."

# =======================
# 编译规则（Host 侧）
# =======================
%.o: %.cpp $(HEADERS)
	@echo "Compiling $< ..."
	@$(CXX) -c $< -o $@ -I$(SGX_SDK)/include -I. -fPIC -O0 -g -std=c++11

%.o: %.c $(HEADERS)
	@echo "Compiling $< ..."
	@$(CC) -c $< -o $@ -I$(SGX_SDK)/include -I. -fPIC -O0 -g

# 生成 Host 程序
test_sgx_basic: $(APP_OBJS)
	@echo "Linking host application..."
	@$(CXX) -o $@ $^ -L$(SGX_SDK)/lib64 -lsgx_urts -lsgx_uae_service -lpthread
	@echo "Host built: test_sgx_basic"

# =======================
# Enclave 特殊编译规则
# =======================
SGXEnclave_t.o: SGXEnclave_t.c SGXEnclave_t.h
	@echo "Compiling trusted edge routines..."
	@$(CC) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-nostdinc -fpie -fstack-protector -O0 -g

SGXEnclave.o: SGXEnclave.cpp SGXEnclave_t.h CryptoUtil.h
	@echo "Compiling enclave (C++)..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

CryptoUtil.o: CryptoUtil.cpp CryptoUtil.h
	@echo "Compiling CryptoUtil (for enclave)..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

NodeSerializer.o: NodeSerializer.cpp NodeSerializer.h
	@echo "Compiling NodeSerializer..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

node.o: node.cpp node.h
	@echo "Compiling node..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

MBR.o: MBR.cpp MBR.h
	@echo "Compiling MBR..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

document.o: document.cpp document.h
	@echo "Compiling document..."
	@$(CXX) -c $< -o $@ \
		-I$(SGX_SDK)/include \
		-I$(SGX_SDK)/include/tlibc \
		-I$(SGX_SDK)/include/libcxx \
		-fpie -fstack-protector -O0 -g -std=c++11

# =======================
# 链接 Enclave .so
# =======================
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
	@echo "Enclave built: SGXEnclave.so"

# =======================
# 签名 Enclave
# =======================
enclave.signed.so: SGXEnclave.so
	@echo "Signing enclave..."
	@$(SGX_SDK)/bin/$(SGX_ARCH)/sgx_sign sign -key Enclave_private.pem -enclave SGXEnclave.so -out $@ -config SGXEnclave.config.xml
	@echo "Enclave signed: enclave.signed.so"

# =======================
# 生成签名密钥
# =======================
keys:
	@echo "Generating keys..."
	@openssl genrsa -out Enclave_private.pem -3 3072
	@openssl rsa -in Enclave_private.pem -pubout -out Enclave_public.pem
	@echo "Keys generated."

# =======================
# 清理
# =======================
clean:
	@echo "Cleaning..."
	@rm -f *.o *.so *.pem *.signed.so SGXEnclave_t.* SGXEnclave_u.* test_sgx_basic

# =======================
# 测试
# =======================
test: all
	@echo "=== Running Test ==="
	@./test_sgx_basic
