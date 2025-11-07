#include "NodeSerializer.h"
#include <iostream>
#include <cstring>


// 写入整数到字节流
bool NodeSerializer::writeInt(std::vector<uint8_t>& data, int value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(int));
    return true;
}

// 从字节流读取整数
bool NodeSerializer::readInt(const std::vector<uint8_t>& data, size_t& offset, int& value) {
    if (offset + sizeof(int) > data.size()) {
        return false;
    }
    memcpy(&value, data.data() + offset, sizeof(int));
    offset += sizeof(int);
    return true;
}

// 写入双精度浮点数到字节流
bool NodeSerializer::writeDouble(std::vector<uint8_t>& data, double value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(double));
    return true;
}

// 从字节流读取双精度浮点数
bool NodeSerializer::readDouble(const std::vector<uint8_t>& data, size_t& offset, double& value) {
    if (offset + sizeof(double) > data.size()) {
        return false;
    }
    memcpy(&value, data.data() + offset, sizeof(double));
    offset += sizeof(double);
    return true;
}

// 写入字符串到字节流
bool NodeSerializer::writeString(std::vector<uint8_t>& data, const std::string& str) {
    if (!writeInt(data, static_cast<int>(str.size()))) {
        return false;
    }
    data.insert(data.end(), str.begin(), str.end());
    return true;
}

// 从字节流读取字符串
bool NodeSerializer::readString(const std::vector<uint8_t>& data, size_t& offset, std::string& str) {
    int size = 0;
    if (!readInt(data, offset, size)) {
        return false;
    }
    if (offset + size > data.size()) {
        return false;
    }
    str.assign(data.begin() + offset, data.begin() + offset + size);
    offset += size;
    return true;
}

// 序列化MBR
bool NodeSerializer::writeMBR(std::vector<uint8_t>& data, const MBR& mbr) {
    const auto& min_coords = mbr.getMinCoords();
    const auto& max_coords = mbr.getMaxCoords();

    if (!writeInt(data, static_cast<int>(min_coords.size()))) {
        return false;
    }
    for (double coord : min_coords) {
        if (!writeDouble(data, coord)) {
            return false;
        }
    }

    if (!writeInt(data, static_cast<int>(max_coords.size()))) {
        return false;
    }
    for (double coord : max_coords) {
        if (!writeDouble(data, coord)) {
            return false;
        }
    }

    return true;
}

// 反序列化MBR
bool NodeSerializer::readMBR(const std::vector<uint8_t>& data, size_t& offset, MBR& mbr) {
    int min_size = 0;
    if (!readInt(data, offset, min_size)) {
        return false;
    }
    
    std::vector<double> min_coords;
    for (int i = 0; i < min_size; i++) {
        double coord;
        if (!readDouble(data, offset, coord)) {
            return false;
        }
        min_coords.push_back(coord);
    }

    int max_size = 0;
    if (!readInt(data, offset, max_size)) {
        return false;
    }
    
    std::vector<double> max_coords;
    for (int i = 0; i < max_size; i++) {
        double coord;
        if (!readDouble(data, offset, coord)) {
            return false;
        }
        max_coords.push_back(coord);
    }

    mbr = MBR(min_coords, max_coords);
    return true;
}

