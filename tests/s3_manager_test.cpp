#include <gtest/gtest.h>
#include "../src/s3_manager.h"
#include "../src/utils.h"
#include <fstream>
#include <thread>
#include <vector>

class S3ManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize AWS SDK
        ASSERT_TRUE(S3Manager::initializeAWS());
        
        // Create test bucket if it doesn't exist
        system("aws s3api create-bucket --bucket dicom-transfer-test-bucket --region ap-south-1 --create-bucket-configuration LocationConstraint=ap-south-1");
       
        // Create test files directory
        Utils::createDirectoryIfNotExists("test_files");
    }

    void TearDown() override {
        // Clean up test files
        system("rm -rf test_files");
        
        // Clean up test bucket
        system("aws s3 rm s3://dicom-transfer-test-bucket --recursive");
        
        S3Manager::shutdownAWS();
    }

    // Helper method to create a file with specific size
    std::string createTestFile(const std::string& filename, size_t sizeInMB) {
        std::string filepath = "test_files/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        
        const size_t blockSize = 1024 * 1024; // 1MB
        std::vector<char> buffer(blockSize, 'A');
        
        for (size_t i = 0; i < sizeInMB; ++i) {
            file.write(buffer.data(), blockSize);
        }
        
        file.close();
        return filepath;
    }

    const std::string TEST_BUCKET = "dicom-transfer-test-bucket";
};

// Test successful file upload
TEST_F(S3ManagerTest, SuccessfulUpload) {
    S3Manager s3Manager("ap-south-1");
    
    // Create a test file
    std::string testFile = createTestFile("test1.txt", 1); // 1MB file
    std::string s3Key = "test/test1.txt";
    
    // Test upload
    EXPECT_TRUE(s3Manager.uploadFile(TEST_BUCKET, testFile, s3Key));
    
    // Verify file exists in S3
    EXPECT_TRUE(s3Manager.doesObjectExist(TEST_BUCKET, s3Key));
}

// Test upload with non-existent file
TEST_F(S3ManagerTest, UploadNonExistentFile) {
    S3Manager s3Manager("ap-south-1");
    
    std::string nonExistentFile = "test_files/doesnotexist.txt";
    std::string s3Key = "test/doesnotexist.txt";
    
    // Test upload should fail
    EXPECT_FALSE(s3Manager.uploadFile(TEST_BUCKET, nonExistentFile, s3Key));
}

// Test download with non-existent S3 key
TEST_F(S3ManagerTest, DownloadNonExistentKey) {
    S3Manager s3Manager("ap-south-1");
    
    std::string nonExistentKey = "test/doesnotexist.txt";
    std::string downloadPath = "test_files/downloaded.txt";
    
    // Test download should fail
    EXPECT_FALSE(s3Manager.downloadFile(TEST_BUCKET, nonExistentKey, downloadPath));
}

// Test concurrent uploads
TEST_F(S3ManagerTest, ConcurrentUploads) {
    S3Manager s3Manager("ap-south-1");
    const int numFiles = 5;
    std::vector<std::thread> threads;
    std::vector<bool> results(numFiles, false);
    
    // Create test files
    std::vector<std::string> testFiles;
    for (int i = 0; i < numFiles; ++i) {
        testFiles.push_back(createTestFile("concurrent_" + std::to_string(i) + ".txt", 1));
    }
    
    // Launch concurrent uploads
    for (int i = 0; i < numFiles; ++i) {
        threads.emplace_back([&, i]() {
            std::string s3Key = "test/concurrent_" + std::to_string(i) + ".txt";
            results[i] = s3Manager.uploadFile(TEST_BUCKET, testFiles[i], s3Key);
        });
    }
    
    // Wait for all uploads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all uploads were successful
    for (int i = 0; i < numFiles; ++i) {
        EXPECT_TRUE(results[i]);
        EXPECT_TRUE(s3Manager.doesObjectExist(TEST_BUCKET, "test/concurrent_" + std::to_string(i) + ".txt"));
    }
}

// Test upload and download with large file
TEST_F(S3ManagerTest, LargeFileTransfer) {
    S3Manager s3Manager("ap-south-1");
    
    // Create a 100MB test file
    std::string testFile = createTestFile("large_file.txt", 100);
    std::string s3Key = "test/large_file.txt";
    std::string downloadPath = "test_files/downloaded_large_file.txt";
    
    // Track progress
    size_t uploadedBytes = 0;
    size_t downloadedBytes = 0;
    
    // Test upload
    EXPECT_TRUE(s3Manager.uploadFile(
        TEST_BUCKET, 
        testFile, 
        s3Key,
        [&uploadedBytes](size_t bytes) {
            uploadedBytes += bytes;
        }
    ));
    
    // Verify file exists in S3
    EXPECT_TRUE(s3Manager.doesObjectExist(TEST_BUCKET, s3Key));
    
    // Test download
    EXPECT_TRUE(s3Manager.downloadFile(
        TEST_BUCKET,
        s3Key,
        downloadPath,
        [&downloadedBytes](size_t bytes) {
            downloadedBytes += bytes;
        }
    ));
    
    // Verify file sizes
    EXPECT_EQ(Utils::getFileSize(testFile), Utils::getFileSize(downloadPath));
    EXPECT_EQ(uploadedBytes, 100 * 1024 * 1024);
    EXPECT_EQ(downloadedBytes, 100 * 1024 * 1024);
} 