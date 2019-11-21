#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mount.h>
#include <unistd.h>
#include "FileSystem.h"

#define ONE_KB 1024
#define NUM_INODES 126

char buffer[ONE_KB];
char *cwd;                                 // keep track of current working dir
Super_block super_block;                                  // init a super block


/*
Mounts the file system for the given virtual disk (file) and sets the current
working directory to root.

Usage: M <disk name>
*/
void fs_mount(char *new_disk_name) {

    FILE *disk;

    disk = fopen(new_disk_name, 'r');
    if (disk == NULL) {      // if disk name does not exist
        fprintf(stderr, "Error: Cannot find disk %s", new_disk_name);
        exit(1);
    }

    // read the super block from disk
    fread(&super_block, sizeof(Super_block), ONE_KB, disk);
    char *free_block_arr = super_block.free_block_list;
    Inode *inode_arr = super_block.inode;

    //-------------------------------------------------------------------------
    // consistency checks:
    //-------------------------------------------------------------------------
    // 1. free-space list check
    uint8_t inode_in_use = -1;      // 1: inodes is in used, 0: not in use
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t start_block = inode_arr[i].start_block;
        uint8_t used_size = inode_arr[i].used_size;
        inode_in_use = used_size >> 7;
        uint8_t file_size;
        file_size = used_size << 1;
        file_size = used_size >> 1;              // inode status bit is removed

        for (int j = 0; j < file_size; j++) {
            uint8_t start_index = (start_block + j) / 8; // index of block in free block arr
            uint8_t start_pos = (start_block + j) % 8;
            uint8_t byte = free_block_arr[start_index];
            uint8_t bit = byte & (1 << (7 - start_pos));    // mask out the bit
            bit = bit >> (7 - start_pos);
            if ((bit ^ inode_in_use) != 0) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 1);
                exit(1);
            }
        }
    }
    //-------------------------------------------------------------------------
    // 2. name of file/dir must be unique in each dir
    // extract the file/dir names and parent dir to an array
    // TODO: should I compare empty names? -> 00000...0
    char name_arr[NUM_INODES];
    uint8_t dir_parent_arr[NUM_INODES];
    for (int i = 0; i < NUM_INODES; i++) {
        name_arr[i] = inode_arr[i].name;
        dir_parent_arr[i] = inode_arr[i].dir_parent;
    }
    // check if there is a duplicate name under the same parent dir
    for (int i = 0; i < NUM_INODES; i++) {
        for (int j = i + 1; j < NUM_INODES; j++) {
            if ((strcmp(name_arr[i], name_arr[j]) == 0) && \ 
                (dir_parent_arr[i] == dir_parent_arr[j])) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 2);
                exit(1);
            }
        }
    }
    //-------------------------------------------------------------------------
    // 3. if inode state is free (0), all bits in inode must be 0; otherwise,
    //    its name has at least one non-zero bit
    uint8_t inode_in_use = -1;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t used_size = inode_arr[i].used_size;
        inode_in_use = used_size >> 7;
        if (inode_in_use == 0) {                            // if inode is free
            // check if name is 0's
            for (int j = 0; j < 5; j++) {
                if (inode_arr[i].name[j] != 0) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error \
                        code: %d)", new_disk_name, 3);
                    exit(1);
                }
            }
            if ((used_size != 0) || (inode_arr[i].start_block != 0) || \
                    (inode_arr[i].dir_parent != 0)) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 3);
                exit(1);
            }
        } else {                                             // if inode in use
            int has_one = 0;
            for (int i = 0; i < 5; i++) {
                if (inode_arr[i].name[i] == 1) {
                    has_one = 1;
                }
            }
            if (has_one == 0) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 3);
                exit(1);
            }
        }
    }
    //-------------------------------------------------------------------------
    // 4. inode for a file must have a startblock having a value [1, ..., 127]
    // 5. size & start block of a dir must be zero
    uint8_t is_dir = -1;                                         // 1: is a dir
    for (int i = 0; i < NUM_INODES; i++) {
        is_dir = inode_arr[i].dir_parent >> 7;
        if (is_dir = 0) {                                     // if it's a file
            if ((inode_arr[i].start_block < 1) || (inode_arr[i].start_block > 127)) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 4);
                exit(1);
            }
        } else if (is_dir == 1) {                             // if it's a dir
            uint8_t size = inode_arr[i].used_size << 1;
            size = size >> 1;
            if ((size != 0) || (inode_arr[i].start_block != 0)) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 5);
                exit(1);
            }
        }
    }
    //-------------------------------------------------------------------------
    // 6. parent dir cannot be 126 (can be 0~125, 127); if 0~125: parent node
    //    must be in use, and marked as dir
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir_parent = inode_arr[i].dir_parent << 1;
        dir_parent = dir_parent >> 1;
        uint8_t is_dir = inode_arr[i].dir_parent >> 7;
        if (dir_parent == 126) {
            fprintf(stderr, "Error: File System in %s is inconsistent (error \
                code: %d)", new_disk_name, 6);
            exit(1);
        } else if ((dir_parent >= 0) || (dir_parent <= 125)) {
            Inode parent = inode_arr[dir_parent];
            uint8_t parent_in_use = parent.used_size >> 7;
            uint8_t parent_is_dir = parent.dir_parent >> 7;
            if ((parent_in_use == 0) || (parent_is_dir == 0)) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error \
                    code: %d)", new_disk_name, 6);
                exit(1);
            }
        }
    }
    //-------------------------------------------------------------------------


}


/*
Creates a new file with the provided name and size in the current working dir.

Usage: C <file name> <file size>
*/
void fs_create(char name[5], int size);


/*
Deletes the specified file from current working dir.

Usage: D <file name>
*/
void fs_delete(char name[5]);


/*
Reads the block-number-th block from the specified file into buffer.

Usage: R <file name> <block number>
*/
void fs_read(char name[5], int block_num);


/*
Writes the data in the buffer to the block-number-th block of the specified file.

Usage: W <file name> <block number>
*/
void fs_write(char name[5], int block_num);


/*
Updates the buffer with the provided characters. Up to 1024 characters can be
provided. If fewer characters are provided, the remaining bytes are set to 0.

Usage: B <new buffer characters>
*/
void fs_buff(uint8_t buff[1024]);


/*
Lists all the files/dirs in the current working dir, including . and ..

Usage: L
*/
void fs_ls(void);


/*
Changes the size of the given file. If the file size is reduced, the extra blocks
must be deleted (zeroed out).

Usage: E <file name> <new size>
*/
void fs_resize(char name[5], int new_size);


/*
Defragments the disk, moveing used blocks toward the superblock while maintaning
the file data. As a result of performing defragmentation, contiguous free blocks
can be created.

Usage: O
*/
void fs_defrag(void);


/*
Updates the current working dir to the provided dir. This new dir can be either
a subdir in the current working dir or the parent of the current working dir.

Usage: Y <dir name>
*/
void fs_cd(char name[5]);
