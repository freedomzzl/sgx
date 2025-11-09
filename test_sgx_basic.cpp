#include "SGXEnclaveWrapper.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

void testWithQueryFileSGX(const std::string& query_filename, bool show_details = true) {
    std::cout << "=== TESTING IRTree IN SGX ENCLAVE ===" << std::endl;
    std::cout << "Query file: " << query_filename << std::endl;

    // 初始化 SGX Enclave
    SGXEnclaveWrapper enclave;
    if (!enclave.initializeEnclave()) {
        std::cerr << "Failed to initialize SGX enclave" << std::endl;
        return;
    }

    // 初始化 IRTree
    std::cout << "Initializing IRTree in SGX..." << std::endl;
    if (!enclave.initializeIRTree(2, 2, 5)) {
        std::cerr << "Failed to initialize IRTree in SGX" << std::endl;
        return;
    }

    // 批量插入数据
    std::cout << "Loading data into SGX IRTree..." << std::endl;
    if (!enclave.bulkInsertFromFile("small_data.txt")) {
        std::cerr << "Failed to bulk insert data" << std::endl;
        return;
    }

    std::cout << "Data loading completed. Starting queries..." << std::endl;

    std::ifstream query_file(query_filename);
    if (!query_file.is_open()) {
        std::cerr << "Error: Cannot open query file " << query_filename << std::endl;
        return;
    }
    std::cout<<"open query file successfully"<<std::endl;
    std::vector<std::chrono::nanoseconds> query_times;
    std::string line;
    int query_count = 0;

    while (std::getline(query_file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string text;
        double x, y;

        // 解析格式: 文本 经度 纬度
        if (iss >> text >> x >> y) {
            query_count++;

            // 创建搜索范围
            double epsilon = 0.01;
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // 在 SGX 中执行搜索
            auto results = enclave.search(text, x - epsilon, y - epsilon, x + epsilon, y + epsilon, 10, 0.5);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto query_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
            query_times.push_back(query_time);

            if (show_details) {
                std::cout << "\n--- Query " << query_count << " ---" << std::endl;
                std::cout << "Keywords: '" << text << "'" << std::endl;
                std::cout << "Location: (" << x << ", " << y << ")" << std::endl;
                std::cout << "Time: " << std::fixed << std::setprecision(3) 
                          << (query_time.count() / 1000000.0) << " ms" << std::endl;
                std::cout << "Results: " << results.size() << " documents" << std::endl;
                
                // 显示前几个结果
                for (size_t i = 0; i < results.size() && i < 3; i++) {
                    std::cout << "  " << (i + 1) << ". Doc " << results[i].first 
                              << " - Score: " << std::fixed << std::setprecision(4) 
                              << results[i].second << std::endl;
                }
            } else {
                // 简洁模式只显示基本信息
                std::cout << "Query " << query_count << ": " << results.size() 
                          << " results in " << std::fixed << std::setprecision(3) 
                          << (query_time.count() / 1000000.0) << " ms" << std::endl;
            }
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
        std::cout << "Total queries executed: " << query_times.size() << std::endl;
        std::cout << "Total execution time: " << std::fixed << std::setprecision(3) << total_seconds << " seconds" << std::endl;
        std::cout << "Average query time: " << std::fixed << std::setprecision(3) << avg_seconds << " seconds" << std::endl;
        std::cout << "Average query latency: " << std::fixed << std::setprecision(1) << (avg_seconds * 1000) << " ms" << std::endl;
        std::cout << "Queries per second: " << std::fixed << std::setprecision(1) << (1.0 / avg_seconds) << " qps" << std::endl;

        // 重置输出格式
        std::cout.unsetf(std::ios_base::floatfield);
    }

    std::cout << "\nSGX IRTree test completed successfully!" << std::endl;
}

int main() {
    try {
        std::cout << "Starting SGX IRTree Test Application" << std::endl;
        std::cout << "=====================================" << std::endl;
        
        // 测试 SGX 版本
        testWithQueryFileSGX("query.txt", true);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}