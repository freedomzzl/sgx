#include "MBR.h"
#include <sstream>
#include <algorithm>


MBR::MBR(const std::vector<double>& min, const std::vector<double>& max)
    : min_coords(min), max_coords(max), cached_area(-1.0), area_calculated(false) {
    if (min.size() != max.size()) {
        throw std::invalid_argument("Min and max coordinates must have same dimension");
    }
    for (size_t i = 0; i < min.size(); i++) {
        if (min[i] > max[i]) {
            throw std::invalid_argument("Min coordinate cannot be greater than max coordinate");
        }
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
    if (other.min_coords.size() != min_coords.size()) {
        throw std::invalid_argument("MBR dimensions must match for expansion");
    }

    for (size_t i = 0; i < min_coords.size(); i++) {
        min_coords[i] = std::min(min_coords[i], other.min_coords[i]);
        max_coords[i] = std::max(max_coords[i], other.max_coords[i]);
    }

    // Çå³ý»º´æ
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
        throw std::invalid_argument("Point dimension must match MBR dimension");
    }

    double distance = 0.0;

    if (p_norm == 2) { // Euclidean distance
        for (size_t i = 0; i < point.size(); i++) {
            if (point[i] < min_coords[i]) {
                distance += std::pow(min_coords[i] - point[i], 2);
            }
            else if (point[i] > max_coords[i]) {
                distance += std::pow(point[i] - max_coords[i], 2);
            }
            // else point is within bounds for this dimension, no distance
        }
        return std::sqrt(distance);
    }
    else { // Manhattan distance (p_norm = 1) or other
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
