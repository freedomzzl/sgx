#include "InvertedIndex.h"
#include "Vocabulary.h"  
#include <algorithm>
#include <cstdio>  
#include <cstring>

InvertedIndex::InvertedIndex() : total_documents(0) {}

void InvertedIndex::addDocument(int doc_id, Vector& vector) {
    total_documents++;

    for (const auto& pair : vector.getTermWeights()) {
        int term_id = pair.first;
        double weight = pair.second;

        // 添加到倒排列表
        auto& postings = index[term_id];
        postings.emplace_back(doc_id, weight);
    }
}

std::vector<Posting> InvertedIndex::getPostings(int term_id) const {
    auto it = index.find(term_id);
    if (it != index.end()) {
        return it->second;
    }
    return {};
}

std::vector<int> InvertedIndex::getDocumentsWithTerm(int term_id) const {
    std::vector<int> doc_ids;
    auto it = index.find(term_id);
    if (it != index.end()) {
        for (const auto& posting : it->second) {
            doc_ids.push_back(posting.doc_id);
        }
    }
    return doc_ids;
}

int InvertedIndex::getDocumentFrequency(int term_id) const {
    auto it = index.find(term_id);
    if (it != index.end()) {
        return it->second.size();
    }
    return 0;
}

void InvertedIndex::clear() {
    index.clear();
    total_documents = 0;
}

void InvertedIndex::merge(const InvertedIndex& other) {
    for (const auto& pair : other.index) {
        int term_id = pair.first;
        const auto& other_postings = pair.second;

        auto& this_postings = index[term_id];
        this_postings.insert(this_postings.end(), other_postings.begin(), other_postings.end());
    }
    total_documents += other.total_documents;
}

size_t InvertedIndex::getStringLength(Vocabulary& vocab) const {
    // 估算字符串长度
    size_t length = 100; // 基础长度
    
    int term_count = 0;
    for (const auto& pair : index) {
        if (term_count++ >= 3) break;
        
        int term_id = pair.first;
        const auto& postings = pair.second;
        
        // 估算每个术语行的长度
        length += 50; // 术语名和基础信息
        length += postings.size() * 20; // 每个posting的估算长度
    }
    
    return length;
}

int InvertedIndex::toString(Vocabulary& vocab, char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    int written = 0;
    
    // 写入基础信息
    written += snprintf(buffer + written, buffer_size - written, 
                       "InvertedIndex[total_docs=%d, terms=%zu]", 
                       total_documents, index.size());
    
    if (written >= buffer_size) {
        return written;
    }
    
    // 显示前3个术语的倒排列表
    int term_count = 0;
    for (const auto& pair : index) {
        if (term_count++ >= 3) break;
        
        int term_id = pair.first;
        const auto& postings = pair.second;
        
        // 获取术语名
        const char* term_name = "unknown";
        char term_buffer[64];
        try {
            std::string term_str = vocab.getTerm(term_id);
            snprintf(term_buffer, sizeof(term_buffer), "%s", term_str.c_str());
            term_name = term_buffer;
        }
        catch (const std::out_of_range&) {
            snprintf(term_buffer, sizeof(term_buffer), "unknown(%d)", term_id);
            term_name = term_buffer;
        }
        
        // 写入术语信息
        written += snprintf(buffer + written, buffer_size - written, 
                           "\n  %s (df=%zu): [", term_name, postings.size());
        
        if (written >= buffer_size) {
            return written;
        }
        
        // 写入前3个posting
        int posting_count = 0;
        for (const auto& posting : postings) {
            if (posting_count++ >= 3) break;
            if (posting_count > 1) {
                written += snprintf(buffer + written, buffer_size - written, ", ");
                if (written >= buffer_size) return written;
            }
            
            written += snprintf(buffer + written, buffer_size - written, 
                               "%d:%.3f", posting.doc_id, posting.weight);
            if (written >= buffer_size) return written;
        }
        
        if (postings.size() > 3) {
            written += snprintf(buffer + written, buffer_size - written, ", ...");
            if (written >= buffer_size) return written;
        }
        
        written += snprintf(buffer + written, buffer_size - written, "]");
        if (written >= buffer_size) return written;
    }
    
    if (index.size() > 3) {
        written += snprintf(buffer + written, buffer_size - written, "\n  ...");
    }
    
    // 确保字符串以null结尾
    if (written < buffer_size) {
        buffer[written] = '\0';
    } else {
        buffer[buffer_size - 1] = '\0';
    }
    
    return written;
}