# Custom_File_System_FATA
Custom FAT File System Implementation
A complete File Allocation Table (FAT) based file system implementation in C with configurable settings, encryption support, and variable file name sizes.

📋 Project Overview
This project implements a custom FAT file system from scratch, providing a console interface to interact with the file system. It supports all basic file operations, directory management, and includes several bonus features for enhanced functionality.

✨ Features
Core Features
FAT-based File System: Implements File Allocation Table for efficient block management

File Operations: Create, delete, read, write, and truncate files

Directory Operations: Create and delete directories, list contents

Block-level Operations: Low-level read/write block operations using host OS filing

Console Interface: Interactive command-line interface for file system management

Bonus Features
🔧 Configurable System Settings: Parameterize disk size, block size, and file limits

📛 Variable File Name Size: Support for filenames up to 64 bytes (configurable to 255)

🔒 Partition Encryption: Basic encryption support for data security

⚙️ Runtime Configuration: Modify system parameters without recompilation

🛠️ System Specifications
Default Settings
Directory Size: 128 entries

Maximum File Name Size: 64 bytes (configurable)

Maximum File Size: 128 blocks

Block Size: 1 KB (1024 bytes)

Total Disk Size: 64 MB

Configurable Parameters
Disk Size: 1MB - 1GB

Block Size: 512B - 16KB (power of 2)

Max Files per Directory: 16 - 1024

Max Filename Size: 8 - 255 bytes

Max File Blocks: 1 - 65535

🚀 Getting Started
Prerequisites
GCC compiler

Standard C library

Linux/Unix environment (tested on WSL2)

Compilation
bash
gcc -o fatfs fatfs.c
Running the Program
bash
./fatfs
📖 Usage Guide
Basic Commands
bash
# Create and format a new partition
format mydisk.fs

# Mount an existing partition
mount mydisk.fs

# Create directory
mkdir documents

# Create file
create hello.txt

# Write to file
write hello.txt "Hello, World!"

# Read file
read hello.txt

# List directory contents
ls

# Delete file
delete hello.txt

# Unmount partition
unmount

# Exit program
exit
Bonus Feature Commands
bash
# Show current configuration
config show

# Change system parameters
config set disk_size 128        # Set disk to 128MB
config set block_size 2048      # Set block size to 2KB
config set max_files 256        # Set max files per directory to 256
config set max_filename 128     # Set max filename size to 128 bytes

# Create encrypted partition
format secure.fs encrypt
🏗️ System Architecture
File System Layout
text
Block 0:     [ BOOT SECTOR ]
             - File system metadata and configuration

Blocks 1-N:  [ FAT TABLE ]
             - File Allocation Table for block tracking

Block N+1:   [ ROOT DIRECTORY ]
             - Directory entries for root

Blocks N+2+: [ DATA BLOCKS ]
             - Actual file data storage
Key Data Structures
BootSector: File system metadata and configuration

DirectoryEntry: File/directory metadata with variable filename support

Directory: Container for directory entries

FileSystem: Global file system context with encryption support

🔧 Technical Implementation
FAT Management
Uses 16-bit FAT entries supporting up to 65536 blocks

Free blocks marked with 0xFFFF

End-of-file marked with 0xFFFE

Bad blocks marked with 0xFFFD

Block Allocation
First-fit allocation strategy

Automatic block chaining via FAT

Efficient space reclamation on file deletion

Directory Management
Two-level directory structure (root + subdirectories)

Fixed-size directory entries with efficient storage

Support for "." and ".." directory entries

🎯 Design Decisions
Performance Considerations
Fixed block size for simplicity and predictable performance

FAT table loaded into memory for fast access

Sequential block allocation for better read performance

Security Features
Optional basic encryption for data blocks

File system signature verification during mount

Proper error handling and validation

Flexibility
Configurable system parameters at runtime

Support for variable filename lengths

Extensible architecture for additional features

📁 Project Structure
text
fatfs.c          - Main implementation file
README.md        - Project documentation
🐛 Known Issues and Limitations
Encryption uses simple XOR for demonstration (not cryptographically secure)

No journaling or transaction support

Limited error recovery mechanisms

Single-threaded implementation

🔮 Future Enhancements
Implement proper AES encryption with OpenSSL

Add file permissions and access control

Support for file appending and random access

Journaling for crash recovery

Multi-threading support

File compression

Network file system capabilities

👨‍💻 Development
Challenges Addressed
Efficient Block Management: Implemented FAT-based allocation with proper chaining

Directory Hierarchy: Managed two-level directory structure within fixed constraints

Variable File Names: Efficient storage of different filename lengths

Configuration Management: Runtime parameter changes without system restart

Testing Strategy
Manual testing of all file operations

Boundary testing for file sizes and names

Configuration change validation

Mount/unmount cycle testing

📄 License
This project is for educational purposes as part of an operating systems assignment.

👥 Contributors
Developed as a solo academic project demonstrating file system implementation concepts.
