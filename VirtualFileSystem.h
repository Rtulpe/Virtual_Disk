//
// Created by rt on 5/27/25.
//

#ifndef VIRTUALFILESYSTEM_H
#define VIRTUALFILESYSTEM_H
#include    <string>
#include    <cstdint>
#include    <fstream>
#include    <vector>

static constexpr uint32_t MAX_FILES = 64;                       // Limit of files in the virtual file system
static constexpr uint32_t BLOCK_SIZE = 512;                     // Block size in bytes
static constexpr uint32_t DEFAULT_DISK_SIZE = 10 * 1024 * 1024; // File system default size in bytes (10 MB)
static constexpr char FS_NAME[8] = "TTvfs01";                   // File system name

// Superblock stored in block 0
// This contains metadata about the file system
// Generally, all UNIX systems have one, it's like
// a header or a table of contents
#pragma pack(push, 1) // This avoids padding by the compiler
struct SuperBlock {
    char fsName[8];             // So-called "magic"
    uint32_t blockSize;         // Size of a block in bytes
    uint32_t totalBlocks;       // Total number of blocks in the file system
    uint32_t totalDirEntries;   // Max number of directory entries
    uint32_t dirStartBlock;     // Block index where directory starts
    uint32_t dirBlockCount;     // Number of blocks used by the directory
    uint32_t fatStartBlock;     // Block index where the FAT starts
    uint32_t fatBlockCount;     // Number of blocks used by the FAT
    uint32_t dataStartBlock;    // Block index where data starts
};
#pragma pack(pop)

// Directory entry structure
#pragma pack(push, 1
struct DirEntry {
    char name[32];          // File name (null-terminated, thus can vary in length)
    uint64_t size;          // File size in bytes
    time_t created;         // Creation timestamp
    char type;              // File type
    uint32_t firstBlock;    // Index of first data block
};
#pragma pack(pop)

class VirtualFileSystem {
public:
    explicit VirtualFileSystem(const std::string &diskPath); // Not sure what explicit does, but CLANG recommends
    ~VirtualFileSystem();

    // Perform formatting and create a new virtual disk
    // Default size is assumed to be 10MB if not given
    bool createDisk(uint32_t diskSize = DEFAULT_DISK_SIZE);

    // Load VD
    bool loadDisk();

    // File operations on VD
    bool copyFromHost(const std::string &hostFile);                             // HOST -> VD
    bool copyToHost(const std::string &fileName, const std::string &destPath);  // VD -> HOST
    bool deleteFile(const std::string &fileName);                               // Remove file from VD
    void listFiles() const;                                                     //Basically "ls"
    void showMap() const;                                                       //Show block occupancy map
    bool removeDisk();                                                          //Remove VD file

private:
    std::string diskPath;               // Path to the disk file
    std::fstream disk;                  // File stream for disk
    SuperBlock sb;                      // METAINFO
    std::vector<DirEntry> directory;    // Dir table
    std::vector<int32_t> FAT;           // File Allocation Table

    // Internal helper functions
    bool readSuperblock();
    bool writeSuperblock();
    bool readDirectory();
    bool writeDirectory();
    bool readFAT();
    bool writeFAT();

    bool findFreeBlocks(uint32_t count, std::vector<int32_t> &blocks) const;
    int findDirectoryEntry(const std::string &name) const;
};

#endif //VIRTUALFILESYSTEM_H
