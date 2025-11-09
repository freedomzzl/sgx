#include "NodeSerializer.h"
#include <cstring>

// 写入整数到字节流
void NodeSerializer::writeInt(std::vector<uint8_t>& data, int value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(int));
}

// 从字节流读取整数
int NodeSerializer::readInt(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + sizeof(int) > data.size()) {
        // 返回默认值，不抛出异常
        return 0;
    }
    int value;
    memcpy(&value, data.data() + offset, sizeof(int));
    offset += sizeof(int);
    return value;
}

// 写入双精度浮点数到字节流
void NodeSerializer::writeDouble(std::vector<uint8_t>& data, double value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(double));
}

// 从字节流读取双精度浮点数
double NodeSerializer::readDouble(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + sizeof(double) > data.size()) {
        // 返回默认值，不抛出异常
        return 0.0;
    }
    double value;
    memcpy(&value, data.data() + offset, sizeof(double));
    offset += sizeof(double);
    return value;
}

// 写入字符串到字节流
void NodeSerializer::writeString(std::vector<uint8_t>& data, const std::string& str) {
    writeInt(data, static_cast<int>(str.size()));
    data.insert(data.end(), str.begin(), str.end());
}

// 从字节流读取字符串
std::string NodeSerializer::readString(const std::vector<uint8_t>& data, size_t& offset) {
    int size = readInt(data, offset);
    if (size < 0 || offset + size > data.size()) {
        // 返回空字符串，不抛出异常
        return "";
    }
    std::string str(data.begin() + offset, data.begin() + offset + size);
    offset += size;
    return str;
}

// 序列化MBR
void NodeSerializer::writeMBR(std::vector<uint8_t>& data, const MBR& mbr) {
    const auto& min_coords = mbr.getMin();
    const auto& max_coords = mbr.getMax();

    writeInt(data, static_cast<int>(min_coords.size()));
    for (double coord : min_coords) {
        writeDouble(data, coord);
    }

    writeInt(data, static_cast<int>(max_coords.size()));
    for (double coord : max_coords) {
        writeDouble(data, coord);
    }
}

// 反序列化MBR
MBR NodeSerializer::readMBR(const std::vector<uint8_t>& data, size_t& offset) {
    int min_size = readInt(data, offset);
    if (min_size < 0) {
        // 返回默认MBR
        return MBR(std::vector<double>{0, 0}, std::vector<double>{0, 0});
    }
    
    std::vector<double> min_coords;
    for (int i = 0; i < min_size; i++) {
        min_coords.push_back(readDouble(data, offset));
    }

    int max_size = readInt(data, offset);
    if (max_size < 0) {
        // 返回默认MBR
        return MBR(std::vector<double>{0, 0}, std::vector<double>{0, 0});
    }
    
    std::vector<double> max_coords;
    for (int i = 0; i < max_size; i++) {
        max_coords.push_back(readDouble(data, offset));
    }

    return MBR(min_coords, max_coords);
}

// 序列化文档
std::vector<uint8_t> NodeSerializer::serializeDocument(const Document& doc) {
    std::vector<uint8_t> data;

    // 写入文档ID
    writeInt(data, doc.getId());

    // 写入原始文本
    writeString(data, doc.getText());

    // 写入位置信息 (MBR)
    const MBR& location = doc.getLocation();
    const auto& min_coords = location.getMin();
    const auto& max_coords = location.getMax();

    writeInt(data, static_cast<int>(min_coords.size()));
    for (double coord : min_coords) {
        writeDouble(data, coord);
    }

    writeInt(data, static_cast<int>(max_coords.size()));
    for (double coord : max_coords) {
        writeDouble(data, coord);
    }

    // 写入词频信息
    const auto& term_freq = doc.getTermFreq();
    writeInt(data, static_cast<int>(term_freq.size()));

    // 使用传统的循环方式
    for (const auto& term_pair : term_freq) {
        const std::string& term = term_pair.first;
        int freq = term_pair.second;

        writeString(data, term);
        writeInt(data, freq);
    }

    return data;
}

// 反序列化文档
std::shared_ptr<Document> NodeSerializer::deserializeDocument(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return nullptr;
    }

    size_t offset = 0;

    // 读取文档ID
    int doc_id = readInt(data, offset);

    // 读取原始文本
    std::string raw_text = readString(data, offset);

    // 读取位置信息 (MBR)
    int min_size = readInt(data, offset);
    std::vector<double> min_coords;
    for (int i = 0; i < min_size; i++) {
        min_coords.push_back(readDouble(data, offset));
    }

    int max_size = readInt(data, offset);
    std::vector<double> max_coords;
    for (int i = 0; i < max_size; i++) {
        max_coords.push_back(readDouble(data, offset));
    }

    MBR location(min_coords, max_coords);
    // 使用原始文本创建文档
    auto document = std::make_shared<Document>(doc_id, location, raw_text);
    
    // 读取词频信息（跳过，因为文档构造函数已经处理了文本）
    int term_count = readInt(data, offset);
    for (int i = 0; i < term_count; i++) {
        readString(data, offset); // 跳过term
        readInt(data, offset);    // 跳过freq
    }

    return document;
}

