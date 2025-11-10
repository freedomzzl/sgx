#ifndef NODE_H
#define NODE_H

#include <vector>
#include <memory>
#include <unordered_map>
#include "MBR.h"
#include <unordered_set>

// 前向声明，避免循环依赖
class Document;

/**
 * @class Node
 * @brief IRTree（或R-Tree）中的节点类。
 *
 * 每个 Node 代表索引树中的一个节点。
 * - 叶子节点（LEAF）存储具体的 Document 对象；
 * - 内部节点（INTERNAL）存储指向子节点的指针；
 * 每个节点都包含一个最小外接矩形（MBR），用于描述节点所覆盖的空间范围。
 * 此外，节点还维护了文档摘要信息（如文档频率 DF、最大词频 TFmax），
 * 用于在查询阶段进行快速剪枝。
 */
class Node {
public:
    /// 节点类型：叶子节点或内部节点
    enum Type { LEAF, INTERNAL };

private:
    int node_id;       ///< 节点ID，用于唯一标识节点
    Type type;         ///< 节点类型（LEAF 或 INTERNAL）
    MBR mbr;           ///< 节点的最小外接矩形（Minimum Bounding Rectangle）
    int level;         ///< 节点所在的层级（根为最高层）

    // 子节点或文档集合（根据节点类型不同而使用）
    std::vector<std::shared_ptr<Node>> child_nodes;   ///< 内部节点的子节点列表
    std::vector<std::shared_ptr<Document>> documents; ///< 叶子节点存储的文档列表

    // 文档摘要信息（用于索引加速）
    int document_count; ///< 当前节点下的文档总数（包含子节点递归统计）
    std::unordered_map<std::string, int> df;      ///< 文档频率（Document Frequency）：包含某词项的文档数
    std::unordered_map<std::string, int> tf_max;  ///< 最大词频（Max Term Frequency）：某词项在该节点下的最大出现次数

    
    std::unordered_map<int, int> child_position_map;  // node_id -> path,存储子节点的 (node_id, path) 对
    std::unordered_map<int, MBR> child_mbrs;  // child_id -> MBR，存储每个子节点的独立MBR
    std::unordered_map<int, double> child_text_upper_bounds;  // child_id -> max_text_score,存储每个子节点的文本相关性上界
    std::unordered_map<int, std::unordered_set<std::string>> child_keywords;  // child_id -> keywords,存储每个子节点包含的关键词

public:
    /**
     * @brief 构造函数
     * @param id 节点ID
     * @param node_type 节点类型（LEAF 或 INTERNAL）
     * @param node_level 节点层级
     * @param node_mbr 节点的MBR范围
     */
    Node(int id, Type node_type, int node_level, const MBR& node_mbr);

    /* ======================== 节点结构操作 ======================== */

    /**
     * @brief 向内部节点添加一个子节点。
     * @param child 子节点指针
     */
    void addChild(std::shared_ptr<Node> child);

    /**
     * @brief 向叶子节点添加一个文档。
     * @param doc 文档指针
     */
    void addDocument(std::shared_ptr<Document> doc);

    /**
     * @brief 更新节点的摘要信息。
     *
     * 包括：
     * - 重新计算文档数量；
     * - 更新 DF（词项出现的文档数）；
     * - 更新 TFmax（节点下该词项的最大频率）。
     */
    void updateSummary();

    /* ======================== Getter 接口 ======================== */

    int getId() const { return node_id; }               ///< 获取节点ID
    Type getType() const { return type; }               ///< 获取节点类型
    const MBR& getMBR() const { return mbr; }           ///< 获取节点MBR
    int getLevel() const { return level; }              ///< 获取节点层级
    int getDocumentCount() const { return document_count; } ///< 获取文档数量

    const std::vector<std::shared_ptr<Node>>& getChildNodes() const { return child_nodes; } ///< 获取子节点列表
    const std::vector<std::shared_ptr<Document>>& getDocuments() const { return documents; } ///< 获取文档列表

