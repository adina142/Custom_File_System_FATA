#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>

/*
 * CUSTOM FILE SYSTEM IMPLEMENTATION USING FAT
 * 
 * DESIGN DOCUMENTATION:
 * 
 * File System Structure:
 * ---------------------
 * 1. Boot Sector (1 block): Contains metadata about the file system
 * 2. FAT Table (128 blocks): File Allocation Table for tracking file blocks
 * 3. Root Directory (1 block): Directory entries for root directory
 * 4. Data Blocks (remaining): Actual file data storage
 * 
 * Key Design Decisions:
 * - Fixed block size of 1KB for simplicity and performance
 * - Two-level directory structure (root + subdirectories)
 * - FAT entries use 16-bit integers (supports up to 65536 blocks)
 * - Directory entries contain metadata and first block pointer
 * - Free blocks marked with 0xFFFF in FAT
 * - End of file marked with 0xFFFE in FAT
 * 
 * Challenges Addressed:
 * - Efficient block allocation/deallocation using FAT
 * - Handling variable file names up to 64 bytes
 * - Managing directory hierarchy within fixed constraints
 * - Ensuring data integrity through proper error handling
 */

#define BLOCK_SIZE 1024
#define TOTAL_DISK_SIZE (64 * 1024 * 1024)  // 64 MB
#define MAX_BLOCKS (TOTAL_DISK_SIZE / BLOCK_SIZE)
#define MAX_FILES_IN_DIR 128
#define MAX_FILENAME_SIZE 64
#define MAX_FILE_BLOCKS 128
#define FAT_ENTRY_FREE 0xFFFF
#define FAT_ENTRY_EOF 0xFFFE
#define FAT_ENTRY_BAD 0xFFFD

// File types
#define TYPE_FILE 0
#define TYPE_DIRECTORY 1

// File system metadata structure (Boot Sector)
typedef struct {
    char signature[8];        // File system signature "MYFATFS"
    uint32_t total_blocks;    // Total number of blocks
    uint32_t fat_blocks;      // Number of blocks for FAT
    uint32_t root_dir_block;  // Root directory block number
    uint32_t data_start_block;// First data block number
    uint16_t block_size;      // Block size in bytes
    uint8_t fat_copies;       // Number of FAT copies (we use 1)
    char volume_label[16];    // Volume label
    uint32_t created_time;    // File system creation time
} BootSector;

// Directory entry structure
typedef struct {
    char filename[MAX_FILENAME_SIZE];
    uint32_t file_size;
    uint16_t first_block;
    uint8_t type;            // FILE or DIRECTORY
    uint32_t created_time;
    uint32_t modified_time;
    uint8_t attributes;      // Reserved for future use
} DirectoryEntry;

// Directory structure (occupies one block)
typedef struct {
    DirectoryEntry entries[MAX_FILES_IN_DIR];
    uint16_t entry_count;
} Directory;

// File System context
typedef struct {
    FILE* disk_file;
    BootSector boot_sector;
    uint16_t* fat_table;
    uint32_t current_dir_block;
    char current_path[256];
} FileSystem;

// Global file system instance
FileSystem fs;

// Function prototypes
int create_partition(const char* filename);
int format_partition(const char* filename);
int mount_partition(const char* filename);
void unmount_partition();
uint16_t allocate_block();
void free_blocks(uint16_t first_block);
int find_free_directory_entry(uint32_t dir_block);
int find_file_in_directory(uint32_t dir_block, const char* filename);
int read_block(uint32_t block_num, void* buffer);
int write_block(uint32_t block_num, const void* buffer);

// Low-level block operations
int read_block(uint32_t block_num, void* buffer) {
    if (!fs.disk_file || block_num >= fs.boot_sector.total_blocks) {
        return -1;
    }
    
    fseek(fs.disk_file, block_num * BLOCK_SIZE, SEEK_SET);
    return fread(buffer, BLOCK_SIZE, 1, fs.disk_file) == 1 ? 0 : -1;
}