// 序列化节点
std::vector<uint8_t> NodeSerializer::serialize(const Node& node) {
    std::vector<uint8_t> data;

    // 写入节点基本信息
    writeInt(data, node.getId());
    writeInt(data, static_cast<int>(node.getType()));
    writeInt(data, node.getLevel());
    writeInt(data, node.getDocumentCount());

    // 写入MBR信息
    const MBR& mbr = node.getMBR();
    const auto& min_coords = mbr.getMin();
    const auto& max_coords = mbr.getMax();

    writeMBR(data, mbr);

    // 写入子节点信息（如果是内部节点）
    if (node.getType() == Node::INTERNAL) {
        const auto& child_nodes = node.getChildNodes();
        writeInt(data, static_cast<int>(child_nodes.size()));

        // 写入所有子节点的ID
        for (const auto& child : child_nodes) {
            writeInt(data, child->getId());
        }
    }
    else {
        writeInt(data, 0); // 叶子节点没有子节点
    }

    // 写入文档列表（如果是叶子节点）
    if (node.getType() == Node::LEAF) {
        const auto& documents = node.getDocuments();
        writeInt(data, static_cast<int>(documents.size()));

        for (const auto& doc : documents) {
            auto doc_data = serializeDocument(*doc);
            writeInt(data, static_cast<int>(doc_data.size()));
            data.insert(data.end(), doc_data.begin(), doc_data.end());
        }
    }
    else {
        writeInt(data, 0); // 内部节点没有文档
    }

    // 写入文档摘要信息
    const auto& df = node.getDF();
    const auto& tf_max = node.getTFMax();

    // 写入文档频率信息
    writeInt(data, static_cast<int>(df.size()));
    for (const auto& df_pair : df) {
        const std::string& term = df_pair.first;
        int freq = df_pair.second;
        writeString(data, term);
        writeInt(data, freq);
    }

    // 写入最大词频信息
    writeInt(data, static_cast<int>(tf_max.size()));
    for (const auto& tf_pair : tf_max) {
        const std::string& term = tf_pair.first;
        int max_freq = tf_pair.second;
        writeString(data, term);
        writeInt(data, max_freq);
    }

    // 写入子节点位置映射信息
    const auto& child_position_map = node.getChildPositionMap();
    writeInt(data, static_cast<int>(child_position_map.size()));
    for (const auto& pos_pair : child_position_map) {
        int child_id = pos_pair.first;
        int path = pos_pair.second;
        writeInt(data, child_id);
        writeInt(data, path);
    }

    // 写入子节点MBR信息（只在内部节点中存储）
    if (node.getType() == Node::INTERNAL) {
        const auto& child_mbr_map = node.getChildMBRMap();  
        writeInt(data, static_cast<int>(child_mbr_map.size()));
        for (const auto& mbr_pair : child_mbr_map) {
            int child_id = mbr_pair.first;
            const MBR& child_mbr = mbr_pair.second;
            writeInt(data, child_id);
            writeMBR(data, child_mbr);
        }
    }
    else {
        writeInt(data, 0); // 叶子节点没有子节点MBR信息
    }

    // 写入子节点文本上界信息（只在内部节点中存储）
    if (node.getType() == Node::INTERNAL) {
        const auto& child_text_bounds = node.getChildTextUpperBounds();
        writeInt(data, static_cast<int>(child_text_bounds.size()));
        for (const auto& bound_pair : child_text_bounds) {
            int child_id = bound_pair.first;
            double upper_bound = bound_pair.second;
            writeInt(data, child_id);
            writeDouble(data, upper_bound);
        }
    }
    else {
        writeInt(data, 0); // 叶子节点没有子节点文本上界信息
    }

    // 写入子节点关键词信息（只在内部节点中存储）
    if (node.getType() == Node::INTERNAL) {
        const auto& child_keywords_map = node.getChildKeywordsMap();
        writeInt(data, static_cast<int>(child_keywords_map.size()));
        for (const auto& keyword_pair : child_keywords_map) {
            int child_id = keyword_pair.first;
            const auto& keywords = keyword_pair.second;

            writeInt(data, child_id);
            writeInt(data, static_cast<int>(keywords.size()));

            for (const auto& keyword : keywords) {
                writeString(data, keyword);
            }
        }
    }
    else {
        writeInt(data, 0); // 叶子节点没有子节点关键词信息
    }

    // 更新版本号到7，表示包含所有子节点优化信息
    writeInt(data, 7); // 版本号升级到7

    return data;
}