    /* ======================== 文档摘要访问 ======================== */

    /**
     * @brief 获取某个词项在该节点下的文档频率。
     * @param term 词项字符串
     * @return 出现该词项的文档数量
     */
    int getDocumentFrequency(const std::string& term) const;

    /**
     * @brief 获取某个词项的最大词频（TFmax）。
     * @param term 词项字符串
     * @return 该词项在节点范围内的最大出现次数
     */
    int getMaxTermFrequency(const std::string& term) const;

    /**
     * @brief 获取 DF 哈希表的引用。
     */
    const std::unordered_map<std::string, int>& getDF() const { return df; }

    /**
     * @brief 获取 TFmax 哈希表的引用。
     */
    const std::unordered_map<std::string, int>& getTFMax() const { return tf_max; }

    /* ======================== 辅助接口 ======================== */

    /**
     * @brief 设置节点的摘要信息（用于反序列化或恢复索引）。
     * @param new_df 新的 DF 数据
     * @param new_tf_max 新的 TFmax 数据
     */
    void setDocumentSummary(const std::unordered_map<std::string, int>& new_df,
        const std::unordered_map<std::string, int>& new_tf_max) {
        df = new_df;
        tf_max = new_tf_max;
    }

    /**
     * @brief 清空节点的文档（常用于测试或节点重建）。
     */
    void clearDocuments() {
        documents.clear();
        updateSummary();
    }

    /**
     * @brief 设置内部节点的子节点列表（用于反序列化）。
     * @param children 子节点向量
     * @throws std::logic_error 若当前节点是叶子节点则抛出异常
     */
    void setChildNodes(const std::vector<std::shared_ptr<Node>>& children) {
        if (type != INTERNAL) {
            throw std::logic_error("Cannot set child nodes on leaf node");
        }
        child_nodes = children;
        updateSummary();
    }

    /**
     * @brief 清空节点的所有子节点。
     */
    void clearChildNodes() {
        child_nodes.clear();
        updateSummary();
    }

    /**
     * @brief 获取所有子节点的 ID 列表。
     * @return 子节点ID向量
     */
    std::vector<int> getChildNodeIds() const {
        std::vector<int> ids;
        for (const auto& child : child_nodes) {
            ids.push_back(child->getId());
        }
        return ids;
    }

    /* ======================== 位置映射操作 ======================== */

    /**
     * @brief 设置子节点的位置映射
     * @param child_id 子节点ID
     * @param path 子节点的物理路径
     */
    void setChildPosition(int child_id, int path) {
        child_position_map[child_id] = path;
    }

    /**
     * @brief 获取子节点的位置映射
     * @param child_id 子节点ID
     * @return 子节点的物理路径，如果不存在返回-1
     */
    int getChildPosition(int child_id) const {
        auto it = child_position_map.find(child_id);
        return (it != child_position_map.end()) ? it->second : -1;
    }

    /**
     * @brief 获取所有子节点的位置映射
     * @return 子节点位置映射的引用
     */
    const std::unordered_map<int, int>& getChildPositionMap() const {
        return child_position_map;
    }

    /**
     * @brief 清空子节点位置映射
     */
    void clearChildPositionMap() {
        child_position_map.clear();
    }

    /**
     * @brief 设置整个子节点位置映射（用于反序列化）
     * @param new_position_map 新的位置映射
     */
    void setChildPositionMap(const std::unordered_map<int, int>& new_position_map) {
        child_position_map = new_position_map;
    }

    /* ======================== 子节点MBR操作 ======================== */

   /**
    * @brief 设置子节点的MBR（用于加速空间查询）
    * @param child_id 子节点ID
    * @param mbr 子节点的MBR
    */
    void setChildMBR(int child_id, const MBR& mbr) {
        child_mbrs[child_id] = mbr;
    }

