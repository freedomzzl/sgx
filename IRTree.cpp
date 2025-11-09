#include"IRTree.h"
#include "NodeSerializer.h"  
#include <queue>
#include <stack>
#include <algorithm>
#include <cmath>
#include "ringoram.h"
#include "RingoramStorage.h"
#include "SGXEnclave_t.h"
#include <sgx_trts.h>
#include <sgx_thread.h>

#define PRINT(msg) ocall_print_string(msg)

// SGX安全的随机数生成
class SGXRandom {
public:
    static int getRandom(int min, int max) {
        uint32_t random_val;
        sgx_read_rand((unsigned char*)&random_val, sizeof(random_val));
        return min + (random_val % (max - min + 1));
    }
};

// SGX互斥锁包装
class SGXMutex {
private:
    sgx_thread_mutex_t mutex;
public:
    SGXMutex() {
        sgx_thread_mutex_init(&mutex, NULL);
    }
    ~SGXMutex() {
        sgx_thread_mutex_destroy(&mutex);
    }
    void lock() {
        sgx_thread_mutex_lock(&mutex);
    }
    void unlock() {
        sgx_thread_mutex_unlock(&mutex);
    }
};

// 文件操作类
class SGXFile {
public:
    static bool readFile(const std::string& filename, std::vector<uint8_t>& content) {
        size_t file_size;
        sgx_status_t retval;
        
        // 调用 OCALL 获取文件大小
        if (ocall_get_file_size(&retval, filename.c_str(), &file_size) != SGX_SUCCESS || retval != SGX_SUCCESS) {
            return false;
        }
        
        if (file_size == 0) {
            return false;
        }
        
        content.resize(file_size);
        size_t actual_size;
        
        // 调用 OCALL 读取文件内容
        if (ocall_read_file(&retval, filename.c_str(), content.data(), file_size, &actual_size) != SGX_SUCCESS || retval != SGX_SUCCESS) {
            return false;
        }
        
        return actual_size == file_size;
    }
    
    static std::vector<std::string> readLines(const std::string& filename) {
        std::vector<uint8_t> file_content;
        if (!readFile(filename, file_content)) {
            return {};
        }
        
        std::vector<std::string> lines;
        std::string current_line;
        
        for (size_t i = 0; i < file_content.size(); i++) {
            char c = static_cast<char>(file_content[i]);
            if (c == '\n') {
                if (!current_line.empty()) {
                    lines.push_back(current_line);
                    current_line.clear();
                }
            } else {
                current_line += c;
            }
        }
        
        if (!current_line.empty()) {
            lines.push_back(current_line);
        }
        
        return lines;
    }
};



// 全局互斥锁实例
static SGXMutex g_cache_mutex;


// 修改构造函数
IRTree::IRTree(std::shared_ptr<StorageInterface> storage_impl,
    int dims, int min_cap, int max_cap)
    : storage(storage_impl), dimensions(dims), min_capacity(min_cap),
    max_capacity(max_cap), next_node_id(0), next_doc_id(0) {

    // 创建根节点 - 初始化为全零MBR的叶子节点
    MBR root_mbr(std::vector<double>(dims, 0.0), std::vector<double>(dims, 0.0));
    root_node_id = createNewNode(Node::LEAF, 0, root_mbr);

    PRINT("IRTree initialized with storage interface");

    // 初始化递归位置映射
    initializeRecursivePositionMap();
}

// 节点管理方法
std::shared_ptr<Node> IRTree::loadNode(int node_id) const {
    // 从存储中读取节点数据
    auto node_data = storage->readNode(node_id);
    if (node_data.empty()) {
        PRINT("No data found for node");

        return nullptr;
    }

    // 反序列化节点数据为节点对象
    auto node = NodeSerializer::deserialize(node_data);
    if (!node) {
        PRINT("Failed to deserialize node");
    }

    return node;
}

void IRTree::saveNode(int node_id, std::shared_ptr<Node> node) {
    if (!node) {
        PRINT("Cannot save null node");
        return;
    }

    // 序列化节点对象为字节数据
    auto node_data = NodeSerializer::serialize(*node);
    if (node_data.empty()) {
        PRINT("Failed to serialize node");
        return;
    }

    // 存储序列化后的节点数据
    storage->storeNode(node_id, node_data);
}


int IRTree::createNewNode(Node::Type type, int level, const MBR& mbr) {
    // 分配新节点ID并创建节点对象
    int new_node_id = next_node_id++;

    // 验证参数合理性
    if (type == Node::LEAF && level != 0) {
        PRINT("WARNING: Creating leaf node with non-zero level");
    }
    if (type == Node::INTERNAL && level == 0) {
        PRINT("WARNING: Creating internal node with level 0");
    }

    auto new_node = std::make_shared<Node>(new_node_id, type, level, mbr);

    // 立即验证节点类型
    if (new_node->getType() != type) {
        PRINT("CRITICAL ERROR: Node type mismatch after creation!");
    }

    // 保存新节点到存储
    saveNode(new_node_id, new_node);

    return new_node_id;
}


