#include <gtest/gtest.h>
#include "../src/dicom_processor.h"
#include "../src/s3_manager.h"
#include "../src/dynamodb_manager.h"
#include "../src/utils.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class DicomTransferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize AWS services
        ASSERT_TRUE(S3Manager::initializeAWS());
        
        // Create test directories
        Utils::createDirectoryIfNotExists("test_dicom_files");
        Utils::createDirectoryIfNotExists("test_output");
        
        // Create test bucket and DynamoDB table
        system("aws s3api create-bucket --bucket dicom-transfer-test --region ap-south-1 --create-bucket-configuration LocationConstraint=ap-south-1");
        
        // Download sample DICOM files
        system("aws s3 cp s3://sample-dicoms test_dicom_files/ --recursive");
        
        // Initialize managers
        m_s3Manager = std::make_unique<S3Manager>("ap-south-1");
        m_dynamoManager = std::make_unique<DynamoDBManager>("ap-south-1");
        m_dicomProcessor = std::make_unique<DicomProcessor>();
        
        // Create DynamoDB table
        ASSERT_TRUE(m_dynamoManager->createTableIfNotExists(TEST_TABLE));
    }

    void TearDown() override {
        // Cleanup test directories
        system("rm -rf test_dicom_files");
        system("rm -rf test_output");
        
        // Cleanup AWS resources
        system("aws s3 rm s3://dicom-transfer-test --recursive");
        system("aws s3api delete-bucket --bucket dicom-transfer-test --region ap-south-1");
        system("aws dynamodb delete-table --table-name dicom-transfer-test-table --region ap-south-1");
        
        S3Manager::shutdownAWS();
    }

    const std::string TEST_BUCKET = "dicom-transfer-test";
    const std::string TEST_TABLE = "dicom-transfer-test-table";
    std::unique_ptr<S3Manager> m_s3Manager;
    std::unique_ptr<DynamoDBManager> m_dynamoManager;
    std::unique_ptr<DicomProcessor> m_dicomProcessor;
};

// Test DICOM file identification and grouping
TEST_F(DicomTransferTest, FileIdentificationAndGrouping) {
    // Get all files from the test directories
    std::vector<std::string> allFiles = Utils::listFilesInDirectory("test_dicom_files", true);
    std::vector<std::string> dicomFiles;
    
    // Filter DICOM files
    for (const auto& file : allFiles) {
        if (m_dicomProcessor->isDicomFile(file)) {
            dicomFiles.push_back(file);
        }
    }
    
    EXPECT_GT(dicomFiles.size(), 0);
    
    // Group files by study
    auto studyGroups = m_dicomProcessor->groupFilesByStudy(dicomFiles);
    EXPECT_GT(studyGroups.size(), 0);
    
    // Verify each study has files
    for (const auto& study : studyGroups) {
        EXPECT_GT(study.second.size(), 0);
        
        // Verify all files in a study have the same StudyInstanceUID
        std::string studyUid = m_dicomProcessor->getStudyUid(study.second[0]);
        for (const auto& file : study.second) {
            EXPECT_EQ(m_dicomProcessor->getStudyUid(file), studyUid);
        }
    }
}

// Test metadata extraction and storage
TEST_F(DicomTransferTest, MetadataExtractionAndStorage) {
    // Get first DICOM file from study1
    std::vector<std::string> study1Files = Utils::listFilesInDirectory("test_dicom_files/study1", true);
    std::string dicomFile;
    for (const auto& file : study1Files) {
        if (m_dicomProcessor->isDicomFile(file)) {
            dicomFile = file;
            break;
        }
    }
    ASSERT_FALSE(dicomFile.empty());
    
    // Extract and verify metadata
    Json::Value metadata;
    EXPECT_TRUE(m_dicomProcessor->extractMetadata(dicomFile, metadata));
    
    std::string studyUid = m_dicomProcessor->getStudyUid(dicomFile);
    EXPECT_FALSE(studyUid.empty());
    EXPECT_EQ(metadata["StudyInstanceUID"].asString(), studyUid);
    
    // Test metadata JSON generation for all files in the study
    std::vector<std::string> studyFiles;
    for (const auto& file : study1Files) {
        if (m_dicomProcessor->isDicomFile(file) && 
            m_dicomProcessor->getStudyUid(file) == studyUid) {
            studyFiles.push_back(file);
        }
    }
    
    std::string jsonPath = "test_dicom_files/metadata.json";
    EXPECT_TRUE(m_dicomProcessor->generateMetadataJson(studyFiles, jsonPath));
    
    // Test metadata storage in DynamoDB
    EXPECT_TRUE(m_dynamoManager->storeStudyMetadata(TEST_TABLE, studyUid, metadata));
    
    // Verify stored metadata
    Json::Value retrievedMetadata;
    EXPECT_TRUE(m_dynamoManager->getStudyMetadata(TEST_TABLE, studyUid, retrievedMetadata));
    EXPECT_EQ(retrievedMetadata["StudyInstanceUID"], metadata["StudyInstanceUID"]);
}


