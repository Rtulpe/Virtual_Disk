// VirtualFileSystem.cpp
#include "VirtualFileSystem.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <cstdio>      // for remove()
#include <algorithm>

using namespace std;

// Special FAT markers
static const int32_t FAT_FREE = 0;      // Block is free
static const int32_t FAT_EOF = -1;      // End-of-file chain
static const int32_t FAT_RESERVED = -2; // Reserved for metadata (not usable by files)

// Constructor: initialize internal structures
VirtualFileSystem::VirtualFileSystem(const std::string &path)
    : diskPath(path), directory(MAX_FILES), FAT() {
    // Reserve directory entries; FAT will be sized on disk load/create
}

// Destructor: close disk file if open
VirtualFileSystem::~VirtualFileSystem() {
    if (disk.is_open()) {
        disk.close();
    }
}

// Create a new virtual disk file and initialize filesystem structures
bool VirtualFileSystem::createDisk(uint32_t diskSize) {
    // Adjust disk size to a multiple of BLOCK_SIZE
    if (diskSize == 0) {
        diskSize = DEFAULT_DISK_SIZE;
    }
    if (diskSize % BLOCK_SIZE != 0) {
        diskSize = ((diskSize / BLOCK_SIZE) + 1) * BLOCK_SIZE;
    }

    // Create and size the file
    ofstream out(diskPath, ios::binary | ios::trunc);
    if (!out) {
        cerr << "Error: Cannot create disk file '" << diskPath << "'\n";
        return false;
    }
    // Seek to last byte and write a zero to allocate space
    out.seekp(diskSize - 1);
    char zero = '\0';
    out.write(&zero, 1);
    out.close();

    // Open the file for read/write access
    disk.open(diskPath, ios::binary | ios::in | ios::out);
    if (!disk) {
        cerr << "Error: Cannot open disk file after creation\n";
        return false;
    }

    // Initialize superblock
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.fsName, FS_NAME, sizeof(sb.fsName));
    sb.blockSize      = BLOCK_SIZE;
    sb.totalBlocks    = diskSize / BLOCK_SIZE;
    sb.totalDirEntries= MAX_FILES;
    sb.dirStartBlock  = 1;
    // Calculate blocks for directory
    uint32_t dirBytes = MAX_FILES * sizeof(DirEntry);
    sb.dirBlockCount  = (dirBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.fatStartBlock  = sb.dirStartBlock + sb.dirBlockCount;
    // Calculate blocks for FAT (one int32 per block)
    uint32_t fatBytes = sb.totalBlocks * sizeof(int32_t);
    sb.fatBlockCount  = (fatBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.dataStartBlock = sb.fatStartBlock + sb.fatBlockCount;

    // Write superblock to disk (block 0)
    writeSuperblock();

    // Initialize and write empty directory entries
    for (auto &entry : directory) {
        memset(&entry, 0, sizeof(DirEntry));
    }
    writeDirectory();

    // Initialize FAT: mark metadata blocks reserved and others free
    FAT.assign(sb.totalBlocks, FAT_FREE);
    for (uint32_t i = 0; i < sb.dataStartBlock; i++) {
        FAT[i] = FAT_RESERVED;
    }
    writeFAT();

    disk.close();
    cout << "Virtual disk '" << diskPath << "' created ("
         << diskSize << " bytes, " << sb.totalBlocks << " blocks).\n";
    return true;
}

// Load an existing virtual disk (read superblock, directory, FAT into memory)
bool VirtualFileSystem::loadDisk() {
    disk.open(diskPath, ios::binary | ios::in | ios::out);
    if (!disk) {
        cerr << "Error: Cannot open virtual disk '" << diskPath << "'\n";
        return false;
    }
    if (!readSuperblock()) {
        cerr << "Error: Invalid or corrupt superblock\n";
        disk.close();
        return false;
    }
    readDirectory();
    readFAT();
    return true;
}

// Read superblock from disk
bool VirtualFileSystem::readSuperblock() {
    disk.seekg(0);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(sb));
    // Verify filesystem identifier
    if (strncmp(sb.fsName, FS_NAME, strlen(FS_NAME)) != 0) {
        return false;
    }
    return true;
}

