#ifndef QUERY_H
#define QUERY_H

#include <vector>
#include <string>
#include "MBR.h"

class Query {
private:
    std::vector<std::string> keywords;
    MBR spatial_scope;
    int k;
    double alpha; // 文本 vs 空间权重

public:
    Query(const std::vector<std::string>& kw, const MBR& scope, int top_k = 10, double a = 0.5);

    // Getter
    const std::vector<std::string>& getKeywords() const { return keywords; }
    const MBR& getSpatialScope() const { return spatial_scope; }
    int getK() const { return k; }
    double getAlpha() const { return alpha; }

    // 使用缓冲区输出字符串
    int toString(char* buffer, size_t buffer_size) const;
    
    // 获取字符串表示的长度
    size_t getStringLength() const;
};

#endif