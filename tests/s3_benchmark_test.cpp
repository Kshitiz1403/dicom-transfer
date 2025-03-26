#include <gtest/gtest.h>
#include "../src/s3_manager.h"
#include "../src/utils.h"
#include "../src/thread_pool.h"
#include "../src/profiler.h"
#include <fstream>
#include <chrono>
#include <vector>
#include <thread>
#include <sys/resource.h>

class S3BenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(S3Manager::initializeAWS());
        system("aws s3api create-bucket --bucket dicom-transfer-benchmark-bucket --region ap-south-1 --create-bucket-configuration LocationConstraint=ap-south-1");
        Utils::createDirectoryIfNotExists("benchmark_files");
        m_s3Manager = std::make_unique<S3Manager>("ap-south-1");
    }

    void TearDown() override {
        system("rm -rf benchmark_files");
        system("aws s3 rm s3://dicom-transfer-benchmark-bucket --recursive");
        S3Manager::shutdownAWS();
    }

    // Helper method to create a test file of specific size
    std::string createTestFile(const std::string& filename, size_t sizeInMB) {
        std::string filepath = "benchmark_files/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        
        const size_t blockSize = 1024 * 1024; // 1MB
        std::vector<char> buffer(blockSize, 'A');
        
        for (size_t i = 0; i < sizeInMB; ++i) {
            file.write(buffer.data(), blockSize);
        }
        
        file.close();
        return filepath;
    }

    // Helper to measure memory usage (in KB)
    size_t getCurrentMemoryUsage() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss;
    }

    // Helper to measure elapsed time
    double measureElapsedTime(std::function<void()> func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(end - start).count();
    }

    const std::string TEST_BUCKET = "dicom-transfer-benchmark-bucket";
    std::unique_ptr<S3Manager> m_s3Manager;
};