int write_block(uint32_t block_num, const void* buffer) {
    if (!fs.disk_file || block_num >= fs.boot_sector.total_blocks) {
        return -1;
    }
    
    fseek(fs.disk_file, block_num * BLOCK_SIZE, SEEK_SET);
    return fwrite(buffer, BLOCK_SIZE, 1, fs.disk_file) == 1 ? 0 : -1;
}

// FAT table operations
uint16_t allocate_block() {
    for (uint32_t i = fs.boot_sector.data_start_block; i < fs.boot_sector.total_blocks; i++) {
        if (fs.fat_table[i] == FAT_ENTRY_FREE) {
            fs.fat_table[i] = FAT_ENTRY_EOF;
            
            // Write updated FAT to disk
            for (uint32_t j = 0; j < fs.boot_sector.fat_blocks; j++) {
                write_block(1 + j, (uint8_t*)fs.fat_table + j * BLOCK_SIZE);
            }
            
            return i;
        }
    }
    return FAT_ENTRY_FREE; // No free blocks
}

void free_blocks(uint16_t first_block) {
    uint16_t current_block = first_block;
    
    while (current_block != FAT_ENTRY_EOF && current_block != FAT_ENTRY_FREE) {
        uint16_t next_block = fs.fat_table[current_block];
        fs.fat_table[current_block] = FAT_ENTRY_FREE;
        current_block = next_block;
    }
    
    // Write updated FAT to disk
    for (uint32_t i = 0; i < fs.boot_sector.fat_blocks; i++) {
        write_block(1 + i, (uint8_t*)fs.fat_table + i * BLOCK_SIZE);
    }
}

// Directory operations
int find_free_directory_entry(uint32_t dir_block) {
    Directory dir;
    if (read_block(dir_block, &dir) != 0) {
        return -1;
    }
    
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir.entries[i].filename[0] == '\0') {
            return i;
        }
    }
    return -1; // Directory full
}

int find_file_in_directory(uint32_t dir_block, const char* filename) {
    Directory dir;
    if (read_block(dir_block, &dir) != 0) {
        return -1;
    }
    
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir.entries[i].filename[0] != '\0' && 
            strcmp(dir.entries[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1; // File not found
}


int create_partition(const char* filename) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Error: Cannot create file '%s'\n", filename);
        return -1;
    }
    
    // Create empty file of TOTAL_DISK_SIZE by writing zeros
    printf("Creating 64MB disk file...\n");
    uint8_t* zeros = calloc(BLOCK_SIZE, 1);
    if (!zeros) {
        fclose(file);
        return -1;
    }
    
    // Write zeros in blocks to create the full disk size
    for (int i = 0; i < TOTAL_DISK_SIZE / BLOCK_SIZE; i++) {
        if (fwrite(zeros, BLOCK_SIZE, 1, file) != 1) {
            free(zeros);
            fclose(file);
            return -1;
        }
    }
    
    free(zeros);
    fclose(file);
    printf("Disk file created successfully\n");
    
    return format_partition(filename);
}