double IRTree::computeNodeRelevance(std::shared_ptr<Node> node, const std::vector<std::string>& keywords, const MBR& spatial_scope, double alpha) const {
    if (!node) return 0.0;

    char msg[256];
    
    // 计算空间相关性
    double spatial_rel = computeSpatialRelevance(node->getMBR(), spatial_scope);
    snprintf(msg, sizeof(msg), "Spatial relevance for node %d: %.6f", node->getId(), spatial_rel);
    PRINT(msg);
    
    if (spatial_rel == 0.0) {
        PRINT("Spatial relevance is 0, returning 0");
        return 0.0;
    }

    // 计算文本相关性上界
    double text_upper_bound = 0.0;
    int total_docs = global_index.getTotalDocuments();
    int valid_keywords = 0;

    snprintf(msg, sizeof(msg), "Total documents in global index: %d", total_docs);
    PRINT(msg);

    for (const auto& keyword : keywords) {
        // 获取节点中该词的最大词频
        int tf_max = node->getMaxTermFrequency(keyword);
        snprintf(msg, sizeof(msg), "Keyword '%s': tf_max=%d", keyword.c_str(), tf_max);
        PRINT(msg);
        
        if (tf_max == 0) continue;

        // 获取全局文档频率
        int term_id = vocab.getTermId(keyword);
        snprintf(msg, sizeof(msg), "Term ID for '%s': %d", keyword.c_str(), term_id);
        PRINT(msg);
        
        if (term_id == -1) continue;

        int global_df = global_index.getDocumentFrequency(term_id);
        snprintf(msg, sizeof(msg), "Global DF for term %d: %d", term_id, global_df);
        PRINT(msg);
        
        if (global_df == 0) continue;

        // 计算该词在节点中可能达到的最大TF-IDF值
        double max_tfidf = Vector::computeTFIDFWeight(tf_max, global_df, total_docs);
        snprintf(msg, sizeof(msg), "Max TF-IDF for '%s': %.6f", keyword.c_str(), max_tfidf);
        PRINT(msg);
        
        text_upper_bound += max_tfidf;
        valid_keywords++;
    }

    snprintf(msg, sizeof(msg), "Text upper bound: %.6f, valid keywords: %d", text_upper_bound, valid_keywords);
    PRINT(msg);

    // 如果没有匹配的关键词，文本相关性为0
    if (valid_keywords == 0) {
        PRINT("No valid keywords, returning 0");
        return 0.0;
    }

    // 归一化到[0,1]范围
    text_upper_bound = std::min(1.0, text_upper_bound / keywords.size());
    snprintf(msg, sizeof(msg), "Normalized text upper bound: %.6f", text_upper_bound);
    PRINT(msg);
   
    //计算综合相关性
    double joint_relevance = computeJointRelevance(text_upper_bound, spatial_rel, alpha);
    snprintf(msg, sizeof(msg), "Joint relevance: %.6f (text=%.6f, spatial=%.6f, alpha=%.2f)", 
             joint_relevance, text_upper_bound, spatial_rel, alpha);
    PRINT(msg);

    return joint_relevance;
}

void IRTree::processLeafNode(std::shared_ptr<Node> leaf_node,
    const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    double alpha,
    std::vector<TreeHeapEntry>& results) const {



    if (!leaf_node || leaf_node->getType() != Node::LEAF) {

        return;
    }

    auto documents = leaf_node->getDocuments();


    // 遍历叶子节点中的所有文档
    for (const auto& doc : documents) {


        // 检查空间相关性 - 文档位置是否在查询范围内
        bool spatial_overlap = doc->getLocation().overlaps(spatial_scope);


        if (!spatial_overlap) {

            continue;
        }

        // 检查文本相关性 - 文档是否包含所有查询关键词
        bool has_all_keywords = true;
        for (const auto& keyword : keywords) {
            int tf = doc->getTermFrequency(keyword);

            if (tf == 0) {
                has_all_keywords = false;
                break;
            }
        }



        // 如果满足空间和文本条件，计算综合相关性并加入结果
        if (has_all_keywords) {
            double spatial_rel = computeSpatialRelevance(doc->getLocation(), spatial_scope);
            double text_rel = computeTextRelevance(*doc, keywords);
            double joint_rel = computeJointRelevance(text_rel, spatial_rel, alpha);



            results.push_back(TreeHeapEntry(doc, joint_rel));
        }

    }
}

void IRTree::processInternalNode(std::shared_ptr<Node> internal_node,
    const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    double alpha,
    std::priority_queue<TreeHeapEntry, std::vector<TreeHeapEntry>, TreeHeapComparator>& queue) const {

    if (internal_node == nullptr || internal_node->getType() != Node::INTERNAL) {
        return;
    }

    auto child_nodes = internal_node->getChildNodes();

    for (const auto& child_node_ptr : child_nodes) {
        int child_id = child_node_ptr->getId();

        // 1. 快速空间检查（使用缓存的MBR）
        MBR child_mbr = internal_node->getChildMBR(child_id);
        if (!internal_node->hasChildMBR(child_id)) {
            // 回退逻辑
            auto child_node = loadNode(child_id);
            if (child_node == nullptr) continue;
            child_mbr = child_node->getMBR();
        }

        bool overlaps = child_mbr.overlaps(spatial_scope);
        if (!overlaps) {
            continue;
        }

        // 2. 快速关键词存在性检查
        if (!internal_node->childHasAllKeywords(child_id, keywords)) {
            continue;  // 子节点不包含所有查询关键词，跳过
        }

        // 3. 快速上界检查
        double text_upper_bound = internal_node->getChildTextUpperBound(child_id);
        double spatial_upper_bound = computeSpatialRelevance(child_mbr, spatial_scope);
        double total_upper_bound = computeJointRelevance(text_upper_bound, spatial_upper_bound, alpha);

        // 如果上界太低，跳过加载子节点
        if (total_upper_bound < 0.1) {  // 可调整的阈值
            continue;
        }

        // 4. 只有通过所有检查的子节点才实际加载
        auto child_node = loadNode(child_id);
        if (child_node == nullptr) {
            continue;
        }

        // 计算精确的相关性
        double relevance = computeNodeRelevance(child_node, keywords, spatial_scope, alpha);
        if (relevance > 0) {
            queue.push(TreeHeapEntry(child_node, relevance));
        }
    }
}

// 使用路径处理内部节点
void IRTree::processInternalNodeWithPath(std::shared_ptr<Node> internal_node,
    int parent_path,
    const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    double alpha,
    std::priority_queue<TreeHeapEntry, std::vector<TreeHeapEntry>, TreeHeapComparator>& queue) {

    if (!internal_node || internal_node->getType() != Node::INTERNAL) {
        return;
    }

    // 从父节点中获取子节点的位置映射
    const auto& child_position_map = internal_node->getChildPositionMap();

    for (const auto& pos_pair : child_position_map) {
        int child_id = pos_pair.first;
        int child_path = pos_pair.second;

        // 1. 快速空间检查（使用缓存的MBR）
        MBR child_mbr = internal_node->getChildMBR(child_id);
        if (!internal_node->hasChildMBR(child_id)) {
            // 如果没有缓存，使用路径访问节点获取MBR
            auto child_node = accessNodeByPath(child_path);
            if (!child_node) continue;
            child_mbr = child_node->getMBR();
        }

        // 检查空间重叠
        bool overlaps = child_mbr.overlaps(spatial_scope);
        if (!overlaps) {
            continue;
        }

        // 2. 快速关键词存在性检查
        if (!internal_node->childHasAllKeywords(child_id, keywords)) {
            continue;  // 子节点不包含所有查询关键词，跳过
        }

        // 3. 快速上界检查
        double text_upper_bound = internal_node->getChildTextUpperBound(child_id);
        double spatial_upper_bound = computeSpatialRelevance(child_mbr, spatial_scope);
        double total_upper_bound = computeJointRelevance(text_upper_bound, spatial_upper_bound, alpha);

        // 如果上界太低，跳过加载子节点
        if (total_upper_bound < 0.1) {  // 可调整的阈值
            continue;
        }

        // 4. 只有通过所有检查的节点才实际加载
        auto child_node = accessNodeByPath(child_path);
        if (!child_node) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to load child node %zu using path %s", child_id, child_path);
            PRINT(msg);
            continue;
        }

        // 计算精确的相关性并加入优先队列（包含路径信息）
        double relevance = computeNodeRelevance(child_node, keywords, spatial_scope, alpha);
        if (relevance > 0) {
            queue.push(TreeHeapEntry(child_node, child_path, relevance));
        }
    }
}

