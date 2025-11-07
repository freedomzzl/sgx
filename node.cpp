#include "node.h"
#include "document.h"
#include <algorithm>

// SGX安全的错误处理
#ifdef SGX_ENCLAVE
#include <sgx_trts.h>
#define NODE_ASSERT(condition, message) \
    if (!(condition)) { \
        return; \
    }
#else
#define NODE_ASSERT(condition, message) \
    if (!(condition)) { \
        throw std::logic_error(message); \
    }
#endif

Node::Node(int id, Type node_type, int node_level, const MBR& node_mbr)
    : node_id(id), type(node_type), mbr(node_mbr), level(node_level), document_count(0) {
}

void Node::addChild(std::shared_ptr<Node> child) {
    NODE_ASSERT(type != LEAF, "Cannot add child to leaf node");

    child_nodes.push_back(child);
    mbr.expand(child->getMBR());
    setChildMBR(child->getId(), child->getMBR());

    std::unordered_set<std::string> child_terms;
    for (const auto& pair : child->getTFMax()) {
        child_terms.insert(pair.first);
    }
    setChildKeywords(child->getId(), child_terms);
    updateSummary();
}

void Node::addDocument(std::shared_ptr<Document> doc) {
    NODE_ASSERT(type != INTERNAL, "Cannot add document to internal node");

    documents.push_back(doc);
    mbr.expand(doc->getLocation());
    updateSummary();
}

void Node::updateSummary() {
    document_count = 0;
    df.clear();
    tf_max.clear();

    if (type == LEAF) {
        for (const auto& doc : documents) {
            document_count++;
            const auto& term_freq_map = doc->getTermFreq();
            for (const auto& pair : term_freq_map) {
                const std::string& term = pair.first;
                int freq = pair.second;

                df[term]++;
                if (tf_max[term] < freq) {
                    tf_max[term] = freq;
                }
            }
        }
    }
    else {
        for (const auto& child : child_nodes) {
            document_count += child->getDocumentCount();
            const auto& child_df = child->getDF();
            for (const auto& pair : child_df) {
                df[pair.first] += pair.second;
            }
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

// 完全移除toString方法