// Write superblock to disk (block 0)
bool VirtualFileSystem::writeSuperblock() {
    disk.seekp(0);
    disk.write(reinterpret_cast<char*>(&sb), sizeof(sb));
    // Pad remaining bytes of block with zeros, if any
    if (sizeof(sb) < BLOCK_SIZE) {
        vector<char> pad(BLOCK_SIZE - sizeof(sb), 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Read directory entries from disk
bool VirtualFileSystem::readDirectory() {
    directory.assign(MAX_FILES, DirEntry());
    disk.seekg(sb.dirStartBlock * BLOCK_SIZE);
    disk.read(reinterpret_cast<char*>(directory.data()), MAX_FILES * sizeof(DirEntry));
    return disk.good();
}

// Write directory entries to disk
bool VirtualFileSystem::writeDirectory() {
    disk.seekp(sb.dirStartBlock * BLOCK_SIZE);
    disk.write(reinterpret_cast<char*>(directory.data()), MAX_FILES * sizeof(DirEntry));
    // Pad the rest of directory blocks
    uint32_t usedBytes = MAX_FILES * sizeof(DirEntry);
    uint32_t totalBytes= sb.dirBlockCount * BLOCK_SIZE;
    if (usedBytes < totalBytes) {
        vector<char> pad(totalBytes - usedBytes, 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Read FAT from disk
bool VirtualFileSystem::readFAT() {
    FAT.assign(sb.totalBlocks, FAT_FREE);
    disk.seekg(sb.fatStartBlock * BLOCK_SIZE);
    disk.read(reinterpret_cast<char*>(FAT.data()), sb.totalBlocks * sizeof(int32_t));
    return disk.good();
}

// Write FAT to disk
bool VirtualFileSystem::writeFAT() {
    disk.seekp(sb.fatStartBlock * BLOCK_SIZE);
    disk.write(reinterpret_cast<char*>(FAT.data()), sb.totalBlocks * sizeof(int32_t));
    // Pad the rest of FAT blocks
    uint32_t usedBytes = sb.totalBlocks * sizeof(int32_t);
    uint32_t totalBytes= sb.fatBlockCount * BLOCK_SIZE;
    if (usedBytes < totalBytes) {
        vector<char> pad(totalBytes - usedBytes, 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Find 'count' free blocks in FAT; return true if found and fill 'blocks' vector
bool VirtualFileSystem::findFreeBlocks(uint32_t count, vector<int32_t> &blocks) const {
    blocks.clear();
    for (uint32_t i = sb.dataStartBlock; i < sb.totalBlocks && blocks.size() < count; ++i) {
        if (FAT[i] == FAT_FREE) {
            blocks.push_back(i);
        }
    }
    return (blocks.size() == count);
}

// Find directory entry index by file name; return -1 if not found
int VirtualFileSystem::findDirectoryEntry(const string &name) const {
    for (int i = 0; i < (int)directory.size(); ++i) {
        if (directory[i].name[0] != '\0' && name == directory[i].name) {
            return i;
        }
    }
    return -1;
}

// Copy a host file into the virtual disk
bool VirtualFileSystem::copyFromHost(const std::string &hostFile) {
    // Determine file name (strip path)
    size_t pos = hostFile.find_last_of("/\\");
    string fname = (pos == string::npos ? hostFile : hostFile.substr(pos+1));

    // Check if file already exists in VFS
    if (findDirectoryEntry(fname) >= 0) {
        cerr << "Error: File '" << fname << "' already exists in virtual disk\n";
        return false;
    }
    // Check directory capacity
    int freeSlot = -1;
    for (int i = 0; i < MAX_FILES; ++i) {
        if (directory[i].name[0] == '\0') { freeSlot = i; break; }
    }
    if (freeSlot < 0) {
        cerr << "Error: Directory is full (max " << MAX_FILES << " files)\n";
        return false;
    }

    // Open host file
    ifstream in(hostFile, ios::binary | ios::ate);
    if (!in) {
        cerr << "Error: Cannot open host file '" << hostFile << "'\n";
        return false;
    }
    streamsize fileSize = in.tellg();
    in.seekg(0, ios::beg);
    if (fileSize <= 0) {
        cerr << "Error: Host file is empty or unreadable\n";
        in.close();
        return false;
    }

    // Calculate blocks needed
    uint32_t blocksNeeded = (uint32_t)((fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE);
    vector<int32_t> blocks;
    if (!findFreeBlocks(blocksNeeded, blocks)) {
        cerr << "Error: Not enough free space on virtual disk\n";
        in.close();
        return false;
    }

    // Fill directory entry
    DirEntry &entry = directory[freeSlot];
    memset(&entry, 0, sizeof(DirEntry));
    strncpy(entry.name, fname.c_str(), sizeof(entry.name)-1);
    entry.size       = (uint64_t)fileSize;
    entry.created    = time(nullptr);
    entry.type       = 'F';
    entry.firstBlock = blocks[0];

    // Update FAT for the allocated blocks
    for (uint32_t i = 0; i < blocksNeeded; ++i) {
        int blk = blocks[i];
        if (i == blocksNeeded - 1) {
            FAT[blk] = FAT_EOF;
        } else {
            FAT[blk] = blocks[i+1];
        }
    }

    // Write file data into data blocks
    char buffer[BLOCK_SIZE];
    for (uint32_t i = 0; i < blocksNeeded; ++i) {
        int blk = blocks[i];
        disk.seekp((uint64_t)blk * BLOCK_SIZE);
        // Compute bytes to read for this block
        uint32_t bytesToRead = (uint32_t)min((streamsize)BLOCK_SIZE, fileSize - (streamsize)i * BLOCK_SIZE);
        in.read(buffer, bytesToRead);
        disk.write(buffer, bytesToRead);
        // Pad remainder of block with zeros if last block not full
        if (bytesToRead < BLOCK_SIZE) {
            vector<char> pad(BLOCK_SIZE - bytesToRead, 0);
            disk.write(pad.data(), pad.size());
        }
    }
    in.close();

    // Save updated metadata (directory and FAT)
    writeDirectory();
    writeFAT();

    cout << "Copied '" << fname << "' (" << fileSize << " bytes) to virtual disk.\n";
    return true;
}

// Copy a file from the virtual disk to host filesystem
bool VirtualFileSystem::copyToHost(const std::string &fileName, const std::string &destPath) {
    int idx = findDirectoryEntry(fileName);
    if (idx < 0) {
        cerr << "Error: File '" << fileName << "' not found in virtual disk\n";
        return false;
    }
    DirEntry &entry = directory[idx];
    // Determine output path
    string outPath = destPath.empty() ? fileName : destPath;

    ofstream out(outPath, ios::binary);
    if (!out) {
        cerr << "Error: Cannot create host file '" << outPath << "'\n";
        return false;
    }

    // Follow FAT chain and write blocks
    int32_t blk = entry.firstBlock;
    uint64_t remaining = entry.size;
    char buffer[BLOCK_SIZE];
    while (blk != FAT_EOF && remaining > 0) {
        disk.seekg((uint64_t)blk * BLOCK_SIZE);
        uint32_t toRead = (uint32_t)min((uint64_t)BLOCK_SIZE, remaining);
        disk.read(buffer, toRead);
        out.write(buffer, toRead);
        remaining -= toRead;
        blk = (blk < 0 ? FAT_EOF : FAT[blk]);
    }
    out.close();

    cout << "Copied '" << fileName << "' from virtual disk to '" << outPath << "'.\n";
    return true;
}

// Delete a file from the virtual disk
bool VirtualFileSystem::deleteFile(const std::string &fileName) {
    int idx = findDirectoryEntry(fileName);
    if (idx < 0) {
        cerr << "Error: File '" << fileName << "' not found in virtual disk\n";
        return false;
    }
    DirEntry &entry = directory[idx];
    // Free all blocks in the file's chain
    int32_t blk = entry.firstBlock;
    while (blk != FAT_EOF && blk != FAT_RESERVED) {
        int32_t next = FAT[blk];
        FAT[blk] = FAT_FREE;
        blk = next;
    }
    // Mark directory entry as unused
    entry.name[0] = '\0';
    entry.size = 0;
    entry.firstBlock = 0;
    writeDirectory();
    writeFAT();

    cout << "Deleted file '" << fileName << "' from virtual disk.\n";
    return true;
}

// List all files in the virtual disk directory
void VirtualFileSystem::listFiles() const {
    cout << left << setw(20) << "Name"
         << right << setw(10) << "Size" << "  "
         << left << "Created               Type\n";
    cout << string(20+10+2+19+6, '-') << "\n";
    bool any = false;
    for (const auto &entry : directory) {
        if (entry.name[0] == '\0') continue;
        any = true;
        // Format creation time
        std::tm *tm_info = std::localtime(&entry.created);
        char timestr[20];
        std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
        cout << left << setw(20) << entry.name
             << right << setw(10) << entry.size << "  "
             << left << timestr << "  "
             << entry.type << "\n";
    }
    if (!any) {
        cout << "(no files)\n";
    }
}

// Show the occupancy map of blocks on the virtual disk
void VirtualFileSystem::showMap() const {
    cout << "Block | Type       | Status\n";
    cout << "-----------------------------\n";
    for (uint32_t i = 0; i < sb.totalBlocks; ++i) {
        string type, status;
        if (i == 0) {
            type = "Superblock";
            status = "occupied";
        } else if (i >= sb.dirStartBlock && i < sb.dirStartBlock + sb.dirBlockCount) {
            type = "Directory";
            status = "occupied";
        } else if (i >= sb.fatStartBlock && i < sb.fatStartBlock + sb.fatBlockCount) {
            type = "FAT";
            status = "occupied";
        } else {
            // Data region
            if (FAT[i] == FAT_FREE) {
                type = "Free";
                status = "free";
            } else {
                // Find which file this block belongs to
                string fname = "";
                for (const auto &entry : directory) {
                    if (entry.name[0] == '\0') continue;
                    int32_t blk = entry.firstBlock;
                    while (blk != FAT_EOF && blk >= 0) {
                        if ((uint32_t)blk == i) {
                            fname = entry.name;
                            break;
                        }
                        blk = FAT[blk];
                    }
                    if (!fname.empty()) break;
                }
                if (!fname.empty()) {
                    type = "File(" + fname + ")";
                    status = "occupied";
                } else {
                    type = "Unknown";
                    status = "occupied";
                }
            }
        }
        cout << setw(5) << i << " | " << setw(10) << type << " | " << status << "\n";
    }
}

// Delete the virtual disk file
bool VirtualFileSystem::removeDisk() {
    if (disk.is_open()) {
        disk.close();
    }
    if (std::remove(diskPath.c_str()) != 0) {
        cerr << "Error: Could not delete disk '" << diskPath << "'\n";
        return false;
    }
    cout << "Deleted virtual disk '" << diskPath << "'.\n";
    return true;
}
