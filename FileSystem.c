#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "FileSystem.h"

#define ONE_KB 1024
#define NUM_INODES 126

int disk_fp;
char *disk_name;
char buffer[ONE_KB];
uint8_t cwd;                                             // current working dir
Super_block super_block;                                  // init a super block
Super_block old_super_block;                           // keep track of old s_b
bool has_old_super_block = false;

int main(int argc, char **argv) {
    char *input_file = argv[1];
    FILE *input_fp;
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    input_fp = fopen(input_file, "r");
    if (input_fp == NULL)
        exit(EXIT_FAILURE);

    char *tokenized[100];

    int line_num = 0;

    while ((linelen = getline(&line, &linecap, input_fp)) != -1) {
        // replace newline at end of line to \0
        if (line[linelen - 1] == '\n') {
            line[linelen - 1] = '\0';
        }
        memset(tokenized, 0, sizeof(char *) * 100);
        tokenize(line, " ", tokenized);
        line_num += 1;
        handle_input(tokenized, input_file, line_num);
    }

    fclose(input_fp);
    if (line)
        free(line);

    return 0;
}

void handle_input(char *tokenized[], char *input_file, int line_num) {
    char *cmd = tokenized[0];
    if (strcmp(cmd, "M") == 0) {
        if (check_num_args(tokenized, 2)) {
            disk_name = tokenized[1];
            fs_mount(disk_name);
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "C") == 0) {
        if ((check_num_args(tokenized, 3)) &&\
                 (check_name_len(tokenized[1])) &&\
                    (check_file_size(atoi(tokenized[2])))) {
            fs_create(tokenized[1], atoi(tokenized[2]));
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "D") == 0) {
        if ((check_num_args(tokenized, 2)) && (check_name_len(tokenized[1]))) {
            fs_delete(tokenized[1]);
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "R") == 0) {
        if ((check_num_args(tokenized, 3)) &&\
                 (check_name_len(tokenized[1])) &&\
                    (check_block_num(atoi(tokenized[2])))) {
            fs_read(tokenized[1], atoi(tokenized[2]));
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "W") == 0) {
        if ((check_num_args(tokenized, 3)) &&\
                 (check_name_len(tokenized[1])) &&\
                    (check_block_num(atoi(tokenized[2])))) {
            fs_write(tokenized[1], atoi(tokenized[2]));
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "B") == 0) {
        if (check_num_args(tokenized, 2)) {
            uint8_t buff[ONE_KB];
            memset(buff, 0, ONE_KB);
            for (int i = 0; i < strlen(tokenized[1]); i++) {
                buff[i] = tokenized[1][i];
            }
            fs_buff(buff);
            // fs_buff((uint8_t *)(tokenized[1]));
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "L") == 0) {
        fs_ls();
    } else if (strcmp(cmd, "E") == 0) {
        if ((check_num_args(tokenized, 3)) &&\
                 (check_name_len(tokenized[1])) &&\
                    (check_file_size(atoi(tokenized[2])))) {
            fs_resize(tokenized[1], atoi(tokenized[2]));
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else if (strcmp(cmd, "O") == 0) {
        fs_defrag();
    } else if (strcmp(cmd, "Y") == 0) {
        if ((check_num_args(tokenized, 2)) &&\
                 (check_name_len(tokenized[1]))) {
            fs_cd(tokenized[1]);
        } else {
            print_cmd_error(input_file, line_num);
        }
    } else {
        print_cmd_error(input_file, line_num);
    }
}

/*
Mounts the file system for the given virtual disk (file) and sets the current
working directory to root.

Usage: M <disk name>
*/
void fs_mount(char *new_disk_name) {

    disk_fp = open(new_disk_name, O_RDWR);
    if (disk_fp == -1) {                            // if disk name does not exist
        fprintf(stderr, "Error: Cannot find disk %s\n", new_disk_name);
        exit(1);
    }

    disk_name = new_disk_name;

    // read the super block from disk
    read(disk_fp, &super_block.free_block_list, 16);
    read(disk_fp, &super_block.inode, 126*8);
    char *free_block_arr = super_block.free_block_list;
    Inode *inode_arr = super_block.inode;

    //-------------------------------------------------------------------------
    // consistency checks:
    bool is_consistent = true;
    //-------------------------------------------------------------------------
    // 1. free-space list check
    uint8_t inode_in_use = -1;           // 1: inodes is in used, 0: not in use
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
                fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 1);
                is_consistent = false;
                break;
            }
        }
    }
    //-------------------------------------------------------------------------
    // 2. name of file/dir must be unique in each dir
    // extract the file/dir names and parent dir to an array
    if (is_consistent) {             // only check this when above tests passed
        char *name_arr[NUM_INODES];
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
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 2);
                    is_consistent = false;
                    break;
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    // 3. if inode state is free (0), all bits in inode must be 0; otherwise,
    //    its name has at least one non-zero bit
    if (is_consistent) {             // only check this when above tests passed
        uint8_t inode_in_use = -1;
        for (int i = 0; i < NUM_INODES; i++) {
            uint8_t used_size = inode_arr[i].used_size;
            inode_in_use = used_size >> 7;
            if (inode_in_use == 0) {                            // if inode is free
                // check if name is 0's
                for (int j = 0; j < 5; j++) {
                    if (inode_arr[i].name[j] != 0) {
                        fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                        is_consistent = false;
                        break;
                    }
                }
                if ((used_size != 0) || (inode_arr[i].start_block != 0) || \
                        (inode_arr[i].dir_parent != 0)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                    is_consistent = false;
                    break;
                }
            } else {                                             // if inode in use
                int has_one = 0;
                for (int i = 0; i < 5; i++) {
                    if (inode_arr[i].name[i] == 1) {
                        has_one = 1;
                    }
                }
                if (has_one == 0) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                    is_consistent = false;
                    break;
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    // 4. inode for a file must have a startblock having a value [1, ..., 127]
    // 5. size & start block of a dir must be zero
    if (is_consistent) {             // only check this when above tests passed
        uint8_t is_dir = -1;                                         // 1: is a dir
        for (int i = 0; i < NUM_INODES; i++) {
            is_dir = inode_arr[i].dir_parent >> 7;
            if (is_dir == 0) {                                     // if it's a file
                if ((inode_arr[i].start_block < 1) || (inode_arr[i].start_block > 127)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 4);
                    is_consistent = false;
                    break;
                }
            } else if (is_dir == 1) {                             // if it's a dir
                uint8_t size = inode_arr[i].used_size << 1;
                size = size >> 1;
                if ((size != 0) || (inode_arr[i].start_block != 0)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 5);
                    is_consistent = false;
                    break;
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    // 6. parent dir cannot be 126 (can be 0~125, 127); if 0~125: parent node
    //    must be in use, and marked as dir
    if (is_consistent) {             // only check this when above tests passed
        for (int i = 0; i < NUM_INODES; i++) {
            uint8_t dir_parent = inode_arr[i].dir_parent << 1;
            dir_parent = dir_parent >> 1;
            if (dir_parent == 126) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 6);
                is_consistent = false;
                break;
            } else if ((dir_parent >= 0) || (dir_parent <= 125)) {
                Inode parent = inode_arr[dir_parent];
                uint8_t parent_in_use = parent.used_size >> 7;
                uint8_t parent_is_dir = parent.dir_parent >> 7;
                if ((parent_in_use == 0) || (parent_is_dir == 0)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 6);
                    is_consistent = false;
                    break;
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    // consistency checks done
    //-------------------------------------------------------------------------

    if (is_consistent == false) {                          // if not consistent
        if (has_old_super_block) {
            super_block = old_super_block;                   // use the old s_b
        } else {               
            fprintf(stderr, "Error: No file system is mounted\n");
        }
    } else {                                                   // if consistent
        old_super_block = super_block;
        has_old_super_block = true;
        cwd = 127;                                 // set cwd to root: 01111111
    }
}


/*
Creates a new file with the provided name and size in the current working dir.

Usage: C <file name> <file size>
*/
void fs_create(char name[5], int size) {

    Inode *inode_arr = super_block.inode;

    bool valid_create = true;

    //------------------check if there is available inode----------------------
    bool available_inode = false;
    int available_inode_index;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t in_use = inode_arr[i].used_size >> 7;
        if (in_use == 0) {
            available_inode_index = i;         // first available inode's index
            available_inode = true;
            break;
        }
    }
    if (available_inode == false) {
        fprintf(stderr, "Error: Superblock in disk %s is full, cannot create %s\n", disk_name, name);
        valid_create = false;
    }

    //--------------------check for name duplications--------------------------
    if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) {
        fprintf(stderr, "Error: File or directory %s already exists\n", name);
        valid_create = false;
    } else {
        for (int i = 0; i < NUM_INODES; i++) {
            if (inode_arr[i].dir_parent == cwd) {
                if (strcmp(inode_arr[i].name, name) == 0) {
                    fprintf(stderr, "Error: File or directory %s already exists\n", name);
                    valid_create = false;
                    break;
                }
            }
        }
    }

    //-------------------------------------------------------------------------
    char *free_block_arr = super_block.free_block_list;
    int num_free_blocks = 0;
    uint8_t start_block = 0;

    //---------------------look for contiguous blocks--------------------------
    bool success = false;
    if (size != 0) {                                            // if it's file        
        for (int i = 0; i < 16; i++) {
            uint8_t byte = free_block_arr[i];

            uint8_t mask = 128;
            for (int j = 0; j < 8; j++) {               // get each bit in byte
                uint8_t bit = byte & mask;
                if (bit == 0) {                    // when finding a free block
                    num_free_blocks += 1;
                } else {                     // reset counter if block not free
                    num_free_blocks = 0;
                }

                start_block += 1;

                if (num_free_blocks == size) {
                    
                    // minus size since start_block pointing to end of contiguous blocks
                    start_block -= (size - 1);

                    success = true;
                    break;
                }
                mask = mask >> 1;
            }
        }
        if (success == false) {
            fprintf(stderr, "Error: Cannot allocate %d on %s\n", size, disk_name);
        }
    }

    //---------------------create the file/dir---------------------------------
    strncpy(inode_arr[available_inode_index].name, name, 5);            // set name

    inode_arr[available_inode_index].start_block = start_block; // set start_block

    uint8_t mask = 128;

    inode_arr[available_inode_index].used_size = size | mask; // set in_use to 1

    if (size == 0) {                                             // if it's dir
        inode_arr[available_inode_index].dir_parent = cwd | mask; // set 1st bit to 1
    } else {                                                    // if it's file
        inode_arr[available_inode_index].dir_parent = cwd;
    }
}


/*
Deletes the specified file from current working dir.

Usage: D <file name>
*/
void fs_delete(char name[5]) {

    //------------------check if name exists-----------------------------------
    Inode *inode_arr = super_block.inode;
    bool name_exists = false;
    int target_index;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir = inode_arr[i].dir_parent << 1;
        dir = dir >> 1;
        if ((strcmp(inode_arr[i].name, name) == 0) && (dir == cwd)) {
            name_exists = true;
            target_index = i;
        }
    }
    if (name_exists == false) {
        fprintf(stderr, "Error: File or directory %s does not exist\n", name);
        return;
    }

    //------------------zero out the file/dir----------------------------------
    if ((inode_arr[target_index].dir_parent >> 7) == 0) {       // if it's file

        char *free_block_arr = super_block.free_block_list;
        uint8_t start_block = inode_arr[target_index].start_block;
        uint8_t used_size = inode_arr[target_index].used_size;
        uint8_t file_size;
        file_size = used_size << 1;
        file_size = used_size >> 1;              // inode status bit is removed

        // clear relevant data blocks
        lseek(disk_fp, start_block * ONE_KB, SEEK_SET);  // reset fp to file start
        char buff[ONE_KB];
        memset(buff, 0, ONE_KB);
        for (int i = 0; i < file_size; i++) {
            write(disk_fp, buff, ONE_KB);                  // clear data blocks
        }

        // zero out the occupied blocks
        for (int j = 0; j < file_size; j++) {
            uint8_t start_index = (start_block + j) / 8; // index of block in free block arr
            uint8_t start_pos = (start_block + j) % 8;
            uint8_t byte = free_block_arr[start_index];
            free_block_arr[start_index] = byte ^ (1 << (7 - start_pos)); // zero out the bit
        }

        for (int i = 0; i < 5; i++) {
            inode_arr[target_index].name[i] = 0;            // zero out name
        }
        inode_arr[target_index].used_size = 0;            // zero out used_size
        inode_arr[target_index].start_block = 0;        // zero out start_block
        inode_arr[target_index].dir_parent = 0;          // zero out dir_parent
    } else {                                                     // if it's dir
        for (int i = 0; i < NUM_INODES; i++) {
            uint8_t dir = inode_arr[i].dir_parent << 1;
            dir = dir >> 1;
            if (dir == target_index) {
                fs_delete(inode_arr[i].name);
            }           
        }
    }
}


/*
Reads the block-number-th block from the specified file into buffer.

Usage: R <file name> <block number>
*/
void fs_read(char name[5], int block_num) {
    int target_index = check_file_exist(name);

    if (check_has_block(block_num, name, target_index)) {
        lseek(disk_fp, (super_block.inode[target_index].start_block + block_num) * ONE_KB, SEEK_SET);
        read(disk_fp, buffer, ONE_KB); 
    }
}


/*
Writes the data in the buffer to the block-number-th block of the specified file.

Usage: W <file name> <block number>
*/
void fs_write(char name[5], int block_num) {
    int target_index = check_file_exist(name);

    if (check_has_block(block_num, name, target_index)) {
        lseek(disk_fp, (super_block.inode[target_index].start_block + block_num) * ONE_KB, SEEK_SET);
        write(disk_fp, buffer, ONE_KB); 
    }
}


/*
Updates the buffer with the provided characters. Up to 1024 characters can be
provided. If fewer characters are provided, the remaining bytes are set to 0.

Usage: B <new buffer characters>
*/
void fs_buff(uint8_t buff[1024]) {
    memset(buffer, 0, ONE_KB);                              // flush the buffer

    // copy new buffer to global buffer
    for (int i = 0; i < ONE_KB; i++) {
        buffer[i] = buff[i];
    }
}


/*
Lists all the files/dirs in the current working dir, including . and ..

Usage: L
*/
void fs_ls(void) {

}


/*
Changes the size of the given file. If the file size is reduced, the extra blocks
must be deleted (zeroed out).

Usage: E <file name> <new size>
*/
void fs_resize(char name[5], int new_size) {

}


/*
Defragments the disk, moveing used blocks toward the superblock while maintaning
the file data. As a result of performing defragmentation, contiguous free blocks
can be created.

Usage: O
*/
void fs_defrag(void) {

}


/*
Updates the current working dir to the provided dir. This new dir can be either
a subdir in the current working dir or the parent of the current working dir.

Usage: Y <dir name>
*/
void fs_cd(char name[5]) {

}

/**
 * @brief Tokenize a C string 
 * 
 * @param str - The C string to tokenize 
 * @param delim - The C string containing delimiter character(s) 
 * @param argv - A char* array that will contain the tokenized strings
 * Make sure that you allocate enough space for the array.
 */
void tokenize(char *str, const char *delim, char *argv[]) {
  char *token;
  token = strtok(str, delim); // getting first token
  for(size_t i = 0; token != NULL; ++i){ //getting the following tokens
    argv[i] = token;
    token = strtok(NULL, delim);
  }
}

// return true if tokenized has num_args arguments, false otherwise
bool check_num_args(char *tokenized[], int num_args) {
    int arr_size = 0;
    while (tokenized[arr_size] != NULL) {
        arr_size += 1;
    }
    if (arr_size != num_args) {
        return false;
    }
    return true;
}

// return true if name length <= 5
bool check_name_len(char *name) {
    if (strlen(name) > 5) {
        return false;
    }
    return true;
}

// return true if block number outside of [1, 127]
bool check_block_num(int block_num) {
    if ((block_num < 1) || (block_num > 127)) {
        return false;
    }
    return true;
}

// return true if size outside of [0, 127]
bool check_file_size(int size) {
    if ((size < 0) || (size > 127)) {
        return false;
    }
    return true;
}

void print_cmd_error(char *file_name, int line_num) {
    fprintf(stderr, "Command Error: %s, %d\n", file_name, line_num);
}

// return file's index if given file name exists in current working dir (cwd)
// return -1 if given file name does not exist
int check_file_exist(char name[5]) {
    Inode *inode_arr = super_block.inode;
    bool name_found = false;
    int target_index;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir_parent = inode_arr[i].dir_parent << 1;
        dir_parent = dir_parent >> 1;
        // if under cwd & name matched & is a file
        if ((inode_arr[i].dir_parent == cwd) && \
                (strcmp(inode_arr[i].name, name) == 0) && \
                    (inode_arr[i].dir_parent >> 7 == 0)) {
            name_found = true;
            target_index = i;
        }
    }
    if (!name_found) {
        fprintf(stderr, "Error: File %s does not exist\n", name);
        return -1;
    }
    return target_index;
}

// return true if inode at target_index has a block_num in range [0, file_size - 1]
// return false otherwise
bool check_has_block(int block_num, char name[5], int target_index) {
    uint8_t used_size = super_block.inode[target_index].used_size;
    uint8_t file_size = used_size << 1;
    file_size = used_size >> 1;
    // check if block_num within [0, file_size - 1]
    if ((block_num < 0) || (block_num >= file_size)) {
        fprintf(stderr, "Error: %s does not have block %d\n", name, block_num);
        return false;
    }
    return true;
}
