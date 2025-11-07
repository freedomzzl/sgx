#!/bin/bash
echo "=== Generating Enclave Signing Keys ==="

# 生成私钥
openssl genrsa -out Enclave_private.pem -3 3072

# 生成公钥
openssl rsa -in Enclave_private.pem -pubout -out Enclave_public.pem

echo "Enclave signing keys generated:"
echo "  - Enclave_private.pem (private key)"
echo "  - Enclave_public.pem (public key)"

# 设置权限
chmod 600 Enclave_private.pem
chmod 644 Enclave_public.pem