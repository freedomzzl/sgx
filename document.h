#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "MBR.h"
#include <string>
#include <unordered_map>

class Document {
private:
    int doc_id;
    MBR location;
    std::string raw_text;
    std::unordered_map<std::string, int> term_freq;

    void processText(const std::string& text);
    void toLower(std::string& str);
    void removePunctuation(std::string& str);

public:
    Document(int id, const MBR& loc, const std::string& text);
    
    // Getters
    int getId() const { return doc_id; }
    const MBR& getLocation() const { return location; }
    const std::string& getText() const { return raw_text; }
    const std::unordered_map<std::string, int>& getTermFreq() const { return term_freq; }
    
    int getTermFrequency(const std::string& term) const;
    void addTerm(const std::string& term, int freq = 1);
};

#endif