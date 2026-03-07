#include "SGXEnclaveWrapper.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include"param.h"

void testWithQueryFileSGX(const std::string& query_filename, bool show_details = true) {
   
    // 初始化 SGX Enclave
    SGXEnclaveWrapper enclave;
    if (!enclave.initializeEnclave()) {
        std::cerr << "Failed to initialize SGX enclave" << std::endl;
        return;
    }

    // 初始化 IRTree
    if (!enclave.initializeIRTree(2, 2, 5)) {
        std::cerr << "Failed to initialize IRTree in SGX" << std::endl;
        return;
    }

    // 批量插入数据
    if (!enclave.bulkInsertFromFile(dataname)) {
        std::cerr << "Failed to bulk insert data" << std::endl;
        return;
    }

    std::ifstream query_file(query_filename);
    if (!query_file.is_open()) {
        std::cerr << "Error: Cannot open query file " << query_filename << std::endl;
        return;
    }
    std::vector<std::chrono::nanoseconds> query_times;
    std::string line;
    int query_count = 0;

    while (std::getline(query_file, line)) {
        if (line.empty()) continue;

        //支持多个关键词
        std::istringstream iss(line);
        std::vector<std::string> parts;
        std::string token;
    
        // 读取所有tokens
        while (iss >> token) {
            parts.push_back(token);
        }
    
        // 至少需要3个部分：至少1个关键词 + 2个坐标
        if (parts.size() < 3) {
            std::cerr << "Invalid query format: " << line << std::endl;
            continue;
        }
    
        // 最后两个是坐标
        double x, y;
        try {
            x = std::stod(parts[parts.size() - 2]);
            y = std::stod(parts[parts.size() - 1]);
        } catch (...) {
            std::cerr << "Invalid coordinates in query: " << line << std::endl;
            continue;
        }
    
        // 前面所有部分都是关键词，用空格连接
        std::string text;
        for (size_t i = 0; i < parts.size() - 2; i++) {
            if (i > 0) text += " ";
            text += parts[i];
        }
    
        query_count++;

        // 创建搜索范围
        double epsilon = 2000;
    
        auto start_time = std::chrono::high_resolution_clock::now();
       
        // 在 SGX 中执行搜索（使用连接后的文本）
        auto results = enclave.search(text, x - epsilon, y - epsilon, x + epsilon, y + epsilon, k, 0.5);
    
        auto end_time = std::chrono::high_resolution_clock::now();
        auto query_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        query_times.push_back(query_time);

        if (show_details) {
            std::cout << "\n--- Query " << query_count << " ---" << std::endl;
            std::cout << "Keywords: '" << text << "'" << std::endl;
            std::cout << "Time: " << std::fixed << std::setprecision(3) 
                  << (query_time.count() / 1000000.0) << " ms" << std::endl;
            std::cout << "Results: " << results.size() << " documents" << std::endl;
        } else {
            // 简洁模式只显示基本信息
            std::cout << "Query " << query_count << ": " << results.size() 
                  << " results in " << std::fixed << std::setprecision(3) 
                  << (query_time.count() / 1000000.0) << " ms" << std::endl;
        }
    }

    query_file.close();

    // 性能统计
    if (!query_times.empty()) {
        std::chrono::nanoseconds total_time = std::chrono::nanoseconds::zero();
        for (const auto& time : query_times) {
            total_time += time;
        }

        double total_seconds = total_time.count() / 1000000000.0;
        double avg_seconds = total_seconds / query_times.size();

        std::cout << "\n" << std::string(50, '=') << std::endl;
        std::cout << "SGX IRTree PERFORMANCE SUMMARY" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        std::cout << "Average query time: " << std::fixed << std::setprecision(3) << avg_seconds << " seconds" << std::endl;
        std::cout << "Queries per second: " << std::fixed << std::setprecision(1) << (1.0 / avg_seconds) << " qps" << std::endl;

        // 重置输出格式
        std::cout.unsetf(std::ios_base::floatfield);
    }

}

int main() {
    try {
       
        // 测试 SGX 版本
        testWithQueryFileSGX(queryname, true);

        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}