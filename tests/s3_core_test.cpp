#include <gtest/gtest.h>
#include "../src/s3_manager.h"
#include "../src/utils.h"
#include <fstream>
#include <thread>
#include <filesystem>

class S3CoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(S3Manager::initializeAWS());
        system("aws s3api create-bucket --bucket dicom-transfer-core-test --region ap-south-1 --create-bucket-configuration LocationConstraint=ap-south-1");
        Utils::createDirectoryIfNotExists("core_test_files");
        m_s3Manager = std::make_unique<S3Manager>("ap-south-1");
    }

    void TearDown() override {
        system("rm -rf core_test_files");
        system("aws s3 rm s3://dicom-transfer-core-test --recursive");
        system("aws s3api delete-bucket --bucket dicom-transfer-core-test --region ap-south-1");
        S3Manager::shutdownAWS();
    }

    // Helper to create a test file with specific content
    std::string createTestFile(const std::string& filename, const std::string& content) {
        std::string filepath = "core_test_files/" + filename;
        std::ofstream file(filepath);
        file << content;
        file.close();
        return filepath;
    }

    // Helper to read file content
    std::string readFileContent(const std::string& filepath) {
        std::ifstream file(filepath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    const std::string TEST_BUCKET = "dicom-transfer-core-test";
    std::unique_ptr<S3Manager> m_s3Manager;
};

// Test basic file upload
TEST_F(S3CoreTest, BasicUpload) {
    std::string testContent = "Hello, S3!";
    std::string filepath = createTestFile("basic.txt", testContent);
    std::string s3Key = "test/basic.txt";

    EXPECT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
    EXPECT_TRUE(m_s3Manager->doesObjectExist(TEST_BUCKET, s3Key));
}

// Test file upload with special characters in key
TEST_F(S3CoreTest, SpecialCharactersInKey) {
    std::string testContent = "Special characters test";
    std::string filepath = createTestFile("special.txt", testContent);
    std::string s3Key = "test/special@#$%^&*.txt";

    EXPECT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
    EXPECT_TRUE(m_s3Manager->doesObjectExist(TEST_BUCKET, s3Key));
}

// Test upload of empty file
TEST_F(S3CoreTest, EmptyFileUpload) {
    std::string filepath = createTestFile("empty.txt", "");
    std::string s3Key = "test/empty.txt";

    EXPECT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
    EXPECT_TRUE(m_s3Manager->doesObjectExist(TEST_BUCKET, s3Key));
}

// Test basic file download
TEST_F(S3CoreTest, BasicDownload) {
    std::string testContent = "Download test content";
    std::string uploadPath = createTestFile("upload.txt", testContent);
    std::string s3Key = "test/download.txt";
    std::string downloadPath = "core_test_files/downloaded.txt";

    // Upload first
    ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, uploadPath, s3Key));

    // Then download
    EXPECT_TRUE(m_s3Manager->downloadFile(TEST_BUCKET, s3Key, downloadPath));
    EXPECT_EQ(readFileContent(downloadPath), testContent);
}

// Test file deletion
TEST_F(S3CoreTest, DeleteObject) {
    std::string filepath = createTestFile("delete.txt", "Delete me");
    std::string s3Key = "test/delete.txt";

    ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
    EXPECT_TRUE(m_s3Manager->deleteObject(TEST_BUCKET, s3Key));
    EXPECT_FALSE(m_s3Manager->doesObjectExist(TEST_BUCKET, s3Key));
}

// Test listing objects
TEST_F(S3CoreTest, ListObjects) {
    // Create multiple test files
    std::vector<std::string> testFiles = {
        "list1.txt",
        "list2.txt",
        "list3.txt"
    };

    std::string prefix = "test/list/";
    std::vector<std::string> uploadedKeys;

    // Upload test files
    for (const auto& filename : testFiles) {
        std::string filepath = createTestFile(filename, "List test content");
        std::string s3Key = prefix + filename;
        ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key));
        uploadedKeys.push_back(s3Key);
    }

    // List objects and verify
    auto listedObjects = m_s3Manager->listObjects(TEST_BUCKET, prefix);
    EXPECT_EQ(listedObjects.size(), testFiles.size());
    
    // Verify each key exists in the listed objects
    for (const auto& key : uploadedKeys) {
        EXPECT_TRUE(std::find(listedObjects.begin(), listedObjects.end(), key) != listedObjects.end());
    }
}

// Test error handling for non-existent file upload
TEST_F(S3CoreTest, NonExistentFileUpload) {
    std::string nonExistentFile = "core_test_files/doesnotexist.txt";
    EXPECT_FALSE(m_s3Manager->uploadFile(TEST_BUCKET, nonExistentFile, "test/fail.txt"));
}

// Test error handling for non-existent object download
TEST_F(S3CoreTest, NonExistentObjectDownload) {
    std::string downloadPath = "core_test_files/nonexistent_download.txt";
    EXPECT_FALSE(m_s3Manager->downloadFile(TEST_BUCKET, "test/doesnotexist.txt", downloadPath));
}

// Test concurrent operations
TEST_F(S3CoreTest, ConcurrentOperations) {
    const int numOperations = 5;
    std::vector<std::future<bool>> futures;

    // Concurrent uploads
    for (int i = 0; i < numOperations; ++i) {
        std::string content = "Concurrent test " + std::to_string(i);
        std::string filepath = createTestFile("concurrent" + std::to_string(i) + ".txt", content);
        std::string s3Key = "test/concurrent/file" + std::to_string(i) + ".txt";

        futures.push_back(std::async(std::launch::async, [this, filepath, s3Key]() {
            return m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key);
        }));
    }

    // Wait for all uploads and verify
    for (auto& future : futures) {
        EXPECT_TRUE(future.get());
    }
}

// Test upload with progress callback
TEST_F(S3CoreTest, UploadProgress) {
    std::string testContent(1024 * 1024, 'A'); // 1MB content
    std::string filepath = createTestFile("progress.txt", testContent);
    std::string s3Key = "test/progress.txt";

    size_t totalBytes = 0;
    auto progressCallback = [&totalBytes](size_t bytes) {
        totalBytes += bytes;
    };

    EXPECT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, filepath, s3Key, progressCallback));
    EXPECT_EQ(totalBytes, testContent.size());
}

// Test download with progress callback
TEST_F(S3CoreTest, DownloadProgress) {
    // First upload a file
    std::string testContent(1024 * 1024, 'B'); // 1MB content
    std::string uploadPath = createTestFile("progress_upload.txt", testContent);
    std::string s3Key = "test/progress_download.txt";
    std::string downloadPath = "core_test_files/progress_download.txt";

    ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, uploadPath, s3Key));

    size_t totalBytes = 0;
    auto progressCallback = [&totalBytes](size_t bytes) {
        totalBytes += bytes;
    };

    EXPECT_TRUE(m_s3Manager->downloadFile(TEST_BUCKET, s3Key, downloadPath, progressCallback));
    EXPECT_EQ(totalBytes, testContent.size());
} 