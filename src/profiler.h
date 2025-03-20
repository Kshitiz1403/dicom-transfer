#pragma once

#include <string>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>

class Profiler {
public:
    static Profiler& getInstance();
    
    // Start timing an operation
    void startOperation(const std::string& operationName);
    
    // End timing an operation
    void endOperation(const std::string& operationName);
    
    // Log bytes transferred for an operation
    void logTransferSize(const std::string& operationName, size_t bytes);
    
    // Generate a performance report
    std::string generateReport() const;
    
    // Reset all metrics
    void reset();

private:
    Profiler() = default;
    ~Profiler() = default;
    
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    
    struct OperationMetrics {
        std::chrono::high_resolution_clock::time_point startTime;
        std::chrono::high_resolution_clock::time_point endTime;
        bool inProgress = false;
        size_t bytesTransferred = 0;
        int count = 0;
    };
    
    std::map<std::string, OperationMetrics> m_metrics;
    mutable std::mutex m_mutex;
}; 