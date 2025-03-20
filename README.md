# Update package lists
sudo apt update

# Install build tools and required libraries
sudo apt install -y build-essential libdcmtk-dev libjsoncpp-dev libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev


# Install additional dependencies for AWS SDK
sudo apt install -y libcurl4-openssl-dev libssl-dev cmake


# Navigate to a directory where you want to download the SDK
cd ~

# Clone the AWS SDK repository
git clone --recursive https://github.com/aws/aws-sdk-cpp.git

# Navigate to the SDK directory
cd aws-sdk-cpp

# Create and navigate to build directory
mkdir build
cd build

# Configure the build (only build S3 and DynamoDB components to save time)
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_ONLY="s3;dynamodb" -DENABLE_TESTING=OFF

# Build the SDK (this will take some time)
make -j4

# Install the SDK
sudo make install


echo "export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH" >> ~/.bashrc
source ~/.bashrc


sudo apt install -y awscli
aws configure


aws s3api create-bucket --bucket dicom-transfer-bucket --region ap-south-1


export AWS_ACCESS_KEY_ID=YOUR_ACCESS_KEY
export AWS_SECRET_ACCESS_KEY=YOUR_SECRET_KEY
export AWS_DEFAULT_REGION=ap-south-1

./dicom_transfer --upload sample-dicom-files --verbose --threads 3

./dicom_transfer --download "1.3.12.2.1107.5.4.3.4975316777216.19951114.94101.16" --output temp