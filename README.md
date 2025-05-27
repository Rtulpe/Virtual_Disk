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

Upsides and downsides:

- **Upsides**:
  - Relatively simple.
  - Little abstaction.
  - Single file, no dependencies.
  - Similar to FAT.
  - Directory and FAT are at fixed positions.
  - Able to see the memory usage.
  - External fragmentation is not a problem, due to FAT-like structure.

- **Downsides**:
  - No subdirectories.
  - Incredibly basic, just bare minimum.
  - Scalability is not considered.
  - Concurrency is not considered.
  - Not suitable for proper OS.
  - Internal fragmentation is a problem, due to fixed-size blocks.
  - File limit (64 files), drive size limit (4KB to 100MB), but can be changed in the code.