int format_partition(const char* filename) {
    // Open the file directly for formatting
    FILE* file = fopen(filename, "rb+");
    if (!file) {
        printf("Error: Cannot open file '%s' for formatting\n", filename);
        return -1;
    }
    
    printf("Formatting file system...\n");
    
    // Initialize boot sector
    BootSector boot_sector;
    memset(&boot_sector, 0, sizeof(BootSector));
    strcpy(boot_sector.signature, "MYFATFS");
    boot_sector.total_blocks = MAX_BLOCKS;
    boot_sector.block_size = BLOCK_SIZE;
    boot_sector.fat_copies = 1;
    boot_sector.created_time = (uint32_t)time(NULL);
    strcpy(boot_sector.volume_label, "MYVOLUME");
    
    // Calculate FAT size (each FAT entry is 2 bytes)
    uint32_t fat_entries = MAX_BLOCKS;
    boot_sector.fat_blocks = (fat_entries * 2 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    boot_sector.root_dir_block = 1 + boot_sector.fat_blocks;
    boot_sector.data_start_block = boot_sector.root_dir_block + 1;
    
    // Write boot sector to block 0
    fseek(file, 0, SEEK_SET);
    fwrite(&boot_sector, sizeof(BootSector), 1, file);
    
    // Initialize FAT table
    uint16_t* fat_table = calloc(MAX_BLOCKS, sizeof(uint16_t));
    for (uint32_t i = 0; i < MAX_BLOCKS; i++) {
        fat_table[i] = FAT_ENTRY_FREE;
    }
    
    // Mark system blocks as used
    for (uint32_t i = 0; i < boot_sector.data_start_block; i++) {
        fat_table[i] = FAT_ENTRY_BAD;
    }
    
    // Write FAT table
    for (uint32_t i = 0; i < boot_sector.fat_blocks; i++) {
        fseek(file, (1 + i) * BLOCK_SIZE, SEEK_SET);
        fwrite((uint8_t*)fat_table + i * BLOCK_SIZE, BLOCK_SIZE, 1, file);
    }
    
    // Initialize root directory - PROPERLY CLEAR ALL ENTRIES
    Directory root_dir;
    memset(&root_dir, 0, sizeof(Directory));  // This sets ALL bytes to zero
    root_dir.entry_count = 0;

    // Explicitly clear all directory entries to be sure
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        memset(&root_dir.entries[i], 0, sizeof(DirectoryEntry));
    }

    fseek(file, boot_sector.root_dir_block * BLOCK_SIZE, SEEK_SET);
    fwrite(&root_dir, sizeof(Directory), 1, file);
    
    free(fat_table);
    fclose(file);
    
    printf("Format completed successfully!\n");
    printf(" - Total blocks: %u\n", boot_sector.total_blocks);
    printf(" - FAT blocks: %u\n", boot_sector.fat_blocks);
    printf(" - Root directory at block: %u\n", boot_sector.root_dir_block);
    printf(" - Data starts at block: %u\n", boot_sector.data_start_block);
    
    return 0;
}

int mount_partition(const char* filename) {
    // Close any previously mounted partition
    if (fs.disk_file) {
        fclose(fs.disk_file);
    }
    if (fs.fat_table) {
        free(fs.fat_table);
    }

    fs.disk_file = fopen(filename, "rb+");
    if (!fs.disk_file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return -1;
    }
    
    // Read boot sector
    fseek(fs.disk_file, 0, SEEK_SET);
    if (fread(&fs.boot_sector, sizeof(BootSector), 1, fs.disk_file) != 1) {
        printf("Error: Cannot read boot sector\n");
        fclose(fs.disk_file);
        fs.disk_file = NULL;
        return -1;
    }
    
    // Verify signature
    if (strcmp(fs.boot_sector.signature, "MYFATFS") != 0) {
        printf("Error: Not a valid MYFATFS partition\n");
        printf("Signature found: '%.8s'\n", fs.boot_sector.signature);
        fclose(fs.disk_file);
        fs.disk_file = NULL;
        return -1;
    }
    
    printf("Boot sector loaded successfully\n");
    printf("Volume: %s, Blocks: %u, Block Size: %u\n", 
           fs.boot_sector.volume_label, 
           fs.boot_sector.total_blocks,
           fs.boot_sector.block_size);
    
    // Allocate and read FAT table
    fs.fat_table = malloc(MAX_BLOCKS * sizeof(uint16_t));
    if (!fs.fat_table) {
        printf("Error: Cannot allocate memory for FAT table\n");
        fclose(fs.disk_file);
        fs.disk_file = NULL;
        return -1;
    }
    
    // Read FAT table blocks
    for (uint32_t i = 0; i < fs.boot_sector.fat_blocks; i++) {
        fseek(fs.disk_file, (1 + i) * BLOCK_SIZE, SEEK_SET);
        if (fread((uint8_t*)fs.fat_table + i * BLOCK_SIZE, BLOCK_SIZE, 1, fs.disk_file) != 1) {
            printf("Error: Cannot read FAT block %u\n", i);
            free(fs.fat_table);
            fclose(fs.disk_file);
            fs.disk_file = NULL;
            return -1;
        }
    }
    
    printf("FAT table loaded (%u blocks)\n", fs.boot_sector.fat_blocks);
    
    fs.current_dir_block = fs.boot_sector.root_dir_block;
    strcpy(fs.current_path, "/");
    
    printf("Partition mounted successfully at %s\n", fs.current_path);
    return 0;
}

