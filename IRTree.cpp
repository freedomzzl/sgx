#include"IRTree.h"
#include "NodeSerializer.h"  
#include <sstream>
#include <queue>
#include <stack>
#include <algorithm>
#include <cmath>
#include "ringoram.h"
#include "RingoramStorage.h"
#include "SGXEnclave_t.h"
#include <sgx_trts.h>


#define PRINT(msg) ocall_print_string(msg)

IRTree::IRTree(std::shared_ptr<StorageInterface> storage_impl,
    int dims, int min_cap, int max_cap)
    : storage(storage_impl), dimensions(dims), min_capacity(min_cap),
    max_capacity(max_cap), next_node_id(0), next_doc_id(0), 
    tree_level(1), cache_level(0), nodes_visited(0){

    // 创建根节点 - 初始化为全零MBR的叶子节点
    MBR root_mbr(std::vector<double>(dims, 0.0), std::vector<double>(dims, 0.0));
    root_node_id = createNewNode(Node::LEAF, 0, root_mbr);

     PRINT("IRTree initialized with storage interface");
}

// 节点管理方法
std::shared_ptr<Node> IRTree::loadNode(int node_id) const {
    // 从存储中读取节点数据
    PRINT("--------loadNode readNode");
    auto node_data = storage->readNode(node_id);
    if (node_data.empty()) {
        // std::cout << "No data found for node " << node_id << std::endl;
        return nullptr;
    }

    // 反序列化节点数据为节点对象
    auto node = NodeSerializer::deserialize(node_data);
    if (!node) {
        char msg[256];
        snprintf(msg,sizeof(msg),"Failed to deserialize node %d",node_id);
        PRINT(msg);
    }

    return node;
}