double IRTree::computeTextRelevance(const Document& doc, const std::vector<std::string>& query_terms) const
{
    double relevance = 0.0;
    int total_docs = global_index.getTotalDocuments();

    // 对每个查询词计算TF-IDF权重
    for (const auto& term : query_terms) {
        int term_id = vocab.getTermId(term);
        if (term_id == -1) continue;  // 词汇表中不存在的词跳过

        int tf = doc.getTermFrequency(term);  // 词频
        if (tf == 0) continue;

        int df = global_index.getDocumentFrequency(term_id);  // 文档频率
        if (df == 0) continue;

        // 使用 TF-IDF 计算权重
        double tf_idf = Vector::computeTFIDFWeight(tf, df, total_docs);
        relevance += tf_idf;
    }

    // 归一化到 [0, 1] 范围
    if (relevance > 0) {
        relevance = std::min(1.0, relevance / query_terms.size());
    }

    return relevance;
}

double IRTree::computeSpatialRelevance(const MBR& doc_location, const MBR& query_scope) const {
    // 首先检查是否重叠
    if (!doc_location.overlaps(query_scope)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "MBR does not overlap: [%.6f,%.6f]-[%.6f,%.6f] vs [%.6f,%.6f]-[%.6f,%.6f]",
                 doc_location.getMin()[0], doc_location.getMin()[1],
                 doc_location.getMax()[0], doc_location.getMax()[1],
                 query_scope.getMin()[0], query_scope.getMin()[1],
                 query_scope.getMax()[0], query_scope.getMax()[1]);
        PRINT(msg);
        return 0.0;
    }

    // 计算重叠区域的面积比例（多维度的乘积）
    double overlap_area = 1.0;
    for (size_t i = 0; i < doc_location.getMin().size(); i++) {
        double overlap_min = std::max(doc_location.getMin()[i], query_scope.getMin()[i]);
        double overlap_max = std::min(doc_location.getMax()[i], query_scope.getMax()[i]);

        if (overlap_min >= overlap_max) {
            return 0.0; // 没有重叠
        }

        overlap_area *= (overlap_max - overlap_min);  // 计算每个维度的重叠长度
    }

    double doc_area = doc_location.area();
    if (doc_area == 0) return 1.0;  // 文档面积为0时返回最大值

    // 返回重叠面积比例
    return overlap_area / doc_area;
}

double IRTree::computeJointRelevance(double text_relevance, double spatial_relevance, double alpha) const
{
    // 线性加权组合文本相关性和空间相关性
    return alpha * text_relevance + (1 - alpha) * spatial_relevance;
}

int IRTree::chooseLeaf(const MBR& mbr) {
    int current_id = root_node_id;
    auto current = loadNode(current_id);

    if (!current) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Failed to load root node %zu", current_id);
        PRINT(msg);
        return -1;
    }

    // 从根节点开始向下遍历，直到找到叶子节点
    while (current->getType() != Node::LEAF) {
        auto children = current->getChildNodes();
        if (children.empty()) break;

        // 选择需要最小扩展的子树（R树插入策略）
        int best_child_id = -1;
        double min_expansion = std::numeric_limits<double>::max();

        for (const auto& child : children) {
            MBR expanded = child->getMBR();
            expanded.expand(mbr);  // 扩展MBR以包含新文档
            double expansion = expanded.area() - child->getMBR().area();  // 计算扩展面积

            // 选择扩展面积最小的子节点，面积相同时选择面积较小的子节点
            if (best_child_id == -1 || expansion < min_expansion ||
                (expansion == min_expansion && child->getMBR().area() <
                    loadNode(best_child_id)->getMBR().area())) {
                min_expansion = expansion;
                best_child_id = child->getId();
            }
        }

        if (best_child_id == -1) break;

        // 移动到选择的子节点
        current_id = best_child_id;
        current = loadNode(current_id);
        if (!current) break;
    }

    return current_id;  // 返回选择的叶子节点ID
}

void IRTree::adjustTree(int node_id)
{
    // 首先加载节点
    auto node = loadNode(node_id);
    if (!node) {
        PRINT("ajust_tree:Failed to load node");
        return;
    }

    // 更新节点摘要信息（MBR和文本摘要）
    node->updateSummary();

    // 保存更新后的节点
    saveNode(node_id, node);

    // 如果节点溢出，需要分裂
    if ((node->getType() == Node::LEAF && node->getDocuments().size() > max_capacity) ||
        (node->getType() == Node::INTERNAL && node->getChildNodes().size() > max_capacity)) {
        splitNode(node_id);
    }
}



