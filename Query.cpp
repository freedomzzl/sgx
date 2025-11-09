#include "Query.h"
#include <cstdio>
#include <cstring>

Query::Query(const std::vector<std::string>& kw, const MBR& scope, int top_k, double a)
    : keywords(kw), spatial_scope(scope), k(top_k), alpha(a) {
    
    if (k <= 0) {
        k = 10; // 使用默认值
    }
    if (alpha < 0 || alpha > 1) {
        alpha = 0.5; // 使用默认值
    }
}

size_t Query::getStringLength() const {
    // 估算字符串长度
    size_t length = 50; // 基础长度
    
    // 关键词的长度
    for (const auto& keyword : keywords) {
        length += keyword.length() + 2; // 关键词长度 + 分隔符
    }
    
    // 空间范围字符串长度
    length += spatial_scope.getStringLength();
    
    // k 和 alpha 的长度
    length += 30;
    
    return length;
}

int Query::toString(char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入 "Query[keywords=("
    written += snprintf(buffer + written, buffer_size - written, "Query[keywords=(");
    if (written >= buffer_size) return written;
    
    // 写入关键词
    for (size_t i = 0; i < keywords.size(); i++) {
        if (i > 0) {
            written += snprintf(buffer + written, buffer_size - written, ", ");
            if (written >= buffer_size) return written;
        }
        
        written += snprintf(buffer + written, buffer_size - written, "%s", keywords[i].c_str());
        if (written >= buffer_size) return written;
    }
    
    // 写入 "), scope="
    written += snprintf(buffer + written, buffer_size - written, "), scope=");
    if (written >= buffer_size) return written;
    
    // 写入空间范围
    size_t mbr_buffer_size = buffer_size - written;
    if (mbr_buffer_size > 0) {
        int mbr_written = spatial_scope.toString(buffer + written, mbr_buffer_size);
        if (mbr_written > 0) {
            written += mbr_written;
        }
    }
    
    if (written >= buffer_size) return written;
    
    // 写入 k 和 alpha
    written += snprintf(buffer + written, buffer_size - written, 
                       ", k=%d, alpha=%.2f]", k, alpha);
    
    // 确保字符串以null结尾
    if (written < buffer_size) {
        buffer[written] = '\0';
    } else {
        buffer[buffer_size - 1] = '\0';
    }
    
    return written;
}