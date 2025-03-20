#pragma once

#include <string>
#include <vector>
#include <map>
#include <json/json.h>

// Forward declaration
class Logger;

class DicomProcessor {
public:
    DicomProcessor();
    
    // Check if a file is a DICOM file
    bool isDicomFile(const std::string& filepath);
    
    // Extract metadata from a DICOM file
    bool extractMetadata(const std::string& filepath, Json::Value& metadata);
    
    // Get study UID from a DICOM file
    std::string getStudyUid(const std::string& filepath);
    
    // Generate a JSON metadata file for a study
    bool generateMetadataJson(const std::vector<std::string>& dicomFiles, 
                             const std::string& jsonFilePath);
    
    // Group DICOM files by study UID
    std::map<std::string, std::vector<std::string>> groupFilesByStudy(
        const std::vector<std::string>& dicomFiles);
    
private:
    // Helper methods
    std::string extractTag(const std::string& filepath, const std::string& tag);
    bool isValidDicomFile(const std::string& filepath);
    
    // Common DICOM tags to extract
    const std::vector<std::pair<std::string, std::string>> m_commonTags = {
        {"PatientID", "0010,0020"},
        {"PatientName", "0010,0010"},
        {"StudyDate", "0008,0020"},
        {"StudyTime", "0008,0030"},
        {"AccessionNumber", "0008,0050"},
        {"StudyID", "0020,0010"},
        {"StudyInstanceUID", "0020,000D"},
        {"StudyDescription", "0008,1030"},
        {"Modality", "0008,0060"},
        {"SeriesInstanceUID", "0020,000E"},
        {"SeriesNumber", "0020,0011"},
        {"SeriesDescription", "0008,103E"},
        {"SOPInstanceUID", "0008,0018"}
    };
}; 