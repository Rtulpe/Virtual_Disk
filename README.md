# Introduction
This repository holds a small program for a virtual file system (VFS), which simulates a file system in memory all within a single file.

This project is based on FAT (File Allocation Table) file system principles, albeit very simplified.

# Building
Well, you do need [CMake](https://cmake.org/), which is pretty standard. Run in bash:
```bash
chmod +x build.sh # If you are on Windows, use Git Bash or WSL
./build.sh
```

# Features
- Single directory structure.
- Files can be placed (put), retrieved (get), and deleted.
- Basic listing operation as well as printing the memory usage.
- Some basic error handling as well as a couple of input checks, so you don't create 10TB files by accident.

# Usage
After building, run the program:
```bash
./vfs
```

Commands list:
**[dmake dremove dput dget ddel dls dmap help about]**