// Test upload performance with different thread counts
TEST_F(S3BenchmarkTest, UploadThreadScaling) {
    const size_t fileSize = 10; // 10MB
    const std::vector<size_t> threadCounts = {1, 2, 4, 8, 16};
    const size_t filesPerThread = 5;
    
    std::cout << "\nUpload Thread Scaling Benchmark:" << std::endl;
    std::cout << "Thread Count | Total Time (s) | Throughput (MB/s)" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    for (size_t threadCount : threadCounts) {
        // Create test files
        std::vector<std::string> testFiles;
        for (size_t i = 0; i < threadCount * filesPerThread; ++i) {
            std::string filename = "thread_test_" + std::to_string(i) + ".dat";
            testFiles.push_back(createTestFile(filename, fileSize));
        }

        // Measure upload time
        ThreadPool pool(threadCount);
        std::vector<std::future<bool>> futures;
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (const auto& file : testFiles) {
            std::string s3Key = "benchmark/" + Utils::getFileName(file);
            futures.push_back(pool.enqueue([this, file, s3Key]() {
                return m_s3Manager->uploadFile(TEST_BUCKET, file, s3Key);
            }));
        }

        // Wait for all uploads to complete
        for (auto& future : futures) {
            ASSERT_TRUE(future.get());
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        double totalTime = std::chrono::duration<double>(endTime - startTime).count();
        double throughput = (fileSize * threadCount * filesPerThread) / totalTime;

        std::cout << std::setw(11) << threadCount << " | "
                  << std::setw(13) << std::fixed << std::setprecision(2) << totalTime << " | "
                  << std::setw(8) << throughput << std::endl;
    }
}

// Test download performance with different file sizes
TEST_F(S3BenchmarkTest, DownloadSizeScaling) {
    const std::vector<size_t> fileSizes = {1, 10, 50, 100, 500}; // MB
    
    std::cout << "\nDownload Size Scaling Benchmark:" << std::endl;
    std::cout << "File Size (MB) | Download Time (s) | Throughput (MB/s)" << std::endl;
    std::cout << "----------------------------------------------------" << std::endl;

    for (size_t fileSize : fileSizes) {
        // Create and upload test file
        std::string filename = "size_test_" + std::to_string(fileSize) + "MB.dat";
        std::string filepath = createTestFile(filename, fileSize);
        std::string s3Key = "benchmark/" + filename;
        
        ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
        
        // Measure download time
        std::string downloadPath = "benchmark_files/download_" + filename;
        
        double downloadTime = measureElapsedTime([&]() {
            ASSERT_TRUE(m_s3Manager->downloadFile(TEST_BUCKET, s3Key, downloadPath));
        });

        double throughput = fileSize / downloadTime;

        std::cout << std::setw(12) << fileSize << " | "
                  << std::setw(15) << std::fixed << std::setprecision(2) << downloadTime << " | "
                  << std::setw(8) << throughput << std::endl;
    }
}

// Test memory usage under load
TEST_F(S3BenchmarkTest, MemoryUsage) {
    const size_t fileSize = 100; // 100MB
    const size_t numFiles = 10;
    
    std::cout << "\nMemory Usage Benchmark:" << std::endl;
    std::cout << "Operation | Memory Usage (KB)" << std::endl;
    std::cout << "------------------------------" << std::endl;

    // Baseline memory usage
    size_t baselineMemory = getCurrentMemoryUsage();
    std::cout << "Baseline  | " << baselineMemory << std::endl;

    // Create test files
    std::vector<std::string> testFiles;
    for (size_t i = 0; i < numFiles; ++i) {
        std::string filename = "memory_test_" + std::to_string(i) + ".dat";
        testFiles.push_back(createTestFile(filename, fileSize));
    }

    // Memory usage after file creation
    size_t afterCreationMemory = getCurrentMemoryUsage();
    std::cout << "Creation  | " << afterCreationMemory << std::endl;

    // Concurrent uploads
    ThreadPool pool(4);
    std::vector<std::future<bool>> futures;
    
    for (const auto& file : testFiles) {
        std::string s3Key = "benchmark/" + Utils::getFileName(file);
        futures.push_back(pool.enqueue([this, file, s3Key]() {
            return m_s3Manager->uploadFile(TEST_BUCKET, file, s3Key);
        }));
    }

    // Memory during upload
    size_t duringUploadMemory = getCurrentMemoryUsage();
    std::cout << "Upload    | " << duringUploadMemory << std::endl;

    // Wait for uploads to complete
    for (auto& future : futures) {
        ASSERT_TRUE(future.get());
    }

    // Memory after upload
    size_t afterUploadMemory = getCurrentMemoryUsage();
    std::cout << "Complete  | " << afterUploadMemory << std::endl;
}

// Test network bandwidth utilization
TEST_F(S3BenchmarkTest, BandwidthUtilization) {
    const size_t fileSize = 100; // 100MB
    const int duration = 30; // Test duration in seconds
    
    std::cout << "\nBandwidth Utilization Benchmark:" << std::endl;
    std::cout << "Time (s) | Upload Speed (MB/s) | Download Speed (MB/s)" << std::endl;
    std::cout << "----------------------------------------------------" << std::endl;

    // Create test file
    std::string filename = "bandwidth_test.dat";
    std::string filepath = createTestFile(filename, fileSize);
    std::string s3Key = "benchmark/" + filename;

    // Monitor bandwidth over time
    auto startTime = std::chrono::steady_clock::now();
    size_t totalUploaded = 0;
    size_t totalDownloaded = 0;
    int currentSecond = 0;

    while (currentSecond < duration) {
        // Upload
        ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key + "_" + std::to_string(currentSecond)));
        totalUploaded += fileSize;

        // Download
        std::string downloadPath = "benchmark_files/download_" + std::to_string(currentSecond) + "_" + filename;
        ASSERT_TRUE(m_s3Manager->downloadFile(TEST_BUCKET, s3Key + "_" + std::to_string(currentSecond), downloadPath));
        totalDownloaded += fileSize;

        auto now = std::chrono::steady_clock::now();
        currentSecond = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        double uploadSpeed = totalUploaded / static_cast<double>(currentSecond);
        double downloadSpeed = totalDownloaded / static_cast<double>(currentSecond);

        std::cout << std::setw(8) << currentSecond << " | "
                  << std::setw(17) << std::fixed << std::setprecision(2) << uploadSpeed << " | "
                  << std::setw(18) << downloadSpeed << std::endl;
    }
} 