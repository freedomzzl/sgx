#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <string>
#include <unordered_map>
#include "MBR.h"

class Document {
private:
    int doc_id;
    MBR location;
    std::unordered_map<std::string, int> term_freq; // ´ÊÆµÍ³¼Æ
    std::string raw_text;

public:
    Document(int id, const MBR& loc, const std::string& text = "");

    // ÎÄ±¾´¦Àí
    void processText(const std::string& text);
    void addTerm(const std::string& term, int freq = 1);

    // Getter
    int getId() const { return doc_id; }
    const MBR& getLocation() const { return location; }
    const std::unordered_map<std::string, int>& getTermFreq() const { return term_freq; }
    int getTermFrequency(const std::string& term) const;
    const std::string& getText() const { return raw_text; }
};

#endif