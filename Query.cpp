#include "Query.h"
#include<sstream>

Query::Query(const std::vector<std::string>& kw, const MBR& scope, int top_k, double a)
    : keywords(kw), spatial_scope(scope), k(top_k), alpha(a) {
    if (k <= 0) {
        throw std::invalid_argument("k must be positive");
    }
    if (alpha < 0 || alpha > 1) {
        throw std::invalid_argument("alpha must be between 0 and 1");
    }
}