void unmount_partition() {
    if (fs.disk_file) {
        fclose(fs.disk_file);
        fs.disk_file = NULL;
    }
    if (fs.fat_table) {
        free(fs.fat_table);
        fs.fat_table = NULL;
    }
}

// File operations
int create_file(const char* filename) {
    if (strlen(filename) >= MAX_FILENAME_SIZE) {
        printf("Filename too long\n");
        return -1;
    }
    
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    // Check if file already exists
    if (find_file_in_directory(fs.current_dir_block, filename) >= 0) {
        printf("File already exists\n");
        return -1;
    }
    
    int entry_index = find_free_directory_entry(fs.current_dir_block);
    if (entry_index < 0) {
        printf("Directory full\n");
        return -1;
    }
    
    // Create directory entry
    DirectoryEntry* entry = &dir.entries[entry_index];
    strcpy(entry->filename, filename);
    entry->file_size = 0;
    entry->first_block = FAT_ENTRY_EOF;
    entry->type = TYPE_FILE;
    entry->created_time = (uint32_t)time(NULL);
    entry->modified_time = entry->created_time;
    entry->attributes = 0;
    
    dir.entry_count++;
    
    // Write directory back to disk
    if (write_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    printf("File '%s' created successfully\n", filename);
    return 0;
}

int delete_file(const char* filename) {
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    int entry_index = find_file_in_directory(fs.current_dir_block, filename);
    if (entry_index < 0) {
        printf("File not found\n");
        return -1;
    }
    
    DirectoryEntry* entry = &dir.entries[entry_index];
    if (entry->type != TYPE_FILE) {
        printf("Not a file\n");
        return -1;
    }
    
    // Free file blocks
    if (entry->first_block != FAT_ENTRY_EOF) {
        free_blocks(entry->first_block);
    }
    
    // Clear directory entry
    memset(entry, 0, sizeof(DirectoryEntry));
    dir.entry_count--;
    
    // Write directory back to disk
    if (write_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    printf("File '%s' deleted successfully\n", filename);
    return 0;
}

int read_file(const char* filename) {
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    int entry_index = find_file_in_directory(fs.current_dir_block, filename);
    if (entry_index < 0) {
        printf("File not found\n");
        return -1;
    }
    
    DirectoryEntry* entry = &dir.entries[entry_index];
    if (entry->type != TYPE_FILE) {
        printf("Not a file\n");
        return -1;
    }
    
    if (entry->file_size == 0) {
        printf("File is empty\n");
        return 0;
    }
    
    // Read file data
    uint16_t current_block = entry->first_block;
    uint32_t bytes_remaining = entry->file_size;
    uint8_t buffer[BLOCK_SIZE];
    
    printf("File content (%u bytes):\n", entry->file_size);
    
    while (current_block != FAT_ENTRY_EOF && bytes_remaining > 0) {
        if (read_block(current_block, buffer) != 0) {
            printf("Error reading block\n");
            return -1;
        }
        
        uint32_t bytes_to_print = bytes_remaining > BLOCK_SIZE ? BLOCK_SIZE : bytes_remaining;
        fwrite(buffer, 1, bytes_to_print, stdout);
        bytes_remaining -= bytes_to_print;
        current_block = fs.fat_table[current_block];
    }
    
    printf("\n");
    return 0;
}

int write_file(const char* filename, const char* data) {
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    int entry_index = find_file_in_directory(fs.current_dir_block, filename);
    if (entry_index < 0) {
        printf("File not found\n");
        return -1;
    }
    
    DirectoryEntry* entry = &dir.entries[entry_index];
    if (entry->type != TYPE_FILE) {
        printf("Not a file\n");
        return -1;
    }
    
    uint32_t data_size = strlen(data);
    if (data_size > MAX_FILE_BLOCKS * BLOCK_SIZE) {
        printf("File too large\n");
        return -1;
    }
    
    // Free existing blocks if any
    if (entry->first_block != FAT_ENTRY_EOF) {
        free_blocks(entry->first_block);
    }
    
    // Allocate new blocks and write data
    uint32_t bytes_remaining = data_size;
    const uint8_t* data_ptr = (const uint8_t*)data;
    uint16_t first_block = FAT_ENTRY_EOF;
    uint16_t prev_block = FAT_ENTRY_EOF;
    
    while (bytes_remaining > 0) {
        uint16_t new_block = allocate_block();
        if (new_block == FAT_ENTRY_FREE) {
            printf("No free space available\n");
            // Free any allocated blocks so far
            if (first_block != FAT_ENTRY_EOF) {
                free_blocks(first_block);
            }
            return -1;
        }
        
        if (first_block == FAT_ENTRY_EOF) {
            first_block = new_block;
        }
        
        if (prev_block != FAT_ENTRY_EOF) {
            fs.fat_table[prev_block] = new_block;
        }
        
        uint32_t bytes_to_write = bytes_remaining > BLOCK_SIZE ? BLOCK_SIZE : bytes_remaining;
        uint8_t block_data[BLOCK_SIZE];
        memset(block_data, 0, BLOCK_SIZE);
        memcpy(block_data, data_ptr, bytes_to_write);
        
        if (write_block(new_block, block_data) != 0) {
            printf("Error writing block\n");
            free_blocks(first_block);
            return -1;
        }
        
        data_ptr += bytes_to_write;
        bytes_remaining -= bytes_to_write;
        prev_block = new_block;
    }
    
    if (prev_block != FAT_ENTRY_EOF) {
        fs.fat_table[prev_block] = FAT_ENTRY_EOF;
    }
    
    // Update directory entry
    entry->first_block = first_block;
    entry->file_size = data_size;
    entry->modified_time = (uint32_t)time(NULL);
    
    // Write directory back to disk
    if (write_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    // Write updated FAT
    for (uint32_t i = 0; i < fs.boot_sector.fat_blocks; i++) {
        write_block(1 + i, (uint8_t*)fs.fat_table + i * BLOCK_SIZE);
    }
    
    printf("Written %u bytes to file '%s'\n", data_size, filename);
    return 0;
}

int truncate_file(const char* filename, uint32_t new_size) {
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    int entry_index = find_file_in_directory(fs.current_dir_block, filename);
    if (entry_index < 0) {
        printf("File not found\n");
        return -1;
    }
    
    DirectoryEntry* entry = &dir.entries[entry_index];
    if (entry->type != TYPE_FILE) {
        printf("Not a file\n");
        return -1;
    }
    
    if (new_size > entry->file_size) {
        printf("New size larger than current size - use write to extend file\n");
        return -1;
    }
    
    if (new_size == entry->file_size) {
        return 0; // No change needed
    }
    
    // Calculate blocks needed for new size
    uint32_t blocks_needed = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // Traverse FAT to find where to truncate
    uint16_t current_block = entry->first_block;
    uint16_t prev_block = FAT_ENTRY_EOF;
    
    for (uint32_t i = 0; i < blocks_needed && current_block != FAT_ENTRY_EOF; i++) {
        prev_block = current_block;
        current_block = fs.fat_table[current_block];
    }
    
    // Free remaining blocks
    if (current_block != FAT_ENTRY_EOF) {
        free_blocks(current_block);
        if (prev_block != FAT_ENTRY_EOF) {
            fs.fat_table[prev_block] = FAT_ENTRY_EOF;
        }
    }
    
    // Update directory entry
    entry->file_size = new_size;
    entry->modified_time = (uint32_t)time(NULL);
    
    // Write directory back to disk
    if (write_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    printf("File '%s' truncated to %u bytes\n", filename, new_size);
    return 0;
}

// Directory operations
int create_directory(const char* dirname) {
    if (strlen(dirname) >= MAX_FILENAME_SIZE) {
        printf("Directory name too long\n");
        return -1;
    }
    
    Directory current_dir;
    if (read_block(fs.current_dir_block, &current_dir) != 0) {
        return -1;
    }
    
    // Check if directory already exists
    if (find_file_in_directory(fs.current_dir_block, dirname) >= 0) {
        printf("Directory already exists\n");
        return -1;
    }
    
    int entry_index = find_free_directory_entry(fs.current_dir_block);
    if (entry_index < 0) {
        printf("Directory full\n");
        return -1;
    }
    
    // Allocate block for new directory
    uint16_t dir_block = allocate_block();
    if (dir_block == FAT_ENTRY_FREE) {
        printf("No free space available\n");
        return -1;
    }
    
    // Initialize new directory
    Directory new_dir;
    memset(&new_dir, 0, sizeof(Directory));
    new_dir.entry_count = 0;
    
    // Add "." entry (self)
    strcpy(new_dir.entries[0].filename, ".");
    new_dir.entries[0].first_block = dir_block;
    new_dir.entries[0].type = TYPE_DIRECTORY;
    new_dir.entries[0].created_time = (uint32_t)time(NULL);
    new_dir.entries[0].modified_time = new_dir.entries[0].created_time;
    new_dir.entry_count++;
    
    // Add ".." entry (parent)
    strcpy(new_dir.entries[1].filename, "..");
    new_dir.entries[1].first_block = fs.current_dir_block;
    new_dir.entries[1].type = TYPE_DIRECTORY;
    new_dir.entries[1].created_time = (uint32_t)time(NULL);
    new_dir.entries[1].modified_time = new_dir.entries[1].created_time;
    new_dir.entry_count++;
    
    // Write new directory to disk
    if (write_block(dir_block, &new_dir) != 0) {
        free_blocks(dir_block);
        return -1;
    }
    
    // Add directory entry to current directory
    DirectoryEntry* entry = &current_dir.entries[entry_index];
    strcpy(entry->filename, dirname);
    entry->first_block = dir_block;
    entry->type = TYPE_DIRECTORY;
    entry->created_time = (uint32_t)time(NULL);
    entry->modified_time = entry->created_time;
    current_dir.entry_count++;
    
    // Write current directory back to disk
    if (write_block(fs.current_dir_block, &current_dir) != 0) {
        return -1;
    }
    
    printf("Directory '%s' created successfully\n", dirname);
    return 0;
}

int list_directory() {
    Directory dir;
    if (read_block(fs.current_dir_block, &dir) != 0) {
        return -1;
    }
    
    printf("Contents of %s:\n", fs.current_path);
    printf("%-20s %-10s %-10s %s\n", "Name", "Type", "Size", "Modified");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir.entries[i].filename[0] != '\0') {
            printf("%-20s %-10s %-10u ", 
                   dir.entries[i].filename,
                   dir.entries[i].type == TYPE_FILE ? "FILE" : "DIR",
                   dir.entries[i].file_size);
            
            // Format time
            time_t mod_time = dir.entries[i].modified_time;
            struct tm* timeinfo = localtime(&mod_time);
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", timeinfo);
            printf("%s\n", time_str);
        }
    }
    
    return 0;
}

// Console interface
void print_help() {
    printf("\nAvailable commands:\n");
    printf("  format <filename>        - Create and format a new partition\n");
    printf("  mount <filename>         - Mount an existing partition\n");
    printf("  unmount                  - Unmount current partition\n");
    printf("  mkdir <dirname>          - Create a new directory\n");
    printf("  ls                       - List directory contents\n");
    printf("  create <filename>        - Create a new file\n");
    printf("  delete <filename>        - Delete a file\n");
    printf("  read <filename>          - Read and display file content\n");
    printf("  write <filename> <data>  - Write data to file\n");
    printf("  truncate <filename> <size> - Truncate file to specified size\n");
    printf("  help                     - Show this help message\n");
    printf("  exit                     - Exit the program\n");
}

void console_interface() {
    char command[256];
    char arg1[256];
    char arg2[1024];
    
    printf("Custom FAT File System Console\n");
    printf("Type 'help' for available commands\n");
    
    while (1) {
        printf("\n%s> ", fs.current_path);
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        // Parse command
        if (strlen(command) == 0) {
            continue;
        }
        
        if (strcmp(command, "exit") == 0) {
            break;
        }
        else if (strcmp(command, "help") == 0) {
            print_help();
        }
        else if (strncmp(command, "format ", 7) == 0) {
            if (sscanf(command, "format %255s", arg1) == 1) {
                if (create_partition(arg1) == 0) {
                    printf("Partition created and formatted successfully\n");
                } else {
                    printf("Failed to create partition\n");
                }
            } else {
                printf("Usage: format <filename>\n");
            }
        }
        else if (strncmp(command, "mount ", 6) == 0) {
            if (sscanf(command, "mount %255s", arg1) == 1) {
                if (mount_partition(arg1) == 0) {
                    printf("Partition mounted successfully\n");
                } else {
                    printf("Failed to mount partition\n");
                }
            } else {
                printf("Usage: mount <filename>\n");
            }
        }
        else if (strcmp(command, "unmount") == 0) {
            unmount_partition();
            printf("Partition unmounted\n");
        }
        else if (strncmp(command, "mkdir ", 6) == 0) {
            if (sscanf(command, "mkdir %255s", arg1) == 1) {
                create_directory(arg1);
            } else {
                printf("Usage: mkdir <dirname>\n");
            }
        }
        else if (strcmp(command, "ls") == 0) {
            list_directory();
        }
        else if (strncmp(command, "create ", 7) == 0) {
            if (sscanf(command, "create %255s", arg1) == 1) {
                create_file(arg1);
            } else {
                printf("Usage: create <filename>\n");
            }
        }
        else if (strncmp(command, "delete ", 7) == 0) {
            if (sscanf(command, "delete %255s", arg1) == 1) {
                delete_file(arg1);
            } else {
                printf("Usage: delete <filename>\n");
            }
        }
        else if (strncmp(command, "read ", 5) == 0) {
            if (sscanf(command, "read %255s", arg1) == 1) {
                read_file(arg1);
            } else {
                printf("Usage: read <filename>\n");
            }
        }
        else if (strncmp(command, "write ", 6) == 0) {
            if (sscanf(command, "write %255s %1023[^\n]", arg1, arg2) == 2) {
                write_file(arg1, arg2);
            } else {
                printf("Usage: write <filename> <data>\n");
            }
        }
        else if (strncmp(command, "truncate ", 9) == 0) {
            unsigned int size;
            if (sscanf(command, "truncate %255s %u", arg1, &size) == 2) {
                truncate_file(arg1, size);
            } else {
                printf("Usage: truncate <filename> <size>\n");
            }
        }
        else {
            printf("Unknown command: %s\n", command);
            printf("Type 'help' for available commands\n");
        }
    }
}

int main() {
    printf("Custom FAT File System Implementation\n");
    printf("=====================================\n");
    
    // Initialize file system
    memset(&fs, 0, sizeof(FileSystem));
    
    // Start console interface
    console_interface();
    
    // Cleanup
    unmount_partition();
    
    printf("Goodbye!\n");
    return 0;
}