// Test complete upload process
TEST_F(DicomTransferTest, CompleteUploadProcess) {
    std::string studyUid = "1.3.12.2.1107.5.4.3.4975316777216.19951114.94101.16";
    std::vector<std::string> studyFiles = {
        "test_dicom_files/study1/0002.DCM",
        "test_dicom_files/study2/0003.DCM",
        "test_dicom_files/study3/0004.DCM",
        "test_dicom_files/study4/0012.DCM",
        "test_dicom_files/study5/0015.DCM",
        "test_dicom_files/study6/0020.DCM",
        "test_dicom_files/study7/MRBRAIN.DCM",
        "test_dicom_files/study8/0009.DCM"
    };
    
    // Group files by study
    auto studyGroups = m_dicomProcessor->groupFilesByStudy(studyFiles);
    ASSERT_EQ(studyGroups.size(), 6);
    
    // Process each study
    for (const auto& study : studyGroups) {
        const std::string& currentStudyUid = study.first;
        const auto& files = study.second;
        
        // Generate and store metadata
        Json::Value metadata;
        ASSERT_TRUE(m_dicomProcessor->extractMetadata(files[0], metadata));
        ASSERT_TRUE(m_dynamoManager->storeStudyMetadata(TEST_TABLE, currentStudyUid, metadata));
        
        // Upload each file
        for (const auto& file : files) {
            std::string s3Key = Utils::generateS3Key(currentStudyUid, file);
            ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, file, s3Key));
            ASSERT_TRUE(m_dynamoManager->storeFileLocation(TEST_TABLE, currentStudyUid, s3Key));
        }
    }
    
    // Verify uploads
    auto fileLocations = m_dynamoManager->getFileLocations(TEST_TABLE, studyUid);
    
    for (const auto& location : fileLocations) {
        EXPECT_TRUE(m_s3Manager->doesObjectExist(TEST_BUCKET, location));
    }
}

// Test download process
TEST_F(DicomTransferTest, CompleteDownloadProcess) {
    // First upload some files
    std::string studyUid = "1.2.3.4.5";
    std::vector<std::string> originalFiles = {
        "test_dicom_files/study1/0002.DCM",
        "test_dicom_files/study2/0003.DCM",
        "test_dicom_files/study3/0004.DCM",
        "test_dicom_files/study4/0012.DCM",
        "test_dicom_files/study5/0015.DCM",
        "test_dicom_files/study6/0020.DCM",
        "test_dicom_files/study7/MRBRAIN.DCM",
        "test_dicom_files/study8/0009.DCM"
    };
    
    // Upload files
    for (const auto& file : originalFiles) {
        std::string s3Key = Utils::generateS3Key(studyUid, file);
        ASSERT_TRUE(m_s3Manager->uploadFile(TEST_BUCKET, file, s3Key));
        ASSERT_TRUE(m_dynamoManager->storeFileLocation(TEST_TABLE, studyUid, s3Key));
    }
    
    // Test download process
    std::string outputDir = "test_output";
    
    // Get file locations from DynamoDB
    auto fileLocations = m_dynamoManager->getFileLocations(TEST_TABLE, studyUid);
    ASSERT_EQ(fileLocations.size(), originalFiles.size());
    
    // Download each file
    for (const auto& s3Key : fileLocations) {
        std::string filename = Utils::getFileName(s3Key);
        std::string downloadPath = Utils::joinPath(outputDir, filename);
        ASSERT_TRUE(m_s3Manager->downloadFile(TEST_BUCKET, s3Key, downloadPath));
        
        // Verify file exists and has content
        EXPECT_TRUE(Utils::fileExists(downloadPath));
        EXPECT_GT(Utils::getFileSize(downloadPath), 0);
    }
}

// Test error handling
TEST_F(DicomTransferTest, ErrorHandling) {
    // Test invalid DICOM file
    std::ofstream("test_dicom_files/invalid.dcm") << "Not a valid DICOM file";
    EXPECT_FALSE(m_dicomProcessor->isDicomFile("test_dicom_files/invalid.dcm"));
    
    // Test non-existent study
    Json::Value metadata;
    EXPECT_FALSE(m_dynamoManager->getStudyMetadata(TEST_TABLE, "non-existent-uid", metadata));
    
    // Test invalid file upload
    EXPECT_FALSE(m_s3Manager->uploadFile(TEST_BUCKET, "non-existent-file.dcm", "test/key"));
    
    // Test invalid study download
    auto fileLocations = m_dynamoManager->getFileLocations(TEST_TABLE, "non-existent-uid");
    EXPECT_TRUE(fileLocations.empty());
} 