void IRTree::splitNode(int node_id) {

    auto node = loadNode(node_id);
    if (node == nullptr) {
        PRINT("splitNode:Failed to load node");
        return;
    }

    if (node->getType() == Node::LEAF) {
        auto documents = node->getDocuments();


        if (documents.size() <= max_capacity) {
            PRINT("No need to split - within capacity");
            return;
        }


        // 按X坐标排序文档，然后分成两部分（线性分裂策略）

        std::sort(documents.begin(), documents.end(),
            [](const std::shared_ptr<Document>& a, const std::shared_ptr<Document>& b) {
                double center_a = a->getLocation().getCenter()[0];
                double center_b = b->getLocation().getCenter()[0];

                return center_a < center_b;
            });



        int split_index = documents.size() / 2;  // 中间位置分裂


        // 创建两个新节点 - 修复1：计算正确的MBR
        MBR new_mbr1 = documents[0]->getLocation();
        MBR new_mbr2 = documents[split_index]->getLocation();

        // 扩展MBR以包含所有分配的文档
        for (int i = 1; i < split_index; i++) {
            new_mbr1.expand(documents[i]->getLocation());
        }
        for (int i = split_index + 1; i < documents.size(); i++) {
            new_mbr2.expand(documents[i]->getLocation());
        }

        int new_node_id1 = createNewNode(Node::LEAF, node->getLevel(), new_mbr1);
        int new_node_id2 = createNewNode(Node::LEAF, node->getLevel(), new_mbr2);

        auto new_node1 = loadNode(new_node_id1);
        auto new_node2 = loadNode(new_node_id2);

        if (new_node1 == nullptr || new_node2 == nullptr) {
            PRINT("splitNode:Failed to create new nodes for splitting");
            return;
        }


        // 分配文档到两个新节点
        for (int i = 0; i < split_index; i++) {
            new_node1->addDocument(documents[i]);

        }
        for (int i = split_index; i < documents.size(); i++) {
            new_node2->addDocument(documents[i]);

        }


        new_node1->updateSummary();
        new_node2->updateSummary();



        // 保存新节点
        saveNode(new_node_id1, new_node1);
        saveNode(new_node_id2, new_node2);

        // 如果是根节点，创建新的根节点
        if (node_id == root_node_id) {

            MBR root_mbr = new_mbr1;
            root_mbr.expand(new_mbr2);

            int new_root_id = createNewNode(Node::INTERNAL, node->getLevel() + 1, root_mbr);
            auto new_root = loadNode(new_root_id);

            if (new_root != nullptr) {
                // 重新加载实际的子节点对象
                auto child1 = loadNode(new_node_id1);
                auto child2 = loadNode(new_node_id2);

                if (child1 != nullptr && child2 != nullptr) {
                    new_root->addChild(child1);
                    new_root->addChild(child2);
                    saveNode(new_root_id, new_root);
                    root_node_id = new_root_id;  // 更新根节点ID


                    // 删除旧的根节点
                    storage->deleteNode(node_id);

                }
                else {
                    PRINT("Failed to load child nodes for new root");
                }
            }
        }

    }
    else {
        // 内部节点的分裂 - 类似修复
        auto children = node->getChildNodes();


        if (children.size() <= max_capacity) {

            return;
        }



        // 按X坐标排序子节点
        std::sort(children.begin(), children.end(),
            [](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                return a->getMBR().getCenter()[0] < b->getMBR().getCenter()[0];
            });

        int split_index = children.size() / 2;

        // 创建新节点 - 修复MBR计算
        MBR new_mbr1 = children[0]->getMBR();
        MBR new_mbr2 = children[split_index]->getMBR();

        for (int i = 1; i < split_index; i++) {
            new_mbr1.expand(children[i]->getMBR());
        }
        for (int i = split_index + 1; i < children.size(); i++) {
            new_mbr2.expand(children[i]->getMBR());
        }

        int new_node_id1 = createNewNode(Node::INTERNAL, node->getLevel(), new_mbr1);
        int new_node_id2 = createNewNode(Node::INTERNAL, node->getLevel(), new_mbr2);

        auto new_node1 = loadNode(new_node_id1);
        auto new_node2 = loadNode(new_node_id2);

        if (new_node1 == nullptr || new_node2 == nullptr) {
            PRINT("Failed to create new internal nodes for splitting");
            return;
        }

        // 分配子节点
        for (int i = 0; i < split_index; i++) {
            new_node1->addChild(children[i]);
        }
        for (int i = split_index; i < children.size(); i++) {
            new_node2->addChild(children[i]);
        }

        // 保存新节点
        saveNode(new_node_id1, new_node1);
        saveNode(new_node_id2, new_node2);

        // 如果是根节点，创建新的根节点
        if (node_id == root_node_id) {
            MBR root_mbr = new_mbr1;
            root_mbr.expand(new_mbr2);

            int new_root_id = createNewNode(Node::INTERNAL, node->getLevel() + 1, root_mbr);
            auto new_root = loadNode(new_root_id);

            if (new_root != nullptr) {
                auto child1 = loadNode(new_node_id1);
                auto child2 = loadNode(new_node_id2);

                if (child1 != nullptr && child2 != nullptr) {
                    new_root->addChild(child1);
                    new_root->addChild(child2);
                    saveNode(new_root_id, new_root);
                    root_node_id = new_root_id;

                    // 删除旧的根节点
                    storage->deleteNode(node_id);
                }
                else {
                    PRINT("Failed to load child nodes for new root");
                }
            }
        }
    }

}
// 初始化递归位置映射
void IRTree::initializeRecursivePositionMap() {
    // 从根节点开始递归分配路径
    int root_path = assignPathRecursively(root_node_id);

    if (root_path != -1) {
        // 存储根节点路径到 STASH（移除调试打印）
        setRootPath(root_path);
    }
    else {
        PRINT("Failed to assign path to root node");
    }

}

// 递归分配路径
int IRTree::assignPathRecursively(int node_id) {
    auto node = loadNode(node_id);
    if (!node) {
        PRINT("Failed to load node for path assignment");
        return -1;
    }

    // 为当前节点分配随机路径
    int current_path = getRandomLeafPath();

    // 为路径分配块索引
    auto path_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (path_oram_storage) {
        int block_index = path_oram_storage->allocateBlockForPath(current_path);
        path_oram_storage->mapPathToNode(current_path, node_id);
    }

    if (node->getType() == Node::INTERNAL) {
        // 递归为子节点分配路径
        auto child_nodes = node->getChildNodes();
        for (const auto& child : child_nodes) {
            int child_id = child->getId();
            int child_path = assignPathRecursively(child_id);

            if (child_path != -1) {
                // 将子节点路径记录到父节点中
                node->setChildPosition(child_id, child_path);
            }
        }
    }

    // 保存更新后的节点（包含子节点位置映射）
    saveNode(node_id, node);

    return current_path;
}

