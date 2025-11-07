#include "document.h"
#include <cctype>
#include <cstring>

Document::Document(int id, const MBR& loc, const std::string& text)
    : doc_id(id), location(loc), raw_text(text) {
    if (!text.empty()) {
        processText(text);
    }
}

void Document::toLower(std::string& str) {
    for (size_t i = 0; i < str.length(); i++) {
        str[i] = std::tolower(static_cast<unsigned char>(str[i]));
    }
}

void Document::removePunctuation(std::string& str) {
    std::string result;
    for (char c : str) {
        if (!std::ispunct(static_cast<unsigned char>(c))) {
            result += c;
        }
    }
    str = result;
}

void Document::processText(const std::string& text) {
    term_freq.clear();

    std::string current_word;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current_word.empty()) {
                // 处理单词：转小写、去标点
                toLower(current_word);
                removePunctuation(current_word);
                
                if (!current_word.empty()) {
                    addTerm(current_word);
                }
                current_word.clear();
            }
        } else {
            current_word += c;
        }
    }
    
    // 处理最后一个单词
    if (!current_word.empty()) {
        toLower(current_word);
        removePunctuation(current_word);
        if (!current_word.empty()) {
            addTerm(current_word);
        }
    }
}

void Document::addTerm(const std::string& term, int freq) {
    if (term.empty()) return;
    
    auto it = term_freq.find(term);
    if (it != term_freq.end()) {
        it->second += freq;
    } else {
        term_freq[term] = freq;
    }
}

int Document::getTermFrequency(const std::string& term) const {
    auto it = term_freq.find(term);
    return (it != term_freq.end()) ? it->second : 0;
}