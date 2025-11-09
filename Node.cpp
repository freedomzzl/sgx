#include "Node.h"
#include "Document.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

Node::Node(int id, Type node_type, int node_level, const MBR& node_mbr)
    : node_id(id), type(node_type), mbr(node_mbr), level(node_level), document_count(0) {
}

void Node::addChild(std::shared_ptr<Node> child) {
    if (type != INTERNAL) {
        // 静默处理，不抛出异常
        return;
    }

    child_nodes.push_back(child);
    mbr.expand(child->getMBR());

    // 存储子节点的独立MBR
    setChildMBR(child->getId(), child->getMBR());

    // 提取子节点的关键词集合
    std::unordered_set<std::string> child_terms;
    for (const auto& pair : child->getTFMax()) {
        child_terms.insert(pair.first);
    }
    setChildKeywords(child->getId(), child_terms);
    updateSummary();
}

void Node::addDocument(std::shared_ptr<Document> doc) {
    if (type != LEAF) {
        // 静默处理，不抛出异常
        return;
    }

    documents.push_back(doc);
    mbr.expand(doc->getLocation());
    updateSummary();
}

void Node::updateSummary() {
    document_count = 0;  // 重置计数
    df.clear();
    tf_max.clear();

    if (type == LEAF) {
        // 叶子节点：统计文档中的词频
        for (const auto& doc : documents) {
            document_count++;  // 每个文档只计数一次
            const auto& term_freq_map = doc->getTermFreq();
            for (const auto& pair : term_freq_map) {
                const std::string& term = pair.first;
                int freq = pair.second;

                df[term]++; // 文档频率
                if (tf_max[term] < freq) {
                    tf_max[term] = freq; // 最大词频
                }
            }
        }
    }
    else {
        // 内部节点：聚合子节点的统计信息
        for (const auto& child : child_nodes) {
            document_count += child->getDocumentCount();

            // 合并文档频率
            const auto& child_df = child->getDF();
            for (const auto& pair : child_df) {
                df[pair.first] += pair.second;
            }

            // 取最大词频
            const auto& child_tf_max = child->getTFMax();
            for (const auto& pair : child_tf_max) {
                if (tf_max.find(pair.first) == tf_max.end() || tf_max[pair.first] < pair.second) {
                    tf_max[pair.first] = pair.second;
                }
            }
        }
    }
}

int Node::getDocumentFrequency(const std::string& term) const {
    auto it = df.find(term);
    return (it != df.end()) ? it->second : 0;
}

int Node::getMaxTermFrequency(const std::string& term) const {
    auto it = tf_max.find(term);
    return (it != tf_max.end()) ? it->second : 0;
}

size_t Node::getStringLength() const {
    // 估算字符串长度
    size_t length = 100; // 基础长度
    
    // MBR 字符串长度
    length += mbr.getStringLength();
    
    // DF 术语的长度
    int count = 0;
    for (const auto& pair : df) {
        if (count++ >= 5) break;
        length += pair.first.length() + 10; // 术语名 + 频率 + 分隔符
    }
    
    return length;
}

int Node::toString(char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入基础信息
    written += snprintf(buffer + written, buffer_size - written, 
                       "Node[id=%d, type=%s, level=%d, documents=%d, ",
                       node_id, 
                       (type == LEAF ? "LEAF" : "INTERNAL"),
                       level, 
                       document_count);
    if (written >= buffer_size) return written;
    
    // 写入 MBR 信息
    size_t mbr_buffer_size = buffer_size - written;
    if (mbr_buffer_size > 0) {
        int mbr_written = mbr.toString(buffer + written, mbr_buffer_size);
        if (mbr_written > 0) {
            written += mbr_written;
        }
    }
    
    if (written >= buffer_size) return written;
    
    // 写入结束括号
    written += snprintf(buffer + written, buffer_size - written, "]");
    if (written >= buffer_size) return written;
    
    // 显示前5个最常见的术语
    written += snprintf(buffer + written, buffer_size - written, " {df=");
    if (written >= buffer_size) return written;
    
    int count = 0;
    for (const auto& pair : df) {
        if (count++ >= 5) break;
        
        if (count > 1) {
            written += snprintf(buffer + written, buffer_size - written, ", ");
            if (written >= buffer_size) return written;
        }
        
        written += snprintf(buffer + written, buffer_size - written, 
                           "%s:%d", pair.first.c_str(), pair.second);
        if (written >= buffer_size) return written;
    }
    
    if (df.size() > 5) {
        written += snprintf(buffer + written, buffer_size - written, ", ...");
        if (written >= buffer_size) return written;
    }
    
    written += snprintf(buffer + written, buffer_size - written, "}");
    
    // 确保字符串以null结尾
    if (written < buffer_size) {
        buffer[written] = '\0';
    } else {
        buffer[buffer_size - 1] = '\0';
    }
    
    return written;
}