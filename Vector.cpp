#include "Vector.h"
#include "Vocabulary.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>  
#include <cstring>

Vector::Vector(int id) : doc_id(id) {}

void Vector::addTerm(int term_id, double weight) {
    if (term_weights.find(term_id) == term_weights.end()) {
        term_weights[term_id] = weight;
    }
    else {
        term_weights[term_id] += weight;
    }
}

void Vector::setTermWeight(int term_id, double weight) {
    term_weights[term_id] = weight;
}

double Vector::getTermWeight(int term_id) const {
    auto it = term_weights.find(term_id);
    return (it != term_weights.end()) ? it->second : 0.0;
}

void Vector::aggregate(const Vector& other) {
    for (const auto& pair : other.term_weights) {
        int term_id = pair.first;
        double weight = pair.second;

        // 对于聚合，取最大权重（用于节点摘要的TF_max）
        if (term_weights.find(term_id) == term_weights.end() || term_weights[term_id] < weight) {
            term_weights[term_id] = weight;
        }
    }
}

double Vector::dotProduct(const Vector& other) const {
    double result = 0.0;
    for (const auto& pair : term_weights) {
        int term_id = pair.first;
        result += pair.second * other.getTermWeight(term_id);
    }
    return result;
}

double Vector::magnitude() const {
    double sum = 0.0;
    for (const auto& pair : term_weights) {
        sum += pair.second * pair.second;
    }
    return std::sqrt(sum);
}

double Vector::cosineSimilarity(const Vector& other) const {
    double dot = dotProduct(other);
    double mag1 = magnitude();
    double mag2 = other.magnitude();

    if (mag1 == 0 || mag2 == 0) {
        return 0.0;
    }

    return dot / (mag1 * mag2);
}

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

void Vector::vectorize(Vector& vector, const std::string& text, Vocabulary& vocab) {
    std::vector<std::string> words;
    split_text(text, words);
    
    std::unordered_map<std::string, int> term_freq;

    // 统计词频
    for (const auto& word : words) {
        std::string cleaned_word = word;
        clean_word(cleaned_word);
        
        if (!cleaned_word.empty()) {
            term_freq[cleaned_word]++;
        }
    }

    // 转换为向量表示
    for (const auto& pair : term_freq) {
        const std::string& term = pair.first;
        int tf = pair.second;

        int term_id = vocab.addTerm(term);
        if (term_id != -1) {
            // 使用原始TF值，后续可以应用TF-IDF
            vector.addTerm(term_id, static_cast<double>(tf));
        }
    }
}

double Vector::computeTFIDFWeight(int tf, int df, int total_docs) {
    if (tf == 0 || df == 0 || total_docs == 0) {
        return 0.0;
    }

    double tf_component = std::log(1 + tf); // 对数TF
    double idf_component = std::log(static_cast<double>(total_docs) / df);

    return tf_component * idf_component;
}

size_t Vector::getStringLength(const Vocabulary& vocab) const {
    // 估算字符串长度
    size_t length = 50; // 基础长度
    
    int count = 0;
    for (const auto& pair : term_weights) {
        if (count++ >= 5) break;
        
        // 估算每个术语的长度
        length += 30; // 术语名和权重的估算长度
    }
    
    return length;
}

int Vector::toString(const Vocabulary& vocab, char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入基础信息
    written += snprintf(buffer + written, buffer_size - written, 
                       "Vector[doc_id=%d, terms=%zu] {", 
                       doc_id, term_weights.size());
    
    if (written >= buffer_size) {
        return written;
    }
    
    // 显示前5个术语
    int count = 0;
    for (const auto& pair : term_weights) {
        if (count++ >= 5) break;
        
        if (count > 1) {
            written += snprintf(buffer + written, buffer_size - written, ", ");
            if (written >= buffer_size) return written;
        }
        
        // 直接获取术语名，不处理异常
        std::string term_str = vocab.getTerm(pair.first);
        written += snprintf(buffer + written, buffer_size - written, 
                           "%s:%.3f", term_str.c_str(), pair.second);
        if (written >= buffer_size) return written;
    }
    
    if (term_weights.size() > 5) {
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