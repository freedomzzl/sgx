#ifndef MBR_H
#define MBR_H

#include <vector>

class MBR {
public:
    // 默认构造函数
    MBR()
        : min_coords(), max_coords(), cached_area(-1.0), area_calculated(false) {}

    MBR(const std::vector<double>& min, const std::vector<double>& max);

    double area() const;
    void expand(const MBR& other);
    bool contains(const MBR& other) const;
    bool overlaps(const MBR& other) const;
    double minDistance(const std::vector<double>& point, int p_norm = 2) const;

    // 获取器方法
    const std::vector<double>& getMinCoords() const { return min_coords; }
    const std::vector<double>& getMaxCoords() const { return max_coords; }
    size_t getDimensions() const { return min_coords.size(); }

private:
    std::vector<double> min_coords;
    std::vector<double> max_coords;
    mutable double cached_area;
    mutable bool area_calculated;
};

#endif
