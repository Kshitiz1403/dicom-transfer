# DICOM Transfer Utility - API Documentation

## DicomProcessor Class
Handles DICOM file processing and metadata extraction.

### Class Methods

#### `bool isDicomFile(const std::string& filepath)`
Validates if a given file is a valid DICOM file.

**Parameters:**
- `filepath`: Path to the file to check

**Returns:**
- `true` if file is a valid DICOM file
- `false` otherwise

#### `bool extractMetadata(const std::string& filepath, Json::Value& metadata)`
Extracts metadata from a DICOM file into a JSON structure.

**Parameters:**
- `filepath`: Path to the DICOM file
- `metadata`: JSON object to store extracted metadata

**Returns:**
- `true` if metadata extraction successful
- `false` if extraction fails

#### `std::string getStudyUid(const std::string& filepath)`
Extracts the Study Instance UID from a DICOM file.

**Parameters:**
- `filepath`: Path to the DICOM file

**Returns:**
- Study Instance UID string
- Empty string if extraction fails

#### `bool generateMetadataJson(const std::vector<std::string>& dicomFiles, const std::string& jsonFilePath)`
Generates a JSON file containing metadata for a collection of DICOM files.

**Parameters:**
- `dicomFiles`: Vector of paths to DICOM files
- `jsonFilePath`: Output path for JSON file

**Returns:**
- `true` if JSON generation successful
- `false` if generation fails

## DynamoDBManager Class
Manages interactions with AWS DynamoDB for metadata storage.

### Class Methods

#### `bool storeStudyMetadata(const std::string& tableName, const std::string& studyUid, const Json::Value& metadata)`
Stores study metadata in DynamoDB.

**Parameters:**
- `tableName`: Name of the DynamoDB table
- `studyUid`: Study Instance UID
- `metadata`: JSON object containing study metadata

**Returns:**
- `true` if storage successful
- `false` if storage fails

#### `bool getStudyMetadata(const std::string& tableName, const std::string& studyUid, Json::Value& metadata)`
Retrieves study metadata from DynamoDB.

**Parameters:**
- `tableName`: Name of the DynamoDB table
- `studyUid`: Study Instance UID
- `metadata`: JSON object to store retrieved metadata

**Returns:**
- `true` if retrieval successful
- `false` if retrieval fails

#### `bool storeFileLocation(const std::string& tableName, const std::string& studyUid, const std::string& s3Key)`
Stores S3 file location in DynamoDB.

**Parameters:**
- `tableName`: Name of the DynamoDB table
- `studyUid`: Study Instance UID
- `s3Key`: S3 object key for the file

**Returns:**
- `true` if storage successful
- `false` if storage fails

#### `std::vector<std::string> getFileLocations(const std::string& tableName, const std::string& studyUid)`
Retrieves all file locations for a study.

**Parameters:**
- `tableName`: Name of the DynamoDB table
- `studyUid`: Study Instance UID

**Returns:**
- Vector of S3 keys for all files in the study

#### `bool createTableIfNotExists(const std::string& tableName)`
Creates a new DynamoDB table if it doesn't exist.

**Parameters:**
- `tableName`: Name of the table to create

**Returns:**
- `true` if table exists or was created successfully
- `false` if table creation fails 