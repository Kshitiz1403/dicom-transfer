#include "dicom_processor.h"
#include "logger.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmimgle/dcmimage.h>

#include <fstream>
#include <sstream>
#include <algorithm>

DcmTagKey parseTagKey(const std::string& tagStr) {
    // Expected format: "gggg,eeee" where g is group and e is element
    size_t commaPos = tagStr.find(',');
    if (commaPos == std::string::npos) {
        LOG_ERROR("Invalid DICOM tag format: " + tagStr);
        return DCM_UndefinedTagKey;
    }
    
    try {
        std::string groupStr = tagStr.substr(0, commaPos);
        std::string elemStr = tagStr.substr(commaPos + 1);
        
        // Parse hexadecimal values
        Uint16 group = static_cast<Uint16>(std::stoi(groupStr, nullptr, 16));
        Uint16 elem = static_cast<Uint16>(std::stoi(elemStr, nullptr, 16));
        
        return DcmTagKey(group, elem);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse DICOM tag: " + tagStr + " - " + e.what());
        return DCM_UndefinedTagKey;
    }
}

DicomProcessor::DicomProcessor() {
    // Initialize DCMTK if needed
}

bool DicomProcessor::isDicomFile(const std::string& filepath) {
    DcmFileFormat fileformat;
    OFCondition status = fileformat.loadFile(filepath.c_str());
    return status.good();
}

bool DicomProcessor::extractMetadata(const std::string& filepath, Json::Value& metadata) {
    if (!isDicomFile(filepath)) {
        LOG_ERROR("File is not a valid DICOM file: " + filepath);
        return false;
    }
    
    try {
        DcmFileFormat fileformat;
        OFCondition status = fileformat.loadFile(filepath.c_str());
        
        if (status.good()) {
            DcmDataset* dataset = fileformat.getDataset();
            
            for (const auto& tag : m_commonTags) {
                const std::string& tagName = tag.first;
                const std::string& tagID = tag.second;
                
                OFString value;
                DcmTagKey dcmTagKey = parseTagKey(tagID);
                
                if (dataset->findAndGetOFString(dcmTagKey, value).good()) {
                    metadata[tagName] = std::string(value.c_str());
                }
            }
            
            return true;
        } else {
            LOG_ERROR("Failed to load DICOM file: " + filepath);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while extracting DICOM metadata: " + std::string(e.what()));
        return false;
    }
}

std::string DicomProcessor::getStudyUid(const std::string& filepath) {
    return extractTag(filepath, "0020,000D"); // StudyInstanceUID
}

bool DicomProcessor::generateMetadataJson(const std::vector<std::string>& dicomFiles, 
                                         const std::string& jsonFilePath) {
    if (dicomFiles.empty()) {
        LOG_WARNING("No DICOM files provided for metadata generation");
        return false;
    }
    
    try {
        // Extract metadata from the first file for study-level information
        Json::Value studyMetadata;
        if (!extractMetadata(dicomFiles[0], studyMetadata)) {
            LOG_ERROR("Failed to extract metadata from first DICOM file");
            return false;
        }
        
        // Add file list to metadata
        Json::Value fileList(Json::arrayValue);
        for (const auto& filepath : dicomFiles) {
            Json::Value fileMetadata;
            if (extractMetadata(filepath, fileMetadata)) {
                fileList.append(fileMetadata);
            }
        }
        
        studyMetadata["Files"] = fileList;
        studyMetadata["TotalFiles"] = static_cast<int>(dicomFiles.size());
        
        // Write to JSON file
        std::ofstream jsonFile(jsonFilePath);
        if (!jsonFile.is_open()) {
            LOG_ERROR("Failed to open JSON file for writing: " + jsonFilePath);
            return false;
        }
        
        Json::StyledWriter writer;
        jsonFile << writer.write(studyMetadata);
        jsonFile.close();
        
        LOG_INFO("Generated metadata JSON file: " + jsonFilePath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while generating JSON metadata: " + std::string(e.what()));
        return false;
    }
}

std::map<std::string, std::vector<std::string>> DicomProcessor::groupFilesByStudy(
    const std::vector<std::string>& dicomFiles) {
    
    std::map<std::string, std::vector<std::string>> studyGroups;
    
    for (const auto& filepath : dicomFiles) {
        std::string studyUid = getStudyUid(filepath);
        if (!studyUid.empty()) {
            studyGroups[studyUid].push_back(filepath);
        } else {
            LOG_WARNING("Could not determine study UID for file: " + filepath);
        }
    }
    
    return studyGroups;
}

std::string DicomProcessor::extractTag(const std::string& filepath, const std::string& tag) {
    DcmFileFormat fileformat;
    OFCondition status = fileformat.loadFile(filepath.c_str());
    
    if (status.good()) {
        DcmDataset* dataset = fileformat.getDataset();
        OFString value;
        DcmTagKey dcmTagKey = parseTagKey(tag);
        
        if (dataset->findAndGetOFString(dcmTagKey, value).good()) {
            return std::string(value.c_str());
        }
    }
    
    return "";
}

bool DicomProcessor::isValidDicomFile(const std::string& filepath) {
    return isDicomFile(filepath);
} 