void IRTree::saveNode(int node_id, std::shared_ptr<Node> node) {
    if (!node) {
        char msg[256];
        snprintf(msg,sizeof(msg),"Cannot save null node with ID %d",node_id);
        PRINT(msg);
        return;
    }

    // 序列化节点对象为字节数据
    auto node_data = NodeSerializer::serialize(*node);
    if (node_data.empty()) {
        char msg[256];
        snprintf(msg,sizeof(msg),"Failed to serialize node %d",node_id);
        PRINT(msg);
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
        char msg[256];
        snprintf(msg,sizeof(msg),"WARNING: Creating leaf node with level %d (should be 0)",level);
        PRINT(msg);
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
    // saveNode(new_node_id, new_node);
    
    // 将新节点加入缓存
    std::lock_guard<std::mutex> lock(cache_mutex);
    node_cache[new_node_id] = new_node;

    return new_node_id;
}



double IRTree::computeNodeRelevance(std::shared_ptr<Node> node, 
                                   const std::vector<std::string>& keywords,
                                   const MBR& spatial_scope, 
                                   double alpha) const
{
    if (!node) return 0.0;

    // === 1. 空间相关性计算 ===
    double spatial_rel = computeSpatialRelevance(node->getMBR(), spatial_scope);
    if (spatial_rel == 0.0) return 0.0;

    // === 2. 文本相关性上界 ===
    double text_upper_bound = 0.0;
    int total_docs = global_index.getTotalDocuments();
    int matched_keywords = 0;

    // 预计算查询词的全局文档频率
    std::vector<double> keyword_weights;
    for (const auto& keyword : keywords) {
        int term_id = vocab.getTermId(keyword);
        if (term_id == -1) {
            keyword_weights.push_back(0.0);
            continue;
        }
        
        int global_df = global_index.getDocumentFrequency(term_id);
        if (global_df == 0) {
            keyword_weights.push_back(0.0);
            continue;
        }
        
        // IDF权重（对数形式）
        double idf_weight = log((total_docs + 1.0) / (global_df + 1.0));
        keyword_weights.push_back(idf_weight);
    }

    // 计算节点中每个查询词的最大可能TF-IDF
    for (size_t i = 0; i < keywords.size(); i++) {
        const auto& keyword = keywords[i];
        double idf_weight = keyword_weights[i];
        if (idf_weight <= 0.0) continue; // 跳过无效关键词

        // 获取节点中该词的最大词频
        int tf_max = node->getMaxTermFrequency(keyword);
        if (tf_max == 0) continue;

        // 获取节点中该词的文档频率
        int node_df = node->getDocumentFrequency(keyword);
        
        // 考虑节点内文档频率的影响
        // 如果节点中只有少量文档包含该词，降低权重
        double node_df_factor = (node_df > 0) ? 
            std::min(1.0, log(1.0 + node_df) / log(2.0)) : 0.0;
        
        // 计算更精确的TF-IDF上界
        double max_tfidf = tf_max * idf_weight * node_df_factor;
        text_upper_bound += max_tfidf;
        matched_keywords++;
    }

    // 归一化文本相关性上界
    if (matched_keywords > 0) {
        
        text_upper_bound = text_upper_bound / (keywords.size() * sqrt(10.0));
        text_upper_bound = std::min(1.0, text_upper_bound);
    } else {
        return 0.0; // 没有匹配的关键词
    }

    // === 3. 综合相关性 ===
    double joint_relevance = computeJointRelevance(text_upper_bound, spatial_rel, alpha);
    
    // 额外的剪枝：如果空间相关性很低，且alpha不偏向空间，降低分数
    if (spatial_rel < 0.1 && alpha < 0.3) {
        joint_relevance *= 0.5; // 惩罚低空间相关性
    }

    return joint_relevance;
}

void IRTree::processLeafNode(std::shared_ptr<Node> leaf_node,
    const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    double alpha,
    std::vector<TreeHeapEntry>& results,
    int k,                    // 最大结果数
    double threshold) const { // 当前阈值
    
    if (!leaf_node || leaf_node->getType() != Node::LEAF) {
        return;
    }

    // 设置关键词匹配阈值（30%）
    const double KEYWORD_MATCH_THRESHOLD = 0.3;
    int min_keywords_needed = std::max(1, (int)(keywords.size() * KEYWORD_MATCH_THRESHOLD));
    
    // ============ 快速预检查 ============
    // 1. 检查叶子节点MBR是否与查询范围重叠
    if (!leaf_node->getMBR().overlaps(spatial_scope)) {
        return; // 整个叶子节点都不在范围内
    }
    
    // 2. 快速文本过滤：检查叶子节点是否至少包含30%的查询关键词
    int keyword_match_count = 0;
    for (const auto& keyword : keywords) {
        if (leaf_node->getDocumentFrequency(keyword) > 0) {
            keyword_match_count++;
            if (keyword_match_count >= min_keywords_needed) {
                break; // 达到阈值，无需继续检查
            }
        }
    }
    if (keyword_match_count < min_keywords_needed) {
        return; // 不满足最低关键词匹配要求
    }

    auto documents = leaf_node->getDocuments();
    int found_count = 0;
    int checked_count = 0;
    
    // ============ 预计算查询词的IDF ============
    std::vector<double> keyword_idf;
    int total_docs = global_index.getTotalDocuments();
    
    for (const auto& keyword : keywords) {
        int term_id = vocab.getTermId(keyword);
        if (term_id == -1) {
            keyword_idf.push_back(0.0);
            continue;
        }
        int df = global_index.getDocumentFrequency(term_id);
        if (df == 0) {
            keyword_idf.push_back(0.0);
            continue;
        }
        double idf = log((total_docs + 1.0) / (df + 1.0));
        keyword_idf.push_back(idf);
    }
    
    // ============ 收集文档和分数 ============
    std::vector<TreeHeapEntry> leaf_results; // 临时存储
    
    for (const auto& doc : documents) {
        checked_count++;
        
        // 1. 快速空间过滤
        if (!doc->getLocation().overlaps(spatial_scope)) {
            continue;
        }
        
        // 2. 计算匹配的关键词数量和分数
        int matched_keywords = 0;
        double text_score = 0.0;
        
        for (size_t i = 0; i < keywords.size(); i++) {
            const auto& keyword = keywords[i];
            int tf = doc->getTermFrequency(keyword);
            if (tf > 0 && keyword_idf[i] > 0) {
                matched_keywords++;
                text_score += tf * keyword_idf[i];
            }
        }
        
        // 检查是否满足最低匹配要求
        if (matched_keywords < min_keywords_needed) {
            continue; 
        }
        
        // 3. 计算空间相关性
        double spatial_rel = computeSpatialRelevance(doc->getLocation(), spatial_scope);
        if (spatial_rel == 0.0) continue;
        
        // 4. 归一化文本分数
        double normalized_text = 0.0;
        if (matched_keywords > 0) {
            // 归一化到[0,1]范围，考虑匹配关键词比例
            double tfidf_max = text_score;
            double match_ratio = (double)matched_keywords / keywords.size();
            
            // 如果只匹配部分关键词，适当降低分数
            normalized_text = (tfidf_max / (keywords.size() * 10.0)) * match_ratio;
            normalized_text = std::min(1.0, normalized_text);
        }
        
        // 5. 计算综合分数
        double joint_rel = computeJointRelevance(normalized_text, spatial_rel, alpha);
        
        // 6. 阈值剪枝
        if (joint_rel < threshold && results.size() >= k) {
            continue; // 分数低于当前阈值，剪枝
        }
        
        // 7. 添加到临时结果
        leaf_results.push_back(TreeHeapEntry(doc, joint_rel));
        found_count++;
        
        //如果叶子节点内找到太多文档，提前终止
        if (found_count > 10 && results.size() >= k) {
            break; 
        }
    }
    
    // ============ 合并结果 ============
    if (!leaf_results.empty()) {
        // 按分数排序（降序）
        std::sort(leaf_results.begin(), leaf_results.end(),
            [](const TreeHeapEntry& a, const TreeHeapEntry& b) {
                return a.score > b.score;
            });
        
        // 计算还能添加多少结果
        int remaining_slots = k - results.size();
        if (remaining_slots <= 0) return; // 结果已满
        
        // 添加高质量结果
        int to_add = std::min(remaining_slots, (int)leaf_results.size());
        for (int i = 0; i < to_add; i++) {
            results.push_back(leaf_results[i]);
        }
     
        if (to_add > 0 && results.size() >= k) {
            std::sort(results.begin(), results.end(),
                [](const TreeHeapEntry& a, const TreeHeapEntry& b) {
                    return a.score > b.score;
                });
           
            if (results.size() > k) {
                results.resize(k);
            }
        }
    }
}

void IRTree::processInternalNodeWithPath(
    std::shared_ptr<Node> internal_node,
    const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    double alpha,
    std::priority_queue<TreeHeapEntry, std::vector<TreeHeapEntry>, TreeHeapComparator>& queue)
{
    if (!internal_node || internal_node->getType() != Node::INTERNAL) {
        return;
    }

    // ============ 预检查1：空间重叠 ============
    if (!internal_node->getMBR().overlaps(spatial_scope)) {
        return;
    }

    // ============ 预检查2：关键词存在性 ============
    bool has_potential_keywords = false;
    for (const auto& keyword : keywords) {
        if (internal_node->getDocumentFrequency(keyword) > 0) {
            has_potential_keywords = true;
            break;
        }
    }
    if (!has_potential_keywords) {
        return;
    }

    // ============ 收集候选子节点 ============
    struct ChildInfo {
        int child_id;
        int child_path;
        double estimated_relevance;
    };
    std::vector<ChildInfo> candidates;

    const auto& child_position_map = internal_node->getChildPositionMap();
    const auto& child_mbr_map = internal_node->getChildMBRMap();
    const auto& child_text_bounds = internal_node->getChildTextUpperBounds();

    for (const auto& pos_pair : child_position_map) {
        int child_id = pos_pair.first;
        int child_path = pos_pair.second;

        // 1. 空间过滤
        auto mbr_it = child_mbr_map.find(child_id);
        if (mbr_it == child_mbr_map.end()) continue;
        if (!mbr_it->second.overlaps(spatial_scope)) continue;

        // 2. 文本过滤
        if (!internal_node->childHasAllKeywords(child_id, keywords)) continue;

        // 3. 估计相关性
        double text_upper = 0.0;
        auto text_it = child_text_bounds.find(child_id);
        if (text_it != child_text_bounds.end()) {
            text_upper = text_it->second;
        } else {
            text_upper = 0.5; // 保守估计
        }

        double spatial_est = 0.5;
        if (spatial_scope.contains(mbr_it->second)) {
            spatial_est = 1.0;
        }

        double estimated_rel = alpha * text_upper + (1.0 - alpha) * spatial_est;
        
        // 只有估计分数足够高才加入候选
        if (estimated_rel >= 0.5) {  // 阈值可以调整
            candidates.push_back({child_id, child_path, estimated_rel});
        }
    }

    // ============ 按估计分数排序（高的在前） ============
    std::sort(candidates.begin(), candidates.end(),
        [](const ChildInfo& a, const ChildInfo& b) {
            return a.estimated_relevance > b.estimated_relevance;
        });

    // ============ 只加载前几个最有希望的节点 ============
    const int MAX_NODES_TO_LOAD = 2;  
    
    int loaded_count = 0;
    for (const auto& candidate : candidates) {
        // 如果已经加载够了，停止
        if (loaded_count >= MAX_NODES_TO_LOAD) {
            break;
        }
        
        // 分数太低的也跳过
        if (candidate.estimated_relevance < 0.5) {  // 阈值可以调整
            continue;
        }

        // 加载子节点
        std::shared_ptr<Node> child_node;
        if (internal_node->getLevel() < cache_end_level) {
            child_node = accessNodeByPath(candidate.child_path);
            nodes_visited++;
        } else {
            child_node = cachedLoadNode(candidate.child_id);
        }
        
        if (!child_node) continue;

        // 计算精确相关性
        double relevance = computeNodeRelevance(child_node, keywords, spatial_scope, alpha);
        
        if (relevance > 0) {
            queue.push(TreeHeapEntry(child_node, candidate.child_path, relevance));
            loaded_count++;  // 成功加载并加入队列才计数
        }
    }
}



double IRTree::computeSpatialRelevance(const MBR& doc_location, const MBR& query_scope) const {
    // 1. 检查是否重叠（基本过滤）
    if (!doc_location.overlaps(query_scope)) {
        return 0.0;
    }

    // 2. 对于点位置文档（小MBR），使用距离倒数
    double doc_area = doc_location.area();
    double query_area = query_scope.area();
    
    // 如果文档位置近似为点
    if (doc_area < 0.0001) { // 很小的面积
        // 计算中心点距离
        std::vector<double> doc_center = doc_location.getCenter();
        std::vector<double> query_center = query_scope.getCenter();
        
        // 计算欧氏距离
        double distance = 0.0;
        for (size_t i = 0; i < doc_center.size(); i++) {
            double diff = doc_center[i] - query_center[i];
            distance += diff * diff;
        }
        distance = sqrt(distance);
        
        // 如果文档中心在查询范围内，返回1.0；否则返回距离倒数
        if (query_scope.contains(doc_location)) {
            return 1.0;
        } else {
            // 归一化距离：除以查询范围对角线长度
            double diag_length = 0.0;
            for (size_t i = 0; i < query_scope.getMin().size(); i++) {
                double diff = query_scope.getMax()[i] - query_scope.getMin()[i];
                diag_length += diff * diff;
            }
            diag_length = sqrt(diag_length);
            
            if (diag_length == 0) return 0.0;
            double normalized_dist = distance / diag_length;
            return 1.0 / (1.0 + normalized_dist);
        }
    }
    
    // 3. 对于区域文档，计算重叠比例
    double overlap_area = 1.0;
    for (size_t i = 0; i < doc_location.getMin().size(); i++) {
        double overlap_min = std::max(doc_location.getMin()[i], query_scope.getMin()[i]);
        double overlap_max = std::min(doc_location.getMax()[i], query_scope.getMax()[i]);
        
        if (overlap_min >= overlap_max) {
            return 0.0;
        }
        overlap_area *= (overlap_max - overlap_min);
    }

    if (doc_area == 0) return 1.0;
    
    // 返回重叠比例，但对小重叠进行惩罚
    double overlap_ratio = overlap_area / doc_area;
    
    // 如果重叠比例很小，降低分数
    if (overlap_ratio < 0.1) {
        overlap_ratio *= 0.3; // 惩罚小重叠
    }
    
    return overlap_ratio;
}

double IRTree::computeJointRelevance(double text_relevance, double spatial_relevance, double alpha) const
{
    // 线性加权组合文本相关性和空间相关性
    return alpha * text_relevance + (1 - alpha) * spatial_relevance;
}



// 初始化递归位置映射
void IRTree::initializeRecursivePositionMap() {
    // 从根节点开始递归分配路径
    int root_path = assignPathRecursively(root_node_id);

    if (root_path != -1) {
        // 存储根节点路径到 STASH
        setRootPath(root_path);
    }
    else {
        PRINT("Failed to assign path to root node");
    }

}

// 递归分配路径
int IRTree::assignPathRecursively(int node_id) {
    auto node = cachedLoadNode(node_id);
    if (!node) {
        // std::cerr << "Failed to load node " << node_id << " for path assignment" << std::endl;
        return -1;
    }

    // 为当前节点分配随机路径
    int current_path = getRandomLeafPath();

    // 为路径分配块索引
    auto ring_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (ring_oram_storage) {
        int block_index = ring_oram_storage->allocateBlockForPath(current_path);
        ring_oram_storage->mapPathToNode(current_path, node_id);
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
    cachedSaveNode(node_id, node);

    return current_path;
}

// 获取随机叶子路径
int IRTree::getRandomLeafPath() const {
 
    uint32_t random_value;
    sgx_status_t ret = sgx_read_rand((uint8_t*)&random_value, sizeof(random_value));
    
    if (ret != SGX_SUCCESS) {
        ocall_print_string("Warning: sgx_read_rand failed, using fallback");
        
    }
    
    return (int)(random_value % numLeaves);
}



void IRTree::setRootPath(int path) {
    if (!storage) {
        PRINT("Storage not available for root path setting");
        return;
    }

    // 动态转换到RingOramStorage来设置根路径
    auto ring_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (ring_oram_storage) {
        ring_oram_storage->setRootPath(path);
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
    auto ring_oram_storage = std::dynamic_pointer_cast<RingOramStorage>(storage);
    if (ring_oram_storage) {
        auto node_data = ring_oram_storage->accessByPath(path);
        if (node_data.empty()) {
            // std::cerr << "Failed to access node data for path " << path << std::endl;
            return nullptr;
        }

        // 反序列化节点数据
        auto node = NodeSerializer::deserialize(node_data);
        if (!node) {
            char msg[256];
            snprintf(msg,sizeof(msg),"Failed to deserialize node from path %d",path);
            PRINT(msg);
            return nullptr;
        }

        return node;
    }
    else {
        PRINT("Storage is not RingOramStorage, cannot use path-based access");
        return nullptr;
    }
}



std::vector<TreeHeapEntry> IRTree::search(const Query& query)
{
    // 委托给参数化搜索方法
    return search(query.getKeywords(), query.getSpatialScope(), query.getK(), query.getAlpha());
}


std::vector<TreeHeapEntry> IRTree::search(const std::vector<std::string>& keywords,
    const MBR& spatial_scope,
    int k,
    double alpha) {

    search_blocks = 0;
    nodes_visited=0;
    std::vector<TreeHeapEntry> results;

    if (!storage || keywords.empty() || k <= 0) {
        return results;
    }

    // 加载根节点
    auto root_node = cachedLoadNode(root_node_id);
    if (!root_node) {
        char msg[256];
        snprintf(msg,sizeof(msg),"Failed to load root node using path");
        PRINT(msg);
        return results;
    }
   
    // 使用优先队列
    std::priority_queue<TreeHeapEntry, std::vector<TreeHeapEntry>, TreeHeapComparator> queue;

    
    // 计算根节点相关性
    double root_relevance = computeNodeRelevance(root_node, keywords, spatial_scope, alpha);
    
    if (root_relevance > 0) {
        queue.push(TreeHeapEntry(root_node, root_relevance));
    }

    int documents_checked = 0;
    double threshold = 0.0;
    int pruned_nodes = 0;
    int pruned_leaves = 0;

  
    while (!queue.empty() && results.size() < k) {
        TreeHeapEntry current = queue.top();
        queue.pop();
       

        auto node = current.node; // 确保current总是节点
        
        // ============ 节点剪枝 ============
        if (results.size() > 0 && current.score < threshold * 0.9) {
            pruned_nodes++;
            continue;
        }

        if (node->getType() == Node::LEAF) {
            // ============ 叶子节点处理 ============
            // 快速预检查
            if (!node->getMBR().overlaps(spatial_scope)) {
                continue; // 无空间重叠
            }
            
            // 检查关键词匹配（30%阈值）
            bool has_potential = false;
            int keyword_match = 0;
            const double KEYWORD_THRESHOLD = 0.3;
            int min_needed = std::max(1, (int)(keywords.size() * KEYWORD_THRESHOLD));
            
            for (const auto& keyword : keywords) {
                if (node->getDocumentFrequency(keyword) > 0) {
                    keyword_match++;
                    if (keyword_match >= min_needed) {
                        has_potential = true;
                        break;
                    }
                }
            }
            
            if (!has_potential) {
                pruned_leaves++;
                continue;
            }
            
            // 计算叶子上界
            double leaf_upper_bound = computeNodeRelevance(node, keywords, spatial_scope, alpha);
            if (results.size() > 0 && leaf_upper_bound < threshold * 0.8) {
                pruned_leaves++;
                continue;
            }
            
            // 处理叶子节点中的文档
            int prev_results = results.size();
            processLeafNode(node, keywords, spatial_scope, alpha, results, k, threshold);
            documents_checked += (results.size() - prev_results);
            
            // 如果达到k个结果，更新阈值
            if (results.size() >= k) {
                // 使用最小堆维护top-k
                std::make_heap(results.begin(), results.end(), 
                    [](const TreeHeapEntry& a, const TreeHeapEntry& b) {
                        return a.score > b.score; // 最小堆
                    });
                threshold = results.front().score; // 最小分数
                
            
                // 如果队列中最高分 < threshold，可以提前结束
                if (!queue.empty() && queue.top().score < threshold) {
                    break;
                }
            }
        }
        else {
            // ============ 内部节点处理 ============
      
            processInternalNodeWithPath(node, keywords, 
                                       spatial_scope, alpha, queue);
        }
    }

    // 最终排序（降序）
    std::sort(results.begin(), results.end(),
        [](const TreeHeapEntry& a, const TreeHeapEntry& b) {
            return a.score > b.score;
        });

    return results;
}



// 优化的批量插入方法
void IRTree::optimizedBulkInsertFromFile(const std::string& filename) {
    std::vector<std::tuple<std::string, double, double>> documents;
    int loaded_count = 0;
    const int REPORT_INTERVAL = 1000;

    // 获取文件大小
    size_t file_size = 0;
    sgx_status_t retval;
    sgx_status_t status = ocall_get_file_size(&retval, filename.c_str(), &file_size);
    if (status != SGX_SUCCESS || retval != SGX_SUCCESS || file_size == 0) {
        ocall_print_string("Error: Cannot get file size");
        return;
    }

    // 读取文件内容
    std::vector<char> file_buffer(file_size + 1, '\0');
    size_t actual_size = 0;

    status = ocall_read_file(&retval, filename.c_str(),
                            reinterpret_cast<uint8_t*>(file_buffer.data()),
                            file_size, &actual_size);
    if (status != SGX_SUCCESS || retval != SGX_SUCCESS || actual_size == 0) {
        ocall_print_string("Error: Cannot read file");
        return;
    }

    // 用C字符串解析
    char* saveptr1;
    char* line = strtok_r(file_buffer.data(), "\n", &saveptr1);

    while (line != nullptr) {
        if (line[0] == '\0') {
            line = strtok_r(nullptr, "\n", &saveptr1);
            continue;
        }

        char* saveptr2;
        char* text = strtok_r(line, "|", &saveptr2);
        char* lon_str = strtok_r(nullptr, "|", &saveptr2);
        char* lat_str = strtok_r(nullptr, "|", &saveptr2);

        if (text && lon_str && lat_str) {
            double lon = std::strtod(lon_str, nullptr);
            double lat = std::strtod(lat_str, nullptr);
            documents.emplace_back(std::string(text), lon, lat);
            loaded_count++;

            if (loaded_count % REPORT_INTERVAL == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Loaded %d records...", loaded_count);
                ocall_print_string(msg);
            }
        }

        line = strtok_r(nullptr, "\n", &saveptr1);
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "File loading completed: %d records", loaded_count);
    ocall_print_string(msg);

    if (!documents.empty()) {
        optimizedBulkInsertDocuments(documents);
    }
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

    // 阶段2：批量构建全局索引
    bulkBuildGlobalIndex(doc_objects);

    // 阶段3：使用自底向上的方式构建树
    buildTreeBottomUp(doc_objects);
}

void IRTree::bulkBuildGlobalIndex(const std::vector<std::shared_ptr<Document>>& documents) {
 
    // 批量词汇表构建 - 先收集所有词汇
    std::unordered_map<std::string, int> term_frequencies;

    // 先收集所有词汇及其频率
    for (const auto& doc : documents) {
        std::istringstream iss(doc->getText());
        std::string word;
        while (iss >> word) {
            term_frequencies[word]++;
        }
    }

    // 批量添加到词汇表
    for (const auto& term : term_frequencies) {
        vocab.addTerm(term.first); // 这会创建或获取term id
    }

    // 批量构建文档向量
    for (const auto& doc : documents) {
        Vector doc_vector(doc->getId());
        Vector::vectorize(doc_vector, doc->getText(), vocab);
        global_index.addDocument(doc->getId(), doc_vector);
    }
}



// 将下半层节点写回存储
void IRTree::flushNodeCache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    for (auto it = node_cache.begin(); it != node_cache.end(); ) {
        if (it->second->getLevel() < cache_end_level) {
            // 将节点写入存储
            if (storage->storeNode(it->first, NodeSerializer::serialize(*it->second))) {
                // 写入成功，从缓存中移除
                it = node_cache.erase(it);  // erase返回下一个元素的迭代器
            } else {
                // 写入失败，保留在缓存中，继续下一个
                ++it;
            }
        } else {
            ++it;
        }
    }
}

// 缓存优化的节点加载  先查缓存，没有再加载
std::shared_ptr<Node> IRTree::cachedLoadNode(int node_id) const {
    std::lock_guard<std::mutex> lock(cache_mutex);

    auto it = node_cache.find(node_id);
    if (it != node_cache.end()) {

        return it->second;  // 缓存命中
    }

    // 缓存未命中，从存储加载
    auto node = loadNode(node_id);
    if (node) {
        node_cache[node_id] = node;
    }

    return node;
}

// 缓存优化的节点保存  更新缓存并异步保存到存储
void IRTree::cachedSaveNode(int node_id, std::shared_ptr<Node> node) {
    if (!node) return;

    std::lock_guard<std::mutex> lock(cache_mutex);
    node_cache[node_id] = node;

    // 异步保存到存储
    // storage->storeNode(node_id, NodeSerializer::serialize(*node));
}

// 自底向上构建树 - 避免频繁的树调整操作
void IRTree::buildTreeBottomUp(const std::vector<std::shared_ptr<Document>>& documents) {
    
    if (documents.empty()) return;

    // 按空间位置聚类 - 提高局部性
    std::vector<std::shared_ptr<Document>> sorted_docs = documents;
    std::sort(sorted_docs.begin(), sorted_docs.end(),
        [](const std::shared_ptr<Document>& a, const std::shared_ptr<Document>& b) {
            return a->getLocation().getCenter()[0] < b->getLocation().getCenter()[0];
        });

    
    std::vector<std::shared_ptr<Node>> leaf_nodes;
    const int LEAF_CAPACITY = max_capacity;

    // 批量创建叶子节点
    for (size_t i = 0; i < sorted_docs.size(); i += LEAF_CAPACITY) {
        size_t end_index = std::min(i + LEAF_CAPACITY, sorted_docs.size());

        // 计算叶子节点的MBR - 包含该批次所有文档
        MBR leaf_mbr = sorted_docs[i]->getLocation();
        for (size_t j = i + 1; j < end_index; j++) {
            leaf_mbr.expand(sorted_docs[j]->getLocation());
        }

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
    }

    char msg[256];
    snprintf(msg,sizeof(msg),"Created %zu leaf nodes total",leaf_nodes.size());
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
                cachedSaveNode(parent_id, parent_node);
                next_level.push_back(parent_node);
            }
        }


        current_level = next_level;
        level++;
    }

    tree_level=level;
    cache_level = tree_level / 2;
    cache_end_level=tree_level-cache_level;

    // 更新根节点
    if (!current_level.empty()) {
        root_node_id = current_level[0]->getId();
    }

    initializeRecursivePositionMap();
    flushNodeCache();
    PRINT("Bottom-up tree construction completed");
}