    /**
     * @brief 获取子节点的MBR（无需加载子节点）
     * @param child_id 子节点ID
     * @return 子节点的MBR，如果不存在返回空MBR
     */
    MBR getChildMBR(int child_id) const {
        auto it = child_mbrs.find(child_id);
        if (it != child_mbrs.end()) {
            return it->second;
        }
        // 返回空MBR表示未找到
        return MBR(std::vector<double>{0, 0}, std::vector<double>{0, 0});
    }

    /**
     * @brief 检查是否存储了子节点的MBR
     * @param child_id 子节点ID
     * @return 是否已存储
     */
    bool hasChildMBR(int child_id) const {
        return child_mbrs.find(child_id) != child_mbrs.end();
    }

    /**
     * @brief 获取所有子节点的MBR映射
     * @return 子节点MBR映射的引用
     */
    const std::unordered_map<int, MBR>& getChildMBRMap() const {
        return child_mbrs;
    }

    /* ======================== 子节点文本上界操作 ======================== */

    /**
     * @brief 设置子节点的文本相关性上界
     * @param child_id 子节点ID
     * @param upper_bound 文本相关性上界值
     */
    void setChildTextUpperBound(int child_id, double upper_bound) {
        child_text_upper_bounds[child_id] = upper_bound;
    }

    /**
     * @brief 获取子节点的文本相关性上界
     * @param child_id 子节点ID
     * @return 文本相关性上界，如果不存在返回0.0
     */
    double getChildTextUpperBound(int child_id) const {
        auto it = child_text_upper_bounds.find(child_id);
        return it != child_text_upper_bounds.end() ? it->second : 0.0;
    }

    /**
     * @brief 检查是否存储了子节点的文本上界
     * @param child_id 子节点ID
     * @return 是否已存储
     */
    bool hasChildTextUpperBound(int child_id) const {
        return child_text_upper_bounds.find(child_id) != child_text_upper_bounds.end();
    }

    /* ======================== 子节点关键词操作 ======================== */

    /**
     * @brief 设置子节点包含的关键词
     * @param child_id 子节点ID
     * @param keywords 关键词集合
     */
    void setChildKeywords(int child_id, const std::unordered_set<std::string>& keywords) {
        child_keywords[child_id] = keywords;
    }

    /**
     * @brief 检查子节点是否包含某个关键词
     * @param child_id 子节点ID
     * @param keyword 关键词
     * @return 是否包含
     */
    bool childHasKeyword(int child_id, const std::string& keyword) const {
        auto it = child_keywords.find(child_id);
        return it != child_keywords.end() && it->second.count(keyword) > 0;
    }

    /**
     * @brief 检查子节点是否包含所有查询关键词
     * @param child_id 子节点ID
     * @param keywords 查询关键词列表
     * @return 是否包含所有关键词
     */
    bool childHasAllKeywords(int child_id, const std::vector<std::string>& keywords) const {
        auto it = child_keywords.find(child_id);
        if (it == child_keywords.end()) return false;

        const auto& child_terms = it->second;
        for (const auto& keyword : keywords) {
            if (child_terms.count(keyword) == 0) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 获取子节点的关键词集合
     * @param child_id 子节点ID
     * @return 关键词集合的引用
     */
    const std::unordered_set<std::string>& getChildKeywords(int child_id) const {
        auto it = child_keywords.find(child_id);
        if (it != child_keywords.end()) {
            return it->second;
        }
        static std::unordered_set<std::string> empty_set;
        return empty_set;
    }

    /**
     * @brief 获取所有子节点的文本上界映射
     */
    const std::unordered_map<int, double>& getChildTextUpperBounds() const {
        return child_text_upper_bounds;
    }

    /**
     * @brief 获取所有子节点的关键词映射
     */
    const std::unordered_map<int, std::unordered_set<std::string>>& getChildKeywordsMap() const {
        return child_keywords;
    }
};

#endif // NODE_H