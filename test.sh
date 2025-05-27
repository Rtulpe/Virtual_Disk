#!/bin/bash
set -e

# Create a new virtual disk (1 MB for demonstration)
echo "Creating virtual disk 'disk.vd' (1 MB)..."
./vfs dmake disk.vd 1048576

echo -e "\nListing files on new disk (should be empty):"
./vfs dls disk.vd

# Prepare some host files
echo -e "\nCreating host files file1.txt and file2.bin..."
echo "Hello, Virtual FS!" > file1.txt
dd if=/dev/urandom of=file2.bin bs=512 count=2 2>/dev/null

# Copy files into virtual disk
echo -e "\nCopying file1.txt and file2.bin into virtual disk..."
./vfs dput disk.vd file1.txt
./vfs dput disk.vd file2.bin

echo -e "\nListing files after copy:"
./vfs dls disk.vd

echo -e "\nBlock map after initial copy (no fragmentation yet):"
./vfs dmap disk.vd

# Delete one file to create a free block between data regions
echo -e "\nDeleting file1.txt from virtual disk..."
./vfs ddel disk.vd file1.txt

echo -e "\nBlock map after deleting file1.txt (fragmentation created):"
./vfs dmap disk.vd

# Copy another file to fill the gap (may become fragmented)
echo -e "\nCreating and copying file3.txt..."
echo "Another file to the VFS." > file3.txt
./vfs dput disk.vd file3.txt

echo -e "\nFinal directory listing:"
./vfs dls disk.vd

echo -e "\nFinal block map (showing how new file filled gaps):"
./vfs dmap disk.vd

# Copy file2.bin back out of virtual disk
echo -e "\nCopying file2.bin from virtual disk to host as output2.bin..."
./vfs dget disk.vd file2.bin output2.bin
echo "Size of output2.bin: $(stat --printf="%s" output2.bin) bytes"

# Clean up: delete virtual disk
echo -e "\nRemoving virtual disk 'disk.vd'..."
./vfs rmdisk disk.vd