#include "MBR.h"
#include <algorithm>
#include <cmath>


// SGX环境下的简单断言 - 静默失败
#define MBR_ASSERT(condition) \
    if (!(condition)) { \
        return; \
    }

MBR::MBR(const std::vector<double>& min, const std::vector<double>& max)
    : min_coords(min), max_coords(max), cached_area(-1.0), area_calculated(false) {
    MBR_ASSERT(min.size() == max.size());
    
    for (size_t i = 0; i < min.size(); i++) {
        MBR_ASSERT(min[i] <= max[i]);
    }
}

double MBR::area() const {
    if (!area_calculated) {
        cached_area = 1.0;
        for (size_t i = 0; i < min_coords.size(); i++) {
            cached_area *= (max_coords[i] - min_coords[i]);
        }
        area_calculated = true;
    }
    return cached_area;
}

void MBR::expand(const MBR& other) {
    MBR_ASSERT(other.min_coords.size() == min_coords.size());

    for (size_t i = 0; i < min_coords.size(); i++) {
        min_coords[i] = std::min(min_coords[i], other.min_coords[i]);
        max_coords[i] = std::max(max_coords[i], other.max_coords[i]);
    }

    cached_area = -1.0;
    area_calculated = false;
}

bool MBR::contains(const MBR& other) const {
    if (other.min_coords.size() != min_coords.size()) return false;

    for (size_t i = 0; i < min_coords.size(); i++) {
        if (other.min_coords[i] < min_coords[i] || other.max_coords[i] > max_coords[i]) {
            return false;
        }
    }
    return true;
}

bool MBR::overlaps(const MBR& other) const {
    if (other.min_coords.size() != min_coords.size()) return false;

    for (size_t i = 0; i < min_coords.size(); i++) {
        if (other.max_coords[i] < min_coords[i] || other.min_coords[i] > max_coords[i]) {
            return false;
        }
    }
    return true;
}

double MBR::minDistance(const std::vector<double>& point, int p_norm) const {
    if (point.size() != min_coords.size()) {
        // 返回一个大的安全值
        return 1e308;  // 使用数值而不是std::numeric_limits
    }

    double distance = 0.0;

    if (p_norm == 2) {
        for (size_t i = 0; i < point.size(); i++) {
            if (point[i] < min_coords[i]) {
                double diff = min_coords[i] - point[i];
                distance += diff * diff;
            }
            else if (point[i] > max_coords[i]) {
                double diff = point[i] - max_coords[i];
                distance += diff * diff;
            }
        }
        return std::sqrt(distance);
    }
    else {
        for (size_t i = 0; i < point.size(); i++) {
            if (point[i] < min_coords[i]) {
                distance += min_coords[i] - point[i];
            }
            else if (point[i] > max_coords[i]) {
                distance += point[i] - max_coords[i];
            }
        }
        return distance;
    }
}