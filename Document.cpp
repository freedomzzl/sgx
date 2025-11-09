#include "Document.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

// 手动实现文本分词
namespace {
    void split_text(const std::string& text, std::vector<std::string>& words) {
        size_t start = 0;
        size_t end = 0;
        
        while (end < text.length()) {
            // 跳过空白字符
            while (start < text.length() && std::isspace(text[start])) {
                start++;
            }
            
            if (start >= text.length()) break;
            
            // 找到单词结束位置
            end = start;
            while (end < text.length() && !std::isspace(text[end])) {
                end++;
            }
            
            // 提取单词
            if (end > start) {
                words.push_back(text.substr(start, end - start));
            }
            
            start = end + 1;
        }
    }
    
    void clean_word(std::string& word) {
        // 转换为小写
        for (char& c : word) {
            c = std::tolower(c);
        }
        
        // 移除标点符号
        word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
    }
}

Document::Document(int id, const MBR& loc, const std::string& text)
    : doc_id(id), location(loc), raw_text(text) {
    if (!text.empty()) {
        processText(text);
    }
}

void Document::processText(const std::string& text) {
    term_freq.clear();

    std::vector<std::string> words;
    split_text(text, words);

    for (const auto& word : words) {
        std::string cleaned_word = word;
        clean_word(cleaned_word);

        if (!cleaned_word.empty()) {
            addTerm(cleaned_word);
        }
    }
}

void Document::addTerm(const std::string& term, int freq) {
    if (term_freq.find(term) == term_freq.end()) {
        term_freq[term] = freq;
    }
    else {
        term_freq[term] += freq;
    }
}

int Document::getTermFrequency(const std::string& term) const {
    auto it = term_freq.find(term);
    return (it != term_freq.end()) ? it->second : 0;
}

size_t Document::getStringLength() const {
    // 估算字符串长度
    size_t length = 50; // 基础长度
    
    // 位置信息的长度
    length += location.getStringLength();
    
    // 术语的长度
    int count = 0;
    for (const auto& pair : term_freq) {
        if (count++ >= 5) break;
        length += pair.first.length() + 10; // 术语名 + 频率 + 分隔符
    }
    
    return length;
}

int Document::toString(char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入基础信息
    written += snprintf(buffer + written, buffer_size - written, 
                       "Document[id=%d, location=", doc_id);
    if (written >= buffer_size) return written;
    
    // 写入位置信息
    size_t mbr_buffer_size = buffer_size - written;
    if (mbr_buffer_size > 0) {
        int mbr_written = location.toString(buffer + written, mbr_buffer_size);
        if (mbr_written > 0) {
            written += mbr_written;
        }
    }
    
    if (written >= buffer_size) return written;
    
    // 写入术语数量
    written += snprintf(buffer + written, buffer_size - written, 
                       ", terms=%zu] {", term_freq.size());
    if (written >= buffer_size) return written;
    
    // 显示前5个词频最高的术语
    int count = 0;
    for (const auto& pair : term_freq) {
        if (count++ >= 5) break;
        
        if (count > 1) {
            written += snprintf(buffer + written, buffer_size - written, ", ");
            if (written >= buffer_size) return written;
        }
        
        written += snprintf(buffer + written, buffer_size - written, 
                           "%s:%d", pair.first.c_str(), pair.second);
        if (written >= buffer_size) return written;
    }
    
    if (term_freq.size() > 5) {
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