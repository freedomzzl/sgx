#ifndef NODE_H
#define NODE_H

#include "MBR.h"
#include "document.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

class Node {
public:
    enum Type { LEAF, INTERNAL };

private:
    int node_id;
    Type type;
    MBR mbr;
    int level;
    int document_count;
    
    std::vector<std::shared_ptr<Document>> documents;
    std::vector<std::shared_ptr<Node>> child_nodes;
    
    // 文本摘要信息
    std::unordered_map<std::string, int> df;      // 文档频率
    std::unordered_map<std::string, int> tf_max;  // 最大词频
    
    // 子节点优化信息
    std::unordered_map<int, int> child_position_map;           // 子节点位置映射
    std::unordered_map<int, MBR> child_mbr_map;                // 子节点MBR缓存
    std::unordered_map<int, double> child_text_upper_bounds;   // 子节点文本上界
    std::unordered_map<int, std::unordered_set<std::string>> child_keywords_map; // 子节点关键词

public:
    Node(int id, Type node_type, int node_level, const MBR& node_mbr);
    
    // Getters
    int getId() const { return node_id; }
    Type getType() const { return type; }
    const MBR& getMBR() const { return mbr; }
    int getLevel() const { return level; }
    int getDocumentCount() const { return document_count; }
    const std::vector<std::shared_ptr<Document>>& getDocuments() const { return documents; }
    const std::vector<std::shared_ptr<Node>>& getChildNodes() const { return child_nodes; }
    const std::unordered_map<std::string, int>& getDF() const { return df; }
    const std::unordered_map<std::string, int>& getTFMax() const { return tf_max; }
    
    // 子节点优化信息getters
    const std::unordered_map<int, int>& getChildPositionMap() const { return child_position_map; }
    const std::unordered_map<int, MBR>& getChildMBRMap() const { return child_mbr_map; }
    const std::unordered_map<int, double>& getChildTextUpperBounds() const { return child_text_upper_bounds; }
    const std::unordered_map<int, std::unordered_set<std::string>>& getChildKeywordsMap() const { return child_keywords_map; }
    
    // 基本操作
    void addChild(std::shared_ptr<Node> child);
    void addDocument(std::shared_ptr<Document> doc);
    void updateSummary();
    
    // 文本相关方法
    int getDocumentFrequency(const std::string& term) const;
    int getMaxTermFrequency(const std::string& term) const;
    
    // 子节点优化信息setters
    void setChildPosition(int child_id, int path) { child_position_map[child_id] = path; }
    void setChildMBR(int child_id, const MBR& mbr) { child_mbr_map[child_id] = mbr; }
    void setChildTextUpperBound(int child_id, double bound) { child_text_upper_bounds[child_id] = bound; }
    void setChildKeywords(int child_id, const std::unordered_set<std::string>& keywords) { child_keywords_map[child_id] = keywords; }
    void setChildPositionMap(const std::unordered_map<int, int>& map) { child_position_map = map; }
    
    // 文档摘要设置
    void setDocumentSummary(const std::unordered_map<std::string, int>& df_map, 
                          const std::unordered_map<std::string, int>& tf_max_map) {
        df = df_map;
        tf_max = tf_max_map;
    }
    
    // 移除toString方法
    // std::string toString() const;
    
    // 子节点关键词检查
    bool childHasAllKeywords(int child_id, const std::vector<std::string>& keywords) const {
        auto it = child_keywords_map.find(child_id);
        if (it == child_keywords_map.end()) return false;
        
        const auto& child_keywords = it->second;
        for (const auto& keyword : keywords) {
            if (child_keywords.find(keyword) == child_keywords.end()) {
                return false;
            }
        }
        return true;
    }
    
    bool hasChildMBR(int child_id) const {
        return child_mbr_map.find(child_id) != child_mbr_map.end();
    }
    
    MBR getChildMBR(int child_id) const {
        auto it = child_mbr_map.find(child_id);
        if (it != child_mbr_map.end()) {
            return it->second;
        }
        return MBR({0,0}, {0,0}); // 返回空MBR
    }
    
    double getChildTextUpperBound(int child_id) const {
        auto it = child_text_upper_bounds.find(child_id);
        if (it != child_text_upper_bounds.end()) {
            return it->second;
        }
        return 0.0;
    }
};

#endif