// 获取随机叶子路径
int IRTree::getRandomLeafPath() const {
    return SGXRandom::getRandom(0, numLeaves - 1);
}

// 根节点路径管理（简化实现，后面需要集成到RingOramStorage）
int IRTree::getRootPath() const {
    if (!storage) {
        PRINT("Storage not available for root path access");
        return -1;
    }

    // 动态转换到RingOramStorage来获取根路径
    auto path_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (path_oram_storage) {
        return path_oram_storage->getRootPath();
    }
    else {
        PRINT("Storage is not RingOramStorage, cannot get root path");
        return -1;
    }
}

void IRTree::setRootPath(int path) {
    if (!storage) {
        PRINT("Storage not available for root path setting");
        return;
    }

    // 动态转换到RingOramStorage来设置根路径
    auto path_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (path_oram_storage) {
        path_oram_storage->setRootPath(path);
        // 移除调试打印，让 RingOramStorage 处理日志
    }
    else {
        PRINT("Storage is not RingOramStorage, cannot set root path");
    }
}

// 递归访问节点（使用路径而不是节点ID）
std::shared_ptr<Node> IRTree::accessNodeByPath(int path) {
    if (!storage) {
        PRINT("Storage not available for path access");
        return nullptr;
    }

    // 动态转换到RingOramStorage来使用路径访问
    auto path_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (path_oram_storage) {
        auto node_data = path_oram_storage->accessByPath(path);
        if (node_data.empty()) {
            return nullptr;
        }

        // 反序列化节点数据
        auto node = NodeSerializer::deserialize(node_data);
        if (!node) {
            return nullptr;
        }

        return node;
    }
    else {
        return nullptr;
    }
}

void IRTree::insertDocument(const std::string& text, const MBR& location)
{
    // 创建文档对象并分配ID
    auto document = std::make_shared<Document>(next_doc_id++, location, text);
    insertDocument(document);
}

//修改 insertDocument 方法
void IRTree::insertDocument(std::shared_ptr<Document> document) {
    // 添加到全局索引 - 构建文本索引
    Vector doc_vector(document->getId());
    Vector::vectorize(doc_vector, document->getText(), vocab);
    global_index.addDocument(document->getId(), doc_vector);

    // 选择插入的叶子节点
    int leaf_id = chooseLeaf(document->getLocation());
    if (leaf_id == -1) {
        PRINT("Failed to choose leaf for document insertion");
        return;
    }

    auto leaf_node = loadNode(leaf_id);
    if (!leaf_node) {
        PRINT("Failed to load leaf node");
        return;
    }

    // 插入文档到叶子节点
    leaf_node->addDocument(document);
    saveNode(leaf_id, leaf_node);

    // 调整树结构（更新MBR，可能分裂）
    adjustTree(leaf_id);

    // 检查根节点是否需要分裂
    auto root_node = loadNode(root_node_id);
    if (!root_node) return;

    if ((root_node->getType() == Node::LEAF && root_node->getDocuments().size() > max_capacity) ||
        (root_node->getType() == Node::INTERNAL && root_node->getChildNodes().size() > max_capacity)) {
        splitNode(root_node_id);
    }
}



std::vector<TreeHeapEntry> IRTree::search(const Query& query)
{
    // 委托给参数化搜索方法
    return search(query.getKeywords(), query.getSpatialScope(), query.getK(), query.getAlpha());
}

// 使用递归位置映射的搜索实现
std::vector<TreeHeapEntry> IRTree::search(const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    int k,
    double alpha) {

    search_blocks = 0;
    std::vector<TreeHeapEntry> results;

    if (!storage || keywords.empty() || k <= 0) {
        return results;
    }


    // 获取根节点路径
    int root_path = getRootPath();
    char msg[256];
    snprintf(msg, sizeof(msg), "Root path: %d", root_path);
    PRINT(msg);
    if (root_path == -1) {
        PRINT("Failed to get root path for search");
        return results;
    }

    // 加载根节点（使用路径访问）
    auto root_node = accessNodeByPath(root_path);
    if (!root_node) {
        PRINT("Failed to load root node using path");
        return results;
    }
    
    snprintf(msg, sizeof(msg), "Root node loaded: id=%d, type=%d, level=%d", 
             root_node->getId(), root_node->getType(), root_node->getLevel());
    PRINT(msg);

    snprintf(msg, sizeof(msg), "Search scope: [%.6f,%.6f] to [%.6f,%.6f]", 
         spatial_scope.getMin()[0], spatial_scope.getMin()[1],
         spatial_scope.getMax()[0], spatial_scope.getMax()[1]);
    PRINT(msg);

    // 检查根节点的MBR是否与搜索范围重叠
    MBR root_mbr = root_node->getMBR();
    snprintf(msg, sizeof(msg), "Root MBR: [%.6f,%.6f] to [%.6f,%.6f]", 
         root_mbr.getMin()[0], root_mbr.getMin()[1],
         root_mbr.getMax()[0], root_mbr.getMax()[1]);
    PRINT(msg);

bool overlaps = root_mbr.overlaps(spatial_scope);
snprintf(msg, sizeof(msg), "Root overlaps search scope: %s", overlaps ? "YES" : "NO");
PRINT(msg);

    // 使用优先队列进行最佳优先搜索（现在包含路径信息）
    std::priority_queue<TreeHeapEntry, std::vector<TreeHeapEntry>, TreeHeapComparator> queue;

    // 计算根节点的相关性并加入队列（包含路径信息）
    double root_relevance = computeNodeRelevance(root_node, keywords, spatial_scope, alpha);

    snprintf(msg, sizeof(msg), "Root node relevance: %.6f", root_relevance);
    PRINT(msg);
    if (root_relevance > 0) {
        queue.push(TreeHeapEntry(root_node, root_path, root_relevance));
    }

    int nodes_visited = 0;
    int documents_checked = 0;

    // 最佳优先搜索主循环
    while (!queue.empty() && results.size() < k) {
        TreeHeapEntry current = queue.top();
        queue.pop();
        nodes_visited++;

        snprintf(msg, sizeof(msg), "Processing entry %d: type=%s, score=%.6f", 
                nodes_visited, current.isNode() ? "NODE" : "DOC", current.score);
        PRINT(msg);

        if (current.isData()) {
            // 找到文档，加入结果
            results.push_back(current);

            snprintf(msg, sizeof(msg), "Found document: id=%d", current.document->getId());
            PRINT(msg);
        }
        else if (current.isNode()) {
            auto node = current.node;

            snprintf(msg, sizeof(msg), "Processing node: id=%d, type=%d, level=%d", 
                    node->getId(), node->getType(), node->getLevel());
            PRINT(msg);

            if (node->getType() == Node::LEAF) {
                // 处理叶子节点 - 检查实际文档
                int prev_results = results.size();
                processLeafNode(node, keywords, spatial_scope, alpha, results);
                documents_checked += (results.size() - prev_results);

                snprintf(msg, sizeof(msg), "Leaf node processed: %zu -> %zu documents", 
                        prev_results, results.size());
                PRINT(msg);
            }
            else {
                // 处理内部节点 - 使用递归位置映射获取子节点
                PRINT("Processing internal node with path...");
                processInternalNodeWithPath(node, current.path, keywords, spatial_scope, alpha, queue);
                snprintf(msg, sizeof(msg), "Queue size after processing: %zu", queue.size());
                PRINT(msg);
            }
        }
    }

    search_blocks = nodes_visited * (OramL -cacheLevel);
    // 排序结果
    std::sort(results.begin(), results.end(),
        [](const TreeHeapEntry& a, const TreeHeapEntry& b) {
            return a.score > b.score;
        });

    // 只返回前k个结果
    if (results.size() > k) {
        results.resize(k);
    }

    PRINT("=== SEARCH COMPLETED ===");

    snprintf(msg, sizeof(msg), "  Nodes visited: %zu", nodes_visited);
    PRINT(msg);

    snprintf(msg, sizeof(msg), "  Blocks accessed: %zu", search_blocks);
    PRINT(msg);

    snprintf(msg, sizeof(msg), "  Documents checked: %zu", documents_checked);
    PRINT(msg);

    snprintf(msg, sizeof(msg), "  Final results: %zu", results.size());
    PRINT(msg);

    return results;
}