// 反序列化节点
std::shared_ptr<Node> NodeSerializer::deserialize(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return nullptr;
    }

    size_t offset = 0;

    // 读取节点基本信息
    int node_id = readInt(data, offset);
    Node::Type node_type = static_cast<Node::Type>(readInt(data, offset));
    int level = readInt(data, offset);
    int doc_count = readInt(data, offset);

    // 读取MBR信息
    int min_size = readInt(data, offset);
    std::vector<double> min_coords;
    for (int i = 0; i < min_size; i++) {
        min_coords.push_back(readDouble(data, offset));
    }

    int max_size = readInt(data, offset);
    std::vector<double> max_coords;
    for (int i = 0; i < max_size; i++) {
        max_coords.push_back(readDouble(data, offset));
    }

    MBR mbr(min_coords, max_coords);
    auto node = std::make_shared<Node>(node_id, node_type, level, mbr);

    // 读取子节点信息
    int child_count = readInt(data, offset);
    std::vector<int> child_ids;
    for (int i = 0; i < child_count; i++) {
        child_ids.push_back(readInt(data, offset));
    }

    // 读取文档列表或子节点ID（根据节点类型）
    if (node_type == Node::LEAF) {
        int document_count = readInt(data, offset);
        for (int i = 0; i < document_count; i++) {
            int doc_data_size = readInt(data, offset);
            if (doc_data_size <= 0 || offset + doc_data_size > data.size()) {
                // 跳过无效的文档数据
                break;
            }

            std::vector<uint8_t> doc_data(data.begin() + offset,
                data.begin() + offset + doc_data_size);
            offset += doc_data_size;

            auto document = deserializeDocument(doc_data);
            if (document) {
                node->addDocument(document);
            }
        }
    }
    else {
        // 内部节点：跳过文档数量（应该为0）
        readInt(data, offset);

        // 为内部节点创建空的子节点对象（只有ID）
        for (int child_id : child_ids) {
            // 创建一个只有ID的子节点占位符
            MBR child_mbr(std::vector<double>(min_size, 0.0), std::vector<double>(max_size, 0.0));
            auto child_node = std::make_shared<Node>(child_id, Node::LEAF, level - 1, child_mbr);
            node->addChild(child_node);
        }
    }

    // 读取文档频率信息
    int df_count = readInt(data, offset);
    std::unordered_map<std::string, int> df_map;
    for (int i = 0; i < df_count; i++) {
        std::string term = readString(data, offset);
        int freq = readInt(data, offset);
        df_map[term] = freq;
    }

    // 读取最大词频信息
    int tf_max_count = readInt(data, offset);
    std::unordered_map<std::string, int> tf_max_map;
    for (int i = 0; i < tf_max_count; i++) {
        std::string term = readString(data, offset);
        int max_freq = readInt(data, offset);
        tf_max_map[term] = max_freq;
    }

    // 读取子节点位置映射信息
    int position_map_count = 0;
    if (offset < data.size()) {
        position_map_count = readInt(data, offset);
    }

    std::unordered_map<int, int> child_position_map;
    for (int i = 0; i < position_map_count; i++) {
        if (offset >= data.size()) break;
        int child_id = readInt(data, offset);
        int path = readInt(data, offset);
        child_position_map[child_id] = path;
    }

    // 设置子节点位置映射
    node->setChildPositionMap(child_position_map);

    // 读取子节点MBR信息
    std::unordered_map<int, MBR> child_mbr_map;
    if (offset < data.size()) {
        int child_mbr_count = readInt(data, offset);
        for (int i = 0; i < child_mbr_count; i++) {
            if (offset >= data.size()) break;
            int child_id = readInt(data, offset);
            MBR child_mbr = readMBR(data, offset);
            child_mbr_map[child_id] = child_mbr;
        }
    }

    // 设置子节点MBR信息
    for (const auto& mbr_pair : child_mbr_map) {
        node->setChildMBR(mbr_pair.first, mbr_pair.second);
    }

    // 读取子节点文本上界信息
    std::unordered_map<int, double> child_text_bounds;
    if (offset < data.size()) {
        int child_bounds_count = readInt(data, offset);
        for (int i = 0; i < child_bounds_count; i++) {
            if (offset >= data.size()) break;
            int child_id = readInt(data, offset);
            double upper_bound = readDouble(data, offset);
            child_text_bounds[child_id] = upper_bound;
        }
    }

    // 设置子节点文本上界信息
    for (const auto& bound_pair : child_text_bounds) {
        node->setChildTextUpperBound(bound_pair.first, bound_pair.second);
    }

    // 读取子节点关键词信息
    std::unordered_map<int, std::unordered_set<std::string>> child_keywords_map;
    if (offset < data.size()) {
        int child_keywords_count = readInt(data, offset);
        for (int i = 0; i < child_keywords_count; i++) {
            if (offset >= data.size()) break;
            int child_id = readInt(data, offset);
            int keyword_count = readInt(data, offset);

            std::unordered_set<std::string> keywords;
            for (int j = 0; j < keyword_count; j++) {
                if (offset >= data.size()) break;
                std::string keyword = readString(data, offset);
                keywords.insert(keyword);
            }

            child_keywords_map[child_id] = keywords;
        }
    }

    // 设置子节点关键词信息
    for (const auto& keyword_pair : child_keywords_map) {
        node->setChildKeywords(keyword_pair.first, keyword_pair.second);
    }

    // 读取版本号
    int version = 1;
    if (offset < data.size()) {
        version = readInt(data, offset);
    }

    // 手动设置文档摘要信息
    node->setDocumentSummary(df_map, tf_max_map);

    return node;
}