// 序列化文档
sgx_status_t NodeSerializer::serializeDocument(const Document& doc, std::vector<uint8_t>& data) {
    data.clear();

    // 写入文档ID
    if (!writeInt(data, doc.getId())) {
        return SGX_ERROR_UNEXPECTED;
    }

    // 写入原始文本
    if (!writeString(data, doc.getText())) {
        return SGX_ERROR_UNEXPECTED;
    }

    // 写入位置信息 (MBR)
    const MBR& location = doc.getLocation();
    const auto& min_coords = location.getMinCoords();
    const auto& max_coords = location.getMaxCoords();

    if (!writeInt(data, static_cast<int>(min_coords.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }
    for (double coord : min_coords) {
        if (!writeDouble(data, coord)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    if (!writeInt(data, static_cast<int>(max_coords.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }
    for (double coord : max_coords) {
        if (!writeDouble(data, coord)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入词频信息
    const auto& term_freq = doc.getTermFreq();
    if (!writeInt(data, static_cast<int>(term_freq.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }

    // 使用传统的循环方式
    for (const auto& term_pair : term_freq) {
        const std::string& term = term_pair.first;
        int freq = term_pair.second;

        if (!writeString(data, term) || !writeInt(data, freq)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    return SGX_SUCCESS;
}

// 反序列化文档
sgx_status_t NodeSerializer::deserializeDocument(const std::vector<uint8_t>& data, std::shared_ptr<Document>& doc) {
    if (data.empty()) {
        return SGX_ERROR_INVALID_PARAMETER;
    }

    try {
        size_t offset = 0;

        // 读取文档ID
        int doc_id = 0;
        if (!readInt(data, offset, doc_id)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }

        // 读取原始文本
        std::string raw_text;
        if (!readString(data, offset, raw_text)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }

        // 读取位置信息 (MBR)
        int min_size = 0;
        if (!readInt(data, offset, min_size)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        std::vector<double> min_coords;
        for (int i = 0; i < min_size; i++) {
            double coord;
            if (!readDouble(data, offset, coord)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            min_coords.push_back(coord);
        }

        int max_size = 0;
        if (!readInt(data, offset, max_size)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        std::vector<double> max_coords;
        for (int i = 0; i < max_size; i++) {
            double coord;
            if (!readDouble(data, offset, coord)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            max_coords.push_back(coord);
        }

        MBR location(min_coords, max_coords);
        // 使用原始文本创建文档
        doc = std::make_shared<Document>(doc_id, location, raw_text);
        
        // 读取词频信息
        int term_count = 0;
        if (!readInt(data, offset, term_count)) {
            return SGX_SUCCESS; // 没有词频信息也可以
        }
        
        for (int i = 0; i < term_count; i++) {
            std::string term;
            int freq;
            if (!readString(data, offset, term) || !readInt(data, offset, freq)) {
                break; // 跳过错误的词频条目
            }
            // 在SGX中可能需要手动设置词频
        }

        return SGX_SUCCESS;
    }
    catch (const std::exception& e) {
        return SGX_ERROR_UNEXPECTED;
    }
}

// 序列化节点 - 完整版本
sgx_status_t NodeSerializer::serialize(const Node& node, std::vector<uint8_t>& data) {
    data.clear();

    // 写入节点基本信息
    if (!writeInt(data, node.getId()) ||
        !writeInt(data, static_cast<int>(node.getType())) ||
        !writeInt(data, node.getLevel()) ||
        !writeInt(data, node.getDocumentCount())) {
        return SGX_ERROR_UNEXPECTED;
    }

    // 写入MBR信息
    const MBR& mbr = node.getMBR();
    if (!writeMBR(data, mbr)) {
        return SGX_ERROR_UNEXPECTED;
    }

    // 写入子节点信息（如果是内部节点）
    if (node.getType() == Node::INTERNAL) {
        const auto& child_nodes = node.getChildNodes();
        if (!writeInt(data, static_cast<int>(child_nodes.size()))) {
            return SGX_ERROR_UNEXPECTED;
        }

        // 写入所有子节点的ID
        for (const auto& child : child_nodes) {
            if (!writeInt(data, child->getId())) {
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    else {
        if (!writeInt(data, 0)) { // 叶子节点没有子节点
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入文档列表（如果是叶子节点）
    if (node.getType() == Node::LEAF) {
        const auto& documents = node.getDocuments();
        if (!writeInt(data, static_cast<int>(documents.size()))) {
            return SGX_ERROR_UNEXPECTED;
        }

        for (const auto& doc : documents) {
            std::vector<uint8_t> doc_data;
            sgx_status_t ret = serializeDocument(*doc, doc_data);
            if (ret != SGX_SUCCESS) {
                return ret;
            }
            
            if (!writeInt(data, static_cast<int>(doc_data.size()))) {
                return SGX_ERROR_UNEXPECTED;
            }
            data.insert(data.end(), doc_data.begin(), doc_data.end());
        }
    }
    else {
        if (!writeInt(data, 0)) { // 内部节点没有文档
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入文档摘要信息
    const auto& df = node.getDF();
    const auto& tf_max = node.getTFMax();

    // 写入文档频率信息
    if (!writeInt(data, static_cast<int>(df.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }
    for (const auto& df_pair : df) {
        const std::string& term = df_pair.first;
        int freq = df_pair.second;
        if (!writeString(data, term) || !writeInt(data, freq)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入最大词频信息
    if (!writeInt(data, static_cast<int>(tf_max.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }
    for (const auto& tf_pair : tf_max) {
        const std::string& term = tf_pair.first;
        int max_freq = tf_pair.second;
        if (!writeString(data, term) || !writeInt(data, max_freq)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入子节点位置映射信息 - 对递归ORAM至关重要
    const auto& child_position_map = node.getChildPositionMap();
    if (!writeInt(data, static_cast<int>(child_position_map.size()))) {
        return SGX_ERROR_UNEXPECTED;
    }
    for (const auto& pos_pair : child_position_map) {
        int child_id = pos_pair.first;
        int path = pos_pair.second;
        if (!writeInt(data, child_id) || !writeInt(data, path)) {
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入子节点MBR信息（只在内部节点中存储）- 对空间剪枝至关重要
    if (node.getType() == Node::INTERNAL) {
        const auto& child_mbr_map = node.getChildMBRMap();  
        if (!writeInt(data, static_cast<int>(child_mbr_map.size()))) {
            return SGX_ERROR_UNEXPECTED;
        }
        for (const auto& mbr_pair : child_mbr_map) {
            int child_id = mbr_pair.first;
            const MBR& child_mbr = mbr_pair.second;
            if (!writeInt(data, child_id) || !writeMBR(data, child_mbr)) {
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    else {
        if (!writeInt(data, 0)) { // 叶子节点没有子节点MBR信息
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入子节点文本上界信息（只在内部节点中存储）- 对文本剪枝至关重要
    if (node.getType() == Node::INTERNAL) {
        const auto& child_text_bounds = node.getChildTextUpperBounds();
        if (!writeInt(data, static_cast<int>(child_text_bounds.size()))) {
            return SGX_ERROR_UNEXPECTED;
        }
        for (const auto& bound_pair : child_text_bounds) {
            int child_id = bound_pair.first;
            double upper_bound = bound_pair.second;
            if (!writeInt(data, child_id) || !writeDouble(data, upper_bound)) {
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    else {
        if (!writeInt(data, 0)) { // 叶子节点没有子节点文本上界信息
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 写入子节点关键词信息（只在内部节点中存储）- 对关键词过滤至关重要
    if (node.getType() == Node::INTERNAL) {
        const auto& child_keywords_map = node.getChildKeywordsMap();
        if (!writeInt(data, static_cast<int>(child_keywords_map.size()))) {
            return SGX_ERROR_UNEXPECTED;
        }
        for (const auto& keyword_pair : child_keywords_map) {
            int child_id = keyword_pair.first;
            const auto& keywords = keyword_pair.second;

            if (!writeInt(data, child_id)) {
                return SGX_ERROR_UNEXPECTED;
            }
            if (!writeInt(data, static_cast<int>(keywords.size()))) {
                return SGX_ERROR_UNEXPECTED;
            }

            for (const auto& keyword : keywords) {
                if (!writeString(data, keyword)) {
                    return SGX_ERROR_UNEXPECTED;
                }
            }
        }
    }
    else {
        if (!writeInt(data, 0)) { // 叶子节点没有子节点关键词信息
            return SGX_ERROR_UNEXPECTED;
        }
    }

    // 更新版本号到7，表示包含所有子节点优化信息
    if (!writeInt(data, 7)) {
        return SGX_ERROR_UNEXPECTED;
    }

    return SGX_SUCCESS;
}

// 反序列化节点 - 完整版本
sgx_status_t NodeSerializer::deserialize(const std::vector<uint8_t>& data, std::shared_ptr<Node>& node) {
    if (data.empty()) {
        return SGX_ERROR_INVALID_PARAMETER;
    }

    try {
        size_t offset = 0;

        // 读取节点基本信息
        int node_id = 0, node_type_val = 0, level = 0, doc_count = 0;
        if (!readInt(data, offset, node_id) ||
            !readInt(data, offset, node_type_val) ||
            !readInt(data, offset, level) ||
            !readInt(data, offset, doc_count)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }

        Node::Type node_type = static_cast<Node::Type>(node_type_val);

        // 读取MBR信息
        MBR mbr;
        if (!readMBR(data, offset, mbr)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }

        auto result_node = std::make_shared<Node>(node_id, node_type, level, mbr);

        // 读取子节点信息
        int child_count = 0;
        if (!readInt(data, offset, child_count)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        std::vector<int> child_ids;
        for (int i = 0; i < child_count; i++) {
            int child_id;
            if (!readInt(data, offset, child_id)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            child_ids.push_back(child_id);
        }

        // 读取文档列表或子节点ID（根据节点类型）
        if (node_type == Node::LEAF) {
            int document_count = 0;
            if (!readInt(data, offset, document_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            
            for (int i = 0; i < document_count; i++) {
                int doc_data_size = 0;
                if (!readInt(data, offset, doc_data_size) || 
                    offset + doc_data_size > data.size()) {
                    return SGX_ERROR_INVALID_PARAMETER;
                }

                std::vector<uint8_t> doc_data(data.begin() + offset,
                                            data.begin() + offset + doc_data_size);
                offset += doc_data_size;

                std::shared_ptr<Document> document;
                sgx_status_t ret = deserializeDocument(doc_data, document);
                if (ret == SGX_SUCCESS && document) {
                    result_node->addDocument(document);
                }
            }
        }
        else {
            // 内部节点：跳过文档数量（应该为0）
            int document_count = 0;
            if (!readInt(data, offset, document_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }

            // 为内部节点创建空的子节点对象（只有ID）
            for (int child_id : child_ids) {
                // 创建一个只有ID的子节点占位符
                MBR child_mbr(std::vector<double>(mbr.getMinCoords().size(), 0.0), 
                             std::vector<double>(mbr.getMaxCoords().size(), 0.0));
                auto child_node = std::make_shared<Node>(child_id, Node::LEAF, level - 1, child_mbr);
                result_node->addChild(child_node);
            }
        }

        // 读取文档频率信息
        int df_count = 0;
        if (!readInt(data, offset, df_count)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        std::unordered_map<std::string, int> df_map;
        for (int i = 0; i < df_count; i++) {
            std::string term;
            int freq;
            if (!readString(data, offset, term) || !readInt(data, offset, freq)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            df_map[term] = freq;
        }

        // 读取最大词频信息
        int tf_max_count = 0;
        if (!readInt(data, offset, tf_max_count)) {
            return SGX_ERROR_INVALID_PARAMETER;
        }
        
        std::unordered_map<std::string, int> tf_max_map;
        for (int i = 0; i < tf_max_count; i++) {
            std::string term;
            int max_freq;
            if (!readString(data, offset, term) || !readInt(data, offset, max_freq)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            tf_max_map[term] = max_freq;
        }

        // 读取子节点位置映射信息
        int position_map_count = 0;
        if (offset < data.size()) {
            if (!readInt(data, offset, position_map_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
        }

        std::unordered_map<int, int> child_position_map;
        for (int i = 0; i < position_map_count; i++) {
            if (offset >= data.size()) break;
            
            int child_id, path;
            if (!readInt(data, offset, child_id) || !readInt(data, offset, path)) {
                break;
            }
            child_position_map[child_id] = path;
        }

        // 设置子节点位置映射
        result_node->setChildPositionMap(child_position_map);

        // 读取子节点MBR信息
        std::unordered_map<int, MBR> child_mbr_map;
        if (offset < data.size()) {
            int child_mbr_count = 0;
            if (!readInt(data, offset, child_mbr_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            
            for (int i = 0; i < child_mbr_count; i++) {
                if (offset >= data.size()) break;
                
                int child_id;
                if (!readInt(data, offset, child_id)) {
                    break;
                }
                
                MBR child_mbr;
                if (!readMBR(data, offset, child_mbr)) {
                    break;
                }
                child_mbr_map[child_id] = child_mbr;
            }
        }

        // 设置子节点MBR信息
        for (const auto& mbr_pair : child_mbr_map) {
            result_node->setChildMBR(mbr_pair.first, mbr_pair.second);
        }

        // 读取子节点文本上界信息
        std::unordered_map<int, double> child_text_bounds;
        if (offset < data.size()) {
            int child_bounds_count = 0;
            if (!readInt(data, offset, child_bounds_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            
            for (int i = 0; i < child_bounds_count; i++) {
                if (offset >= data.size()) break;
                
                int child_id;
                double upper_bound;
                if (!readInt(data, offset, child_id) || !readDouble(data, offset, upper_bound)) {
                    break;
                }
                child_text_bounds[child_id] = upper_bound;
            }
        }

        // 设置子节点文本上界信息
        for (const auto& bound_pair : child_text_bounds) {
            result_node->setChildTextUpperBound(bound_pair.first, bound_pair.second);
        }

        // 读取子节点关键词信息
        std::unordered_map<int, std::unordered_set<std::string>> child_keywords_map;
        if (offset < data.size()) {
            int child_keywords_count = 0;
            if (!readInt(data, offset, child_keywords_count)) {
                return SGX_ERROR_INVALID_PARAMETER;
            }
            
            for (int i = 0; i < child_keywords_count; i++) {
                if (offset >= data.size()) break;
                
                int child_id;
                if (!readInt(data, offset, child_id)) {
                    break;
                }
                
                int keyword_count = 0;
                if (!readInt(data, offset, keyword_count)) {
                    break;
                }

                std::unordered_set<std::string> keywords;
                for (int j = 0; j < keyword_count; j++) {
                    if (offset >= data.size()) break;
                    
                    std::string keyword;
                    if (!readString(data, offset, keyword)) {
                        break;
                    }
                    keywords.insert(keyword);
                }

                child_keywords_map[child_id] = keywords;
            }
        }

        // 设置子节点关键词信息
        for (const auto& keyword_pair : child_keywords_map) {
            result_node->setChildKeywords(keyword_pair.first, keyword_pair.second);
        }

        // 读取版本号
        int version = 1;
        if (offset < data.size()) {
            if (!readInt(data, offset, version)) {
                // 版本号读取失败不影响主要功能
            }
        }

        // 手动设置文档摘要信息
        result_node->setDocumentSummary(df_map, tf_max_map);

        // 设置节点
        node = result_node;
        return SGX_SUCCESS;

    }
    catch (const std::exception& e) {
        return SGX_ERROR_UNEXPECTED;
    }
}