void IRTree::bulkInsertFromFile(const std::string& filename) {
    PRINT("Reading file for bulk insertion...");
    
    auto lines = SGXFile::readLines(filename);
    if (lines.empty()) {
        PRINT("Failed to read file or file is empty");
        return;
    }
    
    std::vector<std::tuple<std::string, double, double>> documents;
    
    for (const auto& line : lines) {
        if (line.empty()) continue;
        
        // 解析格式: 文本|经度|纬度
        size_t pos1 = line.find('|');
        if (pos1 == std::string::npos) continue;
        
        size_t pos2 = line.find('|', pos1 + 1);
        if (pos2 == std::string::npos) continue;
        
        std::string text = line.substr(0, pos1);
        std::string lon_str = line.substr(pos1 + 1, pos2 - pos1 - 1);
        std::string lat_str = line.substr(pos2 + 1);
        
        // 简单的字符串转double
        double lon = 0.0, lat = 0.0;
        if (parseDouble(lon_str, lon) && parseDouble(lat_str, lat)) {
            documents.emplace_back(text, lon, lat);
        }
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Loaded %zu documents from file", documents.size());
    PRINT(msg);
    
    bulkInsertDocuments(documents);
}


void IRTree::bulkInsertDocuments(const std::vector<std::tuple<std::string, double, double>>& documents) {
    
    int inserted_count = 0;

    // 逐个插入文档（顺序插入，性能较低）
    for (const auto& doc_data : documents) {
        const auto& text = std::get<0>(doc_data);
        double lon = std::get<1>(doc_data);
        double lat = std::get<2>(doc_data);

        // 创建MBR（使用点位置，设置小的边界框）
        double epsilon = 0.001; // 小偏移量，使MBR不为点
        MBR location({ lon - epsilon, lat - epsilon }, { lon + epsilon, lat + epsilon });

        // 插入文档
        insertDocument(text, location);
        inserted_count++;

    }

}

// 优化的批量插入方法
void IRTree::optimizedBulkInsertFromFile(const std::string& filename) {
    
    auto lines = SGXFile::readLines(filename);
    if (lines.empty()) {
        PRINT("Failed to read file or file is empty");
        return;
    }
    
    std::vector<std::tuple<std::string, double, double>> documents;
    documents.reserve(lines.size());

    
    for (const auto& line : lines) {
        if (line.empty()) continue;
        
        // 快速解析
        const char* str = line.c_str();
        const char* pipe1 = strchr(str, '|');
        if (!pipe1) continue;
        
        const char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        
        std::string text(str, pipe1 - str);
        std::string lon_str(pipe1 + 1, pipe2 - pipe1 - 1);
        std::string lat_str(pipe2 + 1);
        
        double lon = 0.0, lat = 0.0;
        if (parseDouble(lon_str, lon) && parseDouble(lat_str, lat)) {
            documents.emplace_back(text, lon, lat);
        }
    }
  
    char msg[256];
    snprintf(msg, sizeof(msg), "Optimized file loading: %zu records", 
             documents.size());
    PRINT(msg);
    
    optimizedBulkInsertDocuments(documents);
}


void IRTree::optimizedBulkInsertDocuments(const std::vector<std::tuple<std::string, double, double>>& documents) {
    if (documents.empty()) return;

    // 阶段1：批量创建文档对象（并行化）
    std::vector<std::shared_ptr<Document>> doc_objects;
    doc_objects.reserve(documents.size());

    // 使用更高效的方式创建文档 - OpenMP并行化
#pragma omp parallel for
    for (int i = 0; i < documents.size(); i++) {
        const auto& doc_data = documents[i];
        const auto& text = std::get<0>(doc_data);
        double lon = std::get<1>(doc_data);
        double lat = std::get<2>(doc_data);

        double epsilon = 0.001;
        MBR location({ lon - epsilon, lat - epsilon }, { lon + epsilon, lat + epsilon });

#pragma omp critical
        {
            // 临界区保护共享资源
            auto document = std::make_shared<Document>(next_doc_id++, location, text);
            doc_objects.push_back(document);
        }
    }

    // 阶段2：批量构建全局索引（优化版本）
    bulkBuildGlobalIndex(doc_objects);

    // 阶段3：使用自底向上的方式构建树（关键优化）
    buildTreeBottomUp(doc_objects);

    char msg[128];
    snprintf(msg, sizeof(msg), "Bulk insertion completed: %zu documents.", documents.size());
    PRINT(msg);
}

void IRTree::bulkBuildGlobalIndex(const std::vector<std::shared_ptr<Document>>& documents) {
    PRINT("=== BUILDING GLOBAL INDEX ===");
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Processing %zu documents", documents.size());
    PRINT(msg);

    // 批量词汇表构建 - 先收集所有词汇
    std::unordered_map<std::string, int> term_frequencies;

    // 先收集所有词汇及其频率
    for (const auto& doc : documents) {
        const std::string& text = doc->getText();
        const char* str = text.c_str();
        const char* start = str;
        
        while (*str) {
            if (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
                if (str > start) {
                    std::string word(start, str - start);
                    term_frequencies[word]++;
                }
                start = str + 1;
            }
            str++;
        }
        
        // 处理最后一个单词
        if (str > start) {
            std::string word(start, str - start);
            term_frequencies[word]++;
        }
    }

    snprintf(msg, sizeof(msg), "Collected %zu unique terms", term_frequencies.size());
    PRINT(msg);

    // 显示一些词汇
    PRINT("Sample terms in vocabulary:");
    int count = 0;
    for (const auto& term : term_frequencies) {
        if (count++ < 10) {
            snprintf(msg, sizeof(msg), "  '%s': freq=%d", term.first.c_str(), term.second);
            PRINT(msg);
        }
    }

    // 批量添加到词汇表
    for (const auto& term : term_frequencies) {
        int term_id = vocab.getTermId(term.first);
        snprintf(msg, sizeof(msg), "Term '%s' -> ID %d", term.first.c_str(), term_id);
        PRINT(msg);
    }

    // 批量构建文档向量
    for (const auto& doc : documents) {
        Vector doc_vector(doc->getId());
        
        // 手动分词构建向量
        const std::string& text = doc->getText();
        const char* str = text.c_str();
        const char* start = str;
        
        while (*str) {
            if (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
                if (str > start) {
                    std::string word(start, str - start);
                    int term_id = vocab.getTermId(word);
                    if (term_id != -1) {
                        doc_vector.addTerm(term_id, 1.0);
                    }
                }
                start = str + 1;
            }
            str++;
        }
        
        // 处理最后一个单词
        if (str > start) {
            std::string word(start, str - start);
            int term_id = vocab.getTermId(word);
            if (term_id != -1) {
                doc_vector.addTerm(term_id, 1.0);
            }
        }
        
        global_index.addDocument(doc->getId(), doc_vector);
    }

    snprintf(msg, sizeof(msg), "Global index built for %zu documents", documents.size());
    PRINT(msg);
}

void IRTree::bulkInsertToTree(const std::vector<std::shared_ptr<Document>>& documents) {
 
    // 按空间位置排序，提高局部性
    std::vector<std::shared_ptr<Document>> sorted_docs = documents;
    std::sort(sorted_docs.begin(), sorted_docs.end(),
        [](const std::shared_ptr<Document>& a, const std::shared_ptr<Document>& b) {
            return a->getLocation().getCenter()[0] < b->getLocation().getCenter()[0];
        });

    // 分组插入到叶子节点
    std::unordered_map<int, std::shared_ptr<Node>> leaf_nodes;
    std::unordered_map<int, std::vector<std::shared_ptr<Document>>> leaf_docs;

    // 第一步：收集每个叶子节点要插入的文档
    int choose_leaf_count = 0;
    for (const auto& doc : sorted_docs) {
        int leaf_id = chooseLeaf(doc->getLocation());
        if (leaf_id != -1) {
            leaf_docs[leaf_id].push_back(doc);
            choose_leaf_count++;

            if (choose_leaf_count % 1000 == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Processed %zu documents for leaf assignment...", choose_leaf_count);
                PRINT(msg);
            }
        }
    }

    // 第二步：批量加载叶子节点
    for (const auto& entry : leaf_docs) {
        int leaf_id = entry.first;
        auto node = loadNode(leaf_id);
        if (node) {
            leaf_nodes[leaf_id] = node;
        }
    }

    // 第三步：批量添加文档到节点
    int insert_count = 0;
    for (const auto& entry : leaf_docs) {
        int leaf_id = entry.first;
        auto node = leaf_nodes[leaf_id];
        if (node) {
            for (const auto& doc : entry.second) {
                node->addDocument(doc);
                insert_count++;
            }

            saveNode(leaf_id, node);
        }
    }

    // 第四步：批量调整树结构（延迟调整）
    int adjust_count = 0;
    for (const auto& entry : leaf_nodes) {
        adjustTree(entry.first);
        adjust_count++;

        if (adjust_count % 100 == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Adjusted %zu nodes...", adjust_count);
            PRINT(msg);
        }
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "Tree insertion completed: %zu documents.", insert_count);
    PRINT(msg);
}

bool IRTree::parseDouble(const std::string& str, double& result) {
    // 简单的字符串转double实现
    const char* s = str.c_str();
    result = 0.0;
    double sign = 1.0;
    double fraction = 1.0;
    bool has_digit = false;
    
    // 处理符号
    if (*s == '-') {
        sign = -1.0;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // 整数部分
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        has_digit = true;
        s++;
    }
    
    // 小数部分
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            result = result * 10.0 + (*s - '0');
            fraction *= 10.0;
            has_digit = true;
            s++;
        }
    }
    
    if (!has_digit) {
        return false;
    }
    
    result = sign * result / fraction;
    return true;
}

