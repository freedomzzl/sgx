#ifndef NODE_SERIALIZER_H
#define NODE_SERIALIZER_H

#include <sgx_trts.h>
#include "node.h"
#include "document.h"
#include <vector>
#include <cstdint>
#include <memory>

class NodeSerializer {
public:
    // 序列化节点到字节流 - 返回SGX状态码
    static sgx_status_t serialize(const Node& node, std::vector<uint8_t>& data);

    // 从字节流反序列化节点 - 返回SGX状态码
    static sgx_status_t deserialize(const std::vector<uint8_t>& data, std::shared_ptr<Node>& node);

    // 序列化文档到字节流 - 返回SGX状态码
    static sgx_status_t serializeDocument(const Document& doc, std::vector<uint8_t>& data);

    // 从字节流反序列化文档 - 返回SGX状态码
    static sgx_status_t deserializeDocument(const std::vector<uint8_t>& data, std::shared_ptr<Document>& doc);

private:
    // 辅助方法 - 移除异常，返回bool表示成功
    static bool writeInt(std::vector<uint8_t>& data, int value);
    static bool readInt(const std::vector<uint8_t>& data, size_t& offset, int& value);
    static bool writeDouble(std::vector<uint8_t>& data, double value);
    static bool readDouble(const std::vector<uint8_t>& data, size_t& offset, double& value);
    static bool writeString(std::vector<uint8_t>& data, const std::string& str);
    static bool readString(const std::vector<uint8_t>& data, size_t& offset, std::string& str);
    // MBR序列化方法
    static bool writeMBR(std::vector<uint8_t>& data, const MBR& mbr);
    static bool readMBR(const std::vector<uint8_t>& data, size_t& offset, MBR& mbr);
};

#endif