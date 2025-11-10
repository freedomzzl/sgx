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

