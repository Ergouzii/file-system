#include <stdio.h>
#include <stdint.h>
#include <sys/mount.h>
#include <unistd.h>
#include "FileSystem.h"

#define ONE_KB 1024 

char buffer[ONE_KB];
char *cwd;                                 // keep track of current working dir

/*
Mounts the file system for the given virtual disk (file) and sets the current
working directory to root.

Usage: M <disk name>
*/
void fs_mount(char *new_disk_name) {

    if (access(new_disk_name, F_OK) != 0) {      // if disk name does not exist
        fprintf(stderr, "Error: Cannot find disk %s", new_disk_name);
    }

    // mounting the file system to disk
    Super_block super_block;

    // consistency checks:
    // 1. free-space list check
    
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
