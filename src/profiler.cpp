#include "profiler.h"
#include <sstream>
#include <iomanip>

Profiler& Profiler::getInstance() {
    static Profiler instance;
    return instance;
}

void Profiler::startOperation(const std::string& operationName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto& metrics = m_metrics[operationName];
    metrics.startTime = std::chrono::high_resolution_clock::now();
    metrics.inProgress = true;
    metrics.count++;
}

void Profiler::endOperation(const std::string& operationName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_metrics.find(operationName) != m_metrics.end()) {
        auto& metrics = m_metrics[operationName];
        if (metrics.inProgress) {
            metrics.endTime = std::chrono::high_resolution_clock::now();
            metrics.inProgress = false;
        }
    }
}

void Profiler::logTransferSize(const std::string& operationName, size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto& metrics = m_metrics[operationName];
    metrics.bytesTransferred += bytes;
}

std::string Profiler::generateReport() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream ss;
    ss << "=== PERFORMANCE REPORT ===" << std::endl;
    
    for (const auto& [name, metrics] : m_metrics) {
        if (metrics.count == 0) continue;
        
        ss << "Operation: " << name << std::endl;
        ss << "  Count: " << metrics.count << std::endl;
        
        if (!metrics.inProgress) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                metrics.endTime - metrics.startTime).count();
            
            ss << "  Duration: " << duration << " ms" << std::endl;
            
            if (metrics.bytesTransferred > 0) {
                double seconds = duration / 1000.0;
                if (seconds > 0) {
                    double mbTransferred = metrics.bytesTransferred / (1024.0 * 1024.0);
                    double mbPerSec = mbTransferred / seconds;
                    
                    ss << "  Data transferred: " << std::fixed << std::setprecision(2) 
                       << mbTransferred << " MB" << std::endl;
                    ss << "  Transfer rate: " << std::fixed << std::setprecision(2) 
                       << mbPerSec << " MB/s" << std::endl;
                }
            }
        } else {
            ss << "  Status: In progress" << std::endl;
        }
        
        ss << std::endl;
    }
    
    return ss.str();
}

void Profiler::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.clear();
} 