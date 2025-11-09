#include "Vocabulary.h"
#include <cstdio>  
#include <cstring>

Vocabulary::Vocabulary() : next_id(0) {}

int Vocabulary::addTerm(const std::string& term) {
    if (term.empty()) {
        return -1;
    }

    auto it = term_to_id.find(term);
    if (it != term_to_id.end()) {
        return it->second;
    }

    int term_id = next_id++;
    term_to_id[term] = term_id;
    id_to_term.push_back(term);
    return term_id;
}

int Vocabulary::getTermId(const std::string& term) const {
    auto it = term_to_id.find(term);
    return (it != term_to_id.end()) ? it->second : -1;
}

std::string Vocabulary::getTerm(int term_id) const {
    
    if (term_id < 0 || term_id >= static_cast<int>(id_to_term.size())) {
        return "invalid_term_id";
    }
    return id_to_term[term_id];
}

void Vocabulary::clear() {
    term_to_id.clear();
    id_to_term.clear();
    next_id = 0;
}

size_t Vocabulary::getStringLength() const {
    // 估算字符串长度
    size_t length = 50; // 基础长度
    
    int count = 0;
    for (const auto& term : id_to_term) {
        if (count++ >= 10) break;
        length += term.length() + 10; // 术语长度 + ":ID" 的估算长度
    }
    
    return length;
}

int Vocabulary::toString(char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入基础信息
    written += snprintf(buffer + written, buffer_size - written, 
                       "Vocabulary[size=%zu] {", size());
    
    if (written >= buffer_size) {
        return written;
    }
    
    // 显示前10个术语
    int count = 0;
    for (const auto& term : id_to_term) {
        if (count++ >= 10) break;
        
        if (count > 1) {
            written += snprintf(buffer + written, buffer_size - written, ", ");
            if (written >= buffer_size) return written;
        }
        
        int term_id = term_to_id.at(term);
        written += snprintf(buffer + written, buffer_size - written, 
                           "%s:%d", term.c_str(), term_id);
        if (written >= buffer_size) return written;
    }
    
    if (size() > 10) {
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