// 刷新缓存 - 将所有缓存节点写回存储
void IRTree::flushNodeCache() {
    g_cache_mutex.lock();
    for (const auto& entry : node_cache) {
        storage->storeNode(entry.first, NodeSerializer::serialize(*entry.second));
    }
    node_cache.clear();
    g_cache_mutex.unlock();
}

// 缓存优化的节点加载 - 先查缓存，没有再加载
std::shared_ptr<Node> IRTree::cachedLoadNode(int node_id) const {
    g_cache_mutex.lock();

    auto it = node_cache.find(node_id);
    if (it != node_cache.end()) {
        g_cache_mutex.unlock();
        return it->second;  // 缓存命中
    }

    // 缓存未命中，从存储加载
    auto node = loadNode(node_id);
    if (node) {
        // 简单的缓存管理
        if (node_cache.size() >= MAX_CACHE_SIZE) {
            node_cache.erase(node_cache.begin());
        }
        node_cache[node_id] = node;
    }

    g_cache_mutex.unlock();
    return node;
}

// 缓存优化的节点保存 - 更新缓存并异步保存到存储
void IRTree::cachedSaveNode(int node_id, std::shared_ptr<Node> node) {
    if (!node) return;

    g_cache_mutex.lock();
    node_cache[node_id] = node;
    g_cache_mutex.unlock();

    // 异步保存到存储
    storage->storeNode(node_id, NodeSerializer::serialize(*node));
}

