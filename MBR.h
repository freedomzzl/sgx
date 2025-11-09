#ifndef MBR_H
#define MBR_H

#include <vector>
#include <string>
#include <cmath>

class MBR {
private:
    std::vector<double> min_coords;
    std::vector<double> max_coords;
    mutable double cached_area;    // 缓存面积
    mutable bool area_calculated;  // 面积计算标志

public:
    /**
     * @brief 默认构造函数
     * 创建一个空的MBR，需要后续初始化
     */
    MBR() : cached_area(-1.0), area_calculated(false) {
        // 创建空的坐标向量
        min_coords = std::vector<double>{ 0.0, 0.0 };  // 默认2维
        max_coords = std::vector<double>{ 0.0, 0.0 };
    }
    MBR(const std::vector<double>& min, const std::vector<double>& max);


    double area() const;
    void expand(const MBR& other);
    bool contains(const MBR& other) const;
    bool overlaps(const MBR& other) const;
    double minDistance(const std::vector<double>& point, int p_norm = 2) const;

    // Getter/Setter
    const std::vector<double>& getMin() const { return min_coords; }
    const std::vector<double>& getMax() const { return max_coords; }

    // 使用缓冲区输出字符串
    int toString(char* buffer, size_t buffer_size) const;
    
    //获取字符串表示的长度
    size_t getStringLength() const;

    std::vector<double> getCenter() const {
        std::vector<double> center;
        for (size_t i = 0; i < min_coords.size(); i++) {
            center.push_back((min_coords[i] + max_coords[i]) / 2.0);
        }
        return center;
    }

    double getDiagonalLength() const {
        double sum = 0.0;
        for (size_t i = 0; i < min_coords.size(); i++) {
            sum += std::pow(max_coords[i] - min_coords[i], 2);
        }
        return std::sqrt(sum);
    }
};

#endif