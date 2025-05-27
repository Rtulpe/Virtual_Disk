// VirtualFileSystem.cpp
#include "VirtualFileSystem.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <cstdio>      // for remove()
#include <algorithm>
#include <utility> // for std::move

using namespace std;

// Special FAT markers (constexpr was suggested by CLANG-Tidy)
static constexpr int32_t FAT_FREE = 0; // Block is free
static constexpr int32_t FAT_EOF = -1; // End-of-file chain
static constexpr int32_t FAT_RESERVED = -2; // Reserved for metadata (not usable by files)

// Constructor: initialize internal structures
VirtualFileSystem::VirtualFileSystem(std::string diskPath)
    : diskPath(std::move(diskPath)), directory(MAX_FILES) {
    // Reserve directory entries.
}

// Destructor: close disk file if open
VirtualFileSystem::~VirtualFileSystem() {
    if (disk.is_open()) {
        disk.close(); // Just close the fstream, nothing fancy
    }
}

// Create a new VD file and initialize filesystem structures
bool VirtualFileSystem::createDisk(uint32_t diskSize) {
    // Adjust disk size to a multiple of BLOCK_SIZE
    // So, if the user specified 1000 bytes, it will be rounded up to 1024
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
    constexpr char zero = '\0';
    out.write(&zero, 1);
    out.close();

    // Open the file for read/write access
    disk.open(diskPath, ios::binary | ios::in | ios::out);
    if (!disk) {
        cerr << "Error: Cannot open disk file after creation\n";
        return false;
    }

    // Initialize superblock
    memset(&sb, 0, sizeof(sb)); // Clear the superblock structure
    memcpy(sb.fsName, FS_NAME, sizeof(sb.fsName));
    sb.blockSize = BLOCK_SIZE;
    sb.totalBlocks = diskSize / BLOCK_SIZE;
    sb.totalDirEntries = MAX_FILES;
    sb.dirStartBlock = 1;
    // Calculate blocks for directory
    constexpr uint32_t dirBytes = MAX_FILES * sizeof(DirEntry);
    sb.dirBlockCount = (dirBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.fatStartBlock = sb.dirStartBlock + sb.dirBlockCount;
    // Calculate blocks for FAT (one int32 per block)
    const uint32_t fatBytes = sb.totalBlocks * sizeof(int32_t);
    sb.fatBlockCount = (fatBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.dataStartBlock = sb.fatStartBlock + sb.fatBlockCount;

    // Write superblock to disk (block 0)
    writeSuperblock();

    // Initialize and write empty directory entries
    for (auto &entry: directory) {
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
    disk.read(reinterpret_cast<char *>(&sb), sizeof(sb));
    // Verify filesystem identifier
    if (strncmp(sb.fsName, FS_NAME, strlen(FS_NAME)) != 0) {
        return false;
    }
    return true;
}

// Write superblock to disk (block 0)
bool VirtualFileSystem::writeSuperblock() {
    disk.seekp(0);
    disk.write(reinterpret_cast<char *>(&sb), sizeof(sb));
    // Pad remaining bytes of block with zeros, if any
    if constexpr (sizeof(sb) < BLOCK_SIZE) {
        const vector<char> pad(BLOCK_SIZE - sizeof(sb), 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Read directory entries from disk
bool VirtualFileSystem::readDirectory() {
    directory.assign(MAX_FILES, DirEntry());
    disk.seekg(sb.dirStartBlock * BLOCK_SIZE);
    disk.read(reinterpret_cast<char *>(directory.data()), MAX_FILES * sizeof(DirEntry));
    return disk.good();
}

// Write directory entries to disk
bool VirtualFileSystem::writeDirectory() {
    disk.seekp(sb.dirStartBlock * BLOCK_SIZE);
    disk.write(reinterpret_cast<char *>(directory.data()), MAX_FILES * sizeof(DirEntry));
    // Pad the rest of directory blocks
    constexpr uint32_t usedBytes = MAX_FILES * sizeof(DirEntry);
    if (const uint32_t totalBytes = sb.dirBlockCount * BLOCK_SIZE; usedBytes < totalBytes) {
        const vector<char> pad(totalBytes - usedBytes, 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Read FAT from disk
bool VirtualFileSystem::readFAT() {
    FAT.assign(sb.totalBlocks, FAT_FREE);
    disk.seekg(sb.fatStartBlock * BLOCK_SIZE);
    disk.read(reinterpret_cast<char *>(FAT.data()), sb.totalBlocks * sizeof(int32_t));
    return disk.good();
}

// Write FAT to disk
bool VirtualFileSystem::writeFAT() {
    disk.seekp(sb.fatStartBlock * BLOCK_SIZE);
    disk.write(reinterpret_cast<char *>(FAT.data()), sb.totalBlocks * sizeof(int32_t));
    // Pad the rest of FAT blocks
    const uint32_t usedBytes = sb.totalBlocks * sizeof(int32_t);
    if (const uint32_t totalBytes = sb.fatBlockCount * BLOCK_SIZE; usedBytes < totalBytes) {
        const vector<char> pad(totalBytes - usedBytes, 0);
        disk.write(pad.data(), pad.size());
    }
    return disk.good();
}

// Find 'count' free blocks in FAT; return true if found and fill 'blocks' vector
bool VirtualFileSystem::findFreeBlocks(const uint32_t count, vector<int32_t> &blocks) const {
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
    for (int i = 0; i < static_cast<int>(directory.size()); ++i) {
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
    string fname = (pos == string::npos ? hostFile : hostFile.substr(pos + 1));

    // Check if file already exists in VFS
    if (findDirectoryEntry(fname) >= 0) {
        cerr << "Error: File '" << fname << "' already exists in virtual disk\n";
        return false;
    }
    // Check directory capacity
    int freeSlot = -1;
    for (int i = 0; i < MAX_FILES; ++i) {
        if (directory[i].name[0] == '\0') {
            freeSlot = i;
            break;
        }
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
    auto blocksNeeded = static_cast<uint32_t>((fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE);
    vector<int32_t> blocks;
    if (!findFreeBlocks(blocksNeeded, blocks)) {
        cerr << "Error: Not enough free space on virtual disk\n";
        in.close();
        return false;
    }

    // Fill directory entry
    DirEntry &entry = directory[freeSlot];
    memset(&entry, 0, sizeof(DirEntry));
    strncpy(entry.name, fname.c_str(), sizeof(entry.name) - 1);
    entry.size = static_cast<uint64_t>(fileSize);
    entry.created = time(nullptr);
    entry.type = 'F'; // Just means file. This is a placeholder for future types, such as (sub)directories
    entry.firstBlock = blocks[0];

    // Update FAT for the allocated blocks
    for (uint32_t i = 0; i < blocksNeeded; ++i) {
        int blk = blocks[i];
        if (i == blocksNeeded - 1) {
            FAT[blk] = FAT_EOF;
        } else {
            FAT[blk] = blocks[i + 1];
        }
    }

    // Write file data into data blocks
    char buffer[BLOCK_SIZE];
    for (uint32_t i = 0; i < blocksNeeded; ++i) {
        int blk = blocks[i];
        disk.seekp((uint64_t) blk * BLOCK_SIZE);
        // Compute bytes to read for this block
        uint32_t bytesToRead = static_cast<uint32_t>(min(static_cast<streamsize>(BLOCK_SIZE),
                                                         fileSize - static_cast<streamsize>(i) * BLOCK_SIZE));
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
    const int idx = findDirectoryEntry(fileName);
    if (idx < 0) {
        cerr << "Error: File '" << fileName << "' not found in virtual disk\n";
        return false;
    }
    const DirEntry &entry = directory[idx];
    // Determine output path
    const string outPath = destPath.empty() ? fileName : destPath;

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
        disk.seekg((uint64_t) blk * BLOCK_SIZE);
        const uint32_t toRead = static_cast<uint32_t>(min(static_cast<uint64_t>(BLOCK_SIZE), remaining));
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
    const int idx = findDirectoryEntry(fileName);
    if (idx < 0) {
        cerr << "Error: File '" << fileName << "' not found in virtual disk\n";
        return false;
    }
    DirEntry &entry = directory[idx];
    // Free all blocks in the file's chain
    int32_t blk = entry.firstBlock;
    while (blk != FAT_EOF && blk != FAT_RESERVED) {
        const int32_t next = FAT[blk];
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
    cout << string(20 + 10 + 2 + 19 + 6, '-') << "\n";
    bool any = false;
    for (const auto &entry: directory) {
        if (entry.name[0] == '\0') continue;
        any = true;
        // Format creation time
        const std::tm *tm_info = std::localtime(&entry.created);
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
    cout << "Range            | Type           | Status\n";
    cout << "-----------------------------------------------\n";

    auto describe_block = [&](uint32_t i) -> pair<string, string> {
        if (i == 0) return {"Superblock", "occupied"};
        else if (i >= sb.dirStartBlock && i < sb.dirStartBlock + sb.dirBlockCount)
            return {"Directory", "occupied"};
        else if (i >= sb.fatStartBlock && i < sb.fatStartBlock + sb.fatBlockCount)
            return {"FAT", "occupied"};
        else {
            if (FAT[i] == FAT_FREE) return {"Free", "free"};
            else {
                string fname;
                for (const auto &entry: directory) {
                    if (entry.name[0] == '\0') continue;
                    int32_t blk = entry.firstBlock;
                    while (blk != FAT_EOF && blk >= 0) {
                        if (static_cast<uint32_t>(blk) == i) {
                            fname = entry.name;
                            break;
                        }
                        blk = FAT[blk];
                    }
                    if (!fname.empty()) break;
                }
                return !fname.empty()
                           ? make_pair("File(" + fname + ")", "occupied")
                           : make_pair("Unknown", "occupied");
            }
        }
    };

    uint32_t start = 0;
    auto [currType, currStatus] = describe_block(0);

    for (uint32_t i = 1; i < sb.totalBlocks; ++i) {
        if (auto [type, status] = describe_block(i); type != currType || status != currStatus) {
            cout << setw(4) << start << "-" << setw(4) << i - 1 << "        | "
                    << setw(13) << currType << " | " << currStatus << "\n";
            start = i;
            currType = type;
            currStatus = status;
        }
    }

    // Final group
    cout << setw(4) << start << "-" << setw(4) << sb.totalBlocks - 1 << "        | "
            << setw(13) << currType << " | " << currStatus << "\n";
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