// 关键优化：自底向上构建树 - 避免频繁的树调整操作
void IRTree::buildTreeBottomUp(const std::vector<std::shared_ptr<Document>>& documents) {

    if (documents.empty()) return;

    // 按空间位置聚类 - 提高局部性
    std::vector<std::shared_ptr<Document>> sorted_docs = documents;
    std::sort(sorted_docs.begin(), sorted_docs.end(),
        [](const std::shared_ptr<Document>& a, const std::shared_ptr<Document>& b) {
            return a->getLocation().getCenter()[0] < b->getLocation().getCenter()[0];
        });

    // 直接创建叶子节点，避免频繁的chooseLeaf调用
    std::vector<std::shared_ptr<Node>> leaf_nodes;
    const int LEAF_CAPACITY = max_capacity;

    // 批量创建叶子节点
    for (size_t i = 0; i < sorted_docs.size(); i += LEAF_CAPACITY) {
        size_t end_index = std::min(i + LEAF_CAPACITY, sorted_docs.size());

        // 计算叶子节点的MBR - 包含该批次所有文档
        MBR leaf_mbr = sorted_docs[i]->getLocation();
        char msg[256];
        snprintf(msg, sizeof(msg), "First doc in leaf: [%.6f,%.6f] to [%.6f,%.6f]", 
                sorted_docs[i]->getLocation().getMin()[0], sorted_docs[i]->getLocation().getMin()[1],
                sorted_docs[i]->getLocation().getMax()[0], sorted_docs[i]->getLocation().getMax()[1]);
        PRINT(msg);
        for (size_t j = i + 1; j < end_index; j++) {
            leaf_mbr.expand(sorted_docs[j]->getLocation());
        }

        snprintf(msg, sizeof(msg), "Final leaf MBR: [%.6f,%.6f] to [%.6f,%.6f]", 
                leaf_mbr.getMin()[0], leaf_mbr.getMin()[1],
                leaf_mbr.getMax()[0], leaf_mbr.getMax()[1]);
        PRINT(msg);

        // 创建叶子节点
        int leaf_id = createNewNode(Node::LEAF, 0, leaf_mbr);
        auto leaf_node = cachedLoadNode(leaf_id);

        if (leaf_node) {
            // 批量添加文档
            for (size_t j = i; j < end_index; j++) {
                leaf_node->addDocument(sorted_docs[j]);
            }
            cachedSaveNode(leaf_id, leaf_node);
            leaf_nodes.push_back(leaf_node);
        }


        //// 定期报告进度
        //if (leaf_nodes.size() % 100 == 0) {
        //    std::cout << "Created " << leaf_nodes.size() << " leaf nodes..." << std::endl;
        //}
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Created %zu leaf nodes total.", leaf_nodes.size());
    PRINT(msg);


    // 自底向上构建树 - 从叶子节点开始构建上层节点
    std::vector<std::shared_ptr<Node>> current_level = leaf_nodes;
    int level = 1;

    while (current_level.size() > 1) {
        std::vector<std::shared_ptr<Node>> next_level;

        // 按空间位置排序当前层节点
        std::sort(current_level.begin(), current_level.end(),
            [](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                return a->getMBR().getCenter()[0] < b->getMBR().getCenter()[0];
            });

        // 创建父节点
        for (size_t i = 0; i < current_level.size(); i += max_capacity) {
            size_t end_index = std::min(i + max_capacity, current_level.size());

            // 计算父节点的MBR - 包含所有子节点
            MBR parent_mbr = current_level[i]->getMBR();
            for (size_t j = i + 1; j < end_index; j++) {
                parent_mbr.expand(current_level[j]->getMBR());
            }

            // 创建内部节点
            int parent_id = createNewNode(Node::INTERNAL, level, parent_mbr);
            auto parent_node = cachedLoadNode(parent_id);

            if (parent_node) {
                // 添加子节点
                for (size_t j = i; j < end_index; j++) {
                    parent_node->addChild(current_level[j]);
                }

                //计算并设置子节点的文本上界
                computeAndSetChildUpperBounds(parent_node);

                cachedSaveNode(parent_id, parent_node);
                next_level.push_back(parent_node);
            }
        }


        current_level = next_level;
        level++;
    }

    // 更新根节点
    if (!current_level.empty()) {
        root_node_id = current_level[0]->getId();
    }

    // 刷新所有缓存到存储
    flushNodeCache();
    PRINT("Bottom-up tree construction completed");
    initializeRecursivePositionMap();
}


void IRTree::computeAndSetChildUpperBounds(std::shared_ptr<Node> parent) {
    if (!parent || parent->getType() != Node::INTERNAL) {
        return;
    }

    int total_docs = global_index.getTotalDocuments();
    auto child_nodes = parent->getChildNodes();

    for (const auto& child : child_nodes) {
        int child_id = child->getId();

        // 计算该子节点的文本相关性上界
        double text_upper_bound = 0.0;
        const auto& tf_max_map = child->getTFMax();

        for (const auto& tf_pair : tf_max_map) {
            const std::string& term = tf_pair.first;
            int tf_max = tf_pair.second;

            // 获取全局文档频率
            int term_id = vocab.getTermId(term);
            if (term_id == -1) continue;

            int global_df = global_index.getDocumentFrequency(term_id);
            if (global_df == 0) continue;

            // 计算该词的最大可能TF-IDF值
            double max_tfidf = Vector::computeTFIDFWeight(tf_max, global_df, total_docs);
            text_upper_bound = std::max(text_upper_bound, max_tfidf);
        }

        // 设置到父节点中
        parent->setChildTextUpperBound(child_id, text_upper_bound);
    }
}

