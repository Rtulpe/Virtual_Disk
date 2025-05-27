#include <iostream>
#include "VirtualFileSystem.h"

using namespace std;

void printAbout() {
    cout << "TRUSTech Virtual File System (TTvfs)" << endl;
    cout << "Version - Alpha 0.1" << endl << endl;
}

// Print usage in case the entered command is wrong
void printUsage(const string &programName) {
    cout << "Usage: " << programName << " <command> [options]" << endl;
    cout << "----------------------------------------" << endl;
    cout << "dmake   <diskfile> [size_bytes] <- Create a new virtual disk file with optional size" << "\n" <<
            "(default 10MB, min 4096 bytes, max 100MB)" << endl;
    cout << "dremove <diskfile> <- Remove the virtual disk file" << endl;
    cout << "dput    <diskfile> <localfile> <- Copy a local file to the virtual disk" << endl;
    cout << "dget    <diskfile> <filename> [dest] <- Copy a file from the virtual disk" << endl;
    cout << "ddel    <diskfile> <filename> <- Deletes a file from the virtual disk" << endl;
    cout << "dls     <diskfile> <- List files in the virtual disk" << endl;
    cout << "dmap    <diskfile> <- Show block occupation on the virtual disk" << endl;
    cout << "help <- Show this help message" << endl;
    cout << "about <- For more information about the program" << endl;
}

int main(const int argc, char *argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Complaint: 'string' not being allowed in a switch statement, ridiculous
    // Thus using good-old if-else chain instead

    if (const string cmd = argv[1]; cmd == "dmake") {
        if (argc < 3) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        uint32_t size = DEFAULT_DISK_SIZE;
        if (argc >= 4) {
            size = static_cast<uint32_t>(stoul(argv[3]));
            // We should also check for size in here
            // too small would just cause a crash
            // However, too big can cause serious issues
            // Limiting to 100 MB for now
            if (size < 4096 || size > 100 * 1024 * 1024) {
                cerr << "Error: Disk size must be between 4096 bytes and 100 MB." << endl;
                return 1;
            }
        }
        if (VirtualFileSystem vfs(diskName); !vfs.createDisk(size)) return 1;
    } else if (cmd == "dremove") {
        if (argc < 3) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        VirtualFileSystem vfs(diskName);
        vfs.removeDisk();
    } else if (cmd == "dput") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        const string hostFile = argv[3];
        VirtualFileSystem vfs(diskName);
        if (!vfs.loadDisk()) return 1;
        vfs.copyFromHost(hostFile);
    } else if (cmd == "dget") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        const string fileName = argv[3];
        const string dest = (argc >= 5 ? argv[4] : "");
        VirtualFileSystem vfs(diskName);
        if (!vfs.loadDisk()) return 1;
        vfs.copyToHost(fileName, dest);
    } else if (cmd == "ddel") {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        const string fileName = argv[3];
        VirtualFileSystem vfs(diskName);
        if (!vfs.loadDisk()) return 1;
        vfs.deleteFile(fileName);
    } else if (cmd == "dls") {
        if (argc < 3) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        VirtualFileSystem vfs(diskName);
        if (!vfs.loadDisk()) return 1;
        vfs.listFiles();
    } else if (cmd == "dmap") {
        if (argc < 3) {
            printUsage(argv[0]);
            return 1;
        }

        const string diskName = argv[2];
        VirtualFileSystem vfs(diskName);
        if (!vfs.loadDisk()) return 1;
        vfs.showMap();
    } else if (cmd == "help") {
        printUsage(argv[0]);
        return 0;
    } else if (cmd == "about") {
        printAbout();
        return 0;
    } else {
        cout << "Unknown command: " << cmd << endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
