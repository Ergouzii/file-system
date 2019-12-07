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

int DISK_FP;
char DISK_NAME[100];
bool MOUNTED = false;
char BUFFER[ONE_KB];
uint8_t ROOT_DIR = 127;
uint8_t CWD;                                             // current working dir
Super_block super_block;                                  // init a super block
Super_block old_super_block;                           // keep track of old s_b
bool has_old_super_block = false;
char ONE_DOT[5] = ".";
char TWO_DOTS[5] = "..";
uint8_t DELETE_DIR = 127;

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
        handle_input(tokenized, input_file, line_num, line, linelen);
    }

    fclose(input_fp);
    if (line)
        free(line);

    return 0;
}

void handle_input(char *tokenized[], char *input_file, int line_num, char *line, ssize_t linelen) {
    char *cmd = tokenized[0];
    if (strcmp(cmd, "M") == 0) {
        if (check_num_args(tokenized, 2)) {
            fs_mount(tokenized[1]);
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
            fs_delete(tokenized[1], DELETE_DIR);
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
        if (linelen == 1 || linelen == 2) {
            print_cmd_error(input_file, line_num);
        } else {
            uint8_t buff[ONE_KB];
            memset(buff, 0, ONE_KB);
            for (int i = 2; i < linelen; i++) {           // i = 2 to skip "B "
                buff[i - 2] = line[i];
            }
            fs_buff(buff);
        }
    } else if (strcmp(cmd, "L") == 0) {
        if (check_num_args(tokenized, 1)) {
            fs_ls();
        } else {
            print_cmd_error(input_file, line_num);
        }
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

    DISK_FP = open(new_disk_name, O_RDWR);
    if (DISK_FP == -1) {                            // if disk name does not exist
        fprintf(stderr, "Error: Cannot find disk %s\n", new_disk_name);
        return;
    }

    strcpy(DISK_NAME, new_disk_name);

    // read the super block from disk
    read(DISK_FP, &super_block.free_block_list, 16);
    read(DISK_FP, &super_block.inode, 126*8);
    char *free_block_arr = super_block.free_block_list;
    Inode *inode_arr = super_block.inode;

    //-------------------------------------------------------------------------
    // consistency checks:
    bool is_consistent = true;
    //-------------------------------------------------------------------------
    // 1. free-space list check
    int i = 1;
    while (i < 16 * 8) {                     // for each bit in free block list
        uint8_t start_index = i / 8;        // index of block in free block arr
        uint8_t start_pos = i % 8;
        uint8_t byte = free_block_arr[start_index];
        uint8_t bit = byte & (128 >> start_pos);            // mask out the bit
        bit = bit >> (7 - start_pos);

        bool belong_inode;
        if (bit == 1) {
            belong_inode = false;
        } else {
            belong_inode = true;
        }

        bool block_already_allocated = false;
        
        int file_size;
        for (int j = 0; j < NUM_INODES; j++) {
            uint8_t start_block = inode_arr[j].start_block;
            uint8_t used_size = inode_arr[j].used_size;
            uint8_t in_use = used_size >> 7;
            file_size = used_size << 1 & 255;     // inode status bit is removed
            file_size = file_size >> 1;

            if (i < start_block || i >= (start_block + file_size)) { // if not allocated
                continue;
            }
            
            if (block_already_allocated) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 1);
                is_consistent = false;
                return;
            }

            if (in_use != bit) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 1);
                is_consistent = false;
                return;
            }

            block_already_allocated = true;
            belong_inode = true;
        }

        if (!belong_inode) {
            fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 1);
            is_consistent = false;
            return;
        }

        i ++;
    }
    //-------------------------------------------------------------------------
    // 2. name of file/dir must be unique in each dir
    if (is_consistent) {             // only check this when above tests passed
        bool stop = false;
        // check if there is a duplicate name under the same parent dir
        for (int i = 0; i < NUM_INODES; i++) {
            if (inode_arr[i].used_size >> 7 == 0) {
                continue;
            }
            for (int j = i + 1; j < NUM_INODES; j++) {
                // if in use & name equal & same parent dir
                if ((inode_arr[j].used_size >> 7 == 1) &&\
                    (check_name_equal(inode_arr[i].name, inode_arr[j].name)) && \
                    ((inode_arr[i].dir_parent << 1 >> 1) == (inode_arr[j].dir_parent << 1 >> 1))) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 2);
                    is_consistent = false;
                    stop = true;          // stop two loops once name dup found
                    break;
                }
            }
            if (stop) {
                break;
            }
        }
    }
    //-------------------------------------------------------------------------
    // 3. if inode state is free (0), all bits in inode must be 0; otherwise,
    //    its name has at least one non-zero bit
    if (is_consistent) {             // only check this when above tests passed
        uint8_t inode_in_use = -1;
        for (int i = 0; i < NUM_INODES; i++) {
            bool stop = false;
            uint8_t used_size = inode_arr[i].used_size;
            inode_in_use = used_size >> 7;

            if (inode_in_use == 0) {                        // if inode is free
                // check if name is 0's
                for (int j = 0; j < 5; j++) {
                    if (inode_arr[i].name[j] != 0) {
                        fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                        is_consistent = false;
                        stop = true;
                        break;
                    }
                }
                if ((used_size != 0) || (inode_arr[i].start_block != 0) || \
                        (inode_arr[i].dir_parent != 0)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                    is_consistent = false;
                    stop = true;
                    break;
                }
            } else {                                         // if inode in use
                int has_one = 0;
                for (int i = 0; i < 5; i++) {
                    if (inode_arr[i].name[i] > 0) {
                        has_one = 1;
                    }
                }
                if (has_one == 0) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 3);
                    is_consistent = false;
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
        }
    }
    //-------------------------------------------------------------------------
    // 4. inode for a file must have a startblock having a value [1, ..., 127]
    // 5. size & start block of a dir must be zero
    if (is_consistent) {             // only check this when above tests passed
        uint8_t is_dir = -1;                                     // 1: is a dir
        for (int i = 0; i < NUM_INODES; i++) {
            if (inode_arr[i].used_size >> 7 == 0) {       // skip if not in use
                continue;
            }
            is_dir = inode_arr[i].dir_parent >> 7;
            if (is_dir == 0) {                                // if it's a file
                if ((inode_arr[i].start_block < 1) || (inode_arr[i].start_block > 127)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 4);
                    is_consistent = false;
                    break;
                }
            } else if (is_dir == 1) {                          // if it's a dir
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
            if (inode_arr[i].used_size >> 7 == 0) {
                continue;
            }
            uint8_t dir_parent = inode_arr[i].dir_parent << 1 >> 1;
            if (dir_parent == 126) {
                fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 6);
                is_consistent = false;
                break;
            } else if ((dir_parent >= 0) && (dir_parent <= 125)) {
                Inode parent = inode_arr[dir_parent];
                uint8_t parent_in_use = parent.used_size >> 7;
                uint8_t parent_is_dir = parent.dir_parent >> 7;
                if ((parent_in_use == 0) || (parent_is_dir == 0)) {
                    fprintf(stderr, "Error: File System in %s is inconsistent (error code: %d)\n", new_disk_name, 6);
                    is_consistent = false;
                    break;
                }
            } else if (dir_parent == 127) {
                ;
            }
        }
    }
    //-------------------------------------------------------------------------
    // consistency checks done
    //-------------------------------------------------------------------------

    if (is_consistent == false) {                          // if not consistent
        if (has_old_super_block) {
            super_block = old_super_block;                   // use the old s_b
        }
    } else {                                                   // if consistent
        MOUNTED = true;
        old_super_block = super_block;
        has_old_super_block = true;
        CWD = ROOT_DIR;                            // set cwd to root: 01111111
    }
}


/*
Creates a new file with the provided name and size in the current working dir.

Usage: C <file name> <file size>
*/
void fs_create(char name[5], int size) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    Inode *inode_arr = super_block.inode;

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
        fprintf(stderr, "Error: Superblock in disk %s is full, cannot create %s\n", DISK_NAME, name);
        return;
    }

    //--------------------check for name duplications--------------------------
    if (check_name_equal(name, ONE_DOT) || check_name_equal(name, TWO_DOTS)) {
        char temp_name[6];
        strncpy(temp_name, name, 5);
        temp_name[5] = '\0';                   // make name null-terminated
        fprintf(stderr, "Error: File or directory %s already exists\n", temp_name);
        return;
    } else {
        for (int i = 0; i < NUM_INODES; i++) {
            if (inode_arr[i].dir_parent == CWD) {
                if (check_name_equal(inode_arr[i].name, name)) {
                    char temp_name[6];
                    strncpy(temp_name, name, 5);
                    temp_name[5] = '\0';                   // make name null-terminated
                    fprintf(stderr, "Error: File or directory %s already exists\n", temp_name);
                    return;
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
        for (int i = 1; i < (16 * 8); i++) {
            uint8_t start_index = i / 8;
            uint8_t start_pos = i % 8;
            uint8_t byte = free_block_arr[start_index];
            uint8_t bit = byte & (128 >> start_pos);
            bit = bit >> (7 - start_pos);

            if (bit == 0) {                        // when finding a free block
                num_free_blocks += 1;
            } else {                         // reset counter if block not free
                num_free_blocks = 0;
            }

            start_block += 1;

            if (num_free_blocks == size) {
                
                // minus size since start_block pointing to end of contiguous blocks
                start_block -= (size - 1);

                success = true;
                break;
            }
        }
        if (success == false) {
            fprintf(stderr, "Error: Cannot allocate %d on %s\n", size, DISK_NAME);
            return;
        }
    }

    //---------------------create the file/dir---------------------------------
    strncpy(inode_arr[available_inode_index].name, name, 5);        // set name

    inode_arr[available_inode_index].start_block = start_block; // set start_block

    inode_arr[available_inode_index].used_size = size;
    inode_arr[available_inode_index].used_size = \
        set_bit(inode_arr[available_inode_index].used_size, 0, 1);// set in_use to 1

    if (size == 0) {                                             // if it's dir
        inode_arr[available_inode_index].dir_parent = CWD | 128; // set 1st bit to 1
    } else {                                                    // if it's file
        inode_arr[available_inode_index].dir_parent = CWD;
    }

    // update free block list
    for (int i = 0; i < size; i++) {
        uint8_t start_index = (start_block + i) / 8; // index of block in free block arr
        uint8_t start_pos = (start_block + i) % 8;
        uint8_t byte = super_block.free_block_list[start_index];
        super_block.free_block_list[start_index] = set_bit(byte, start_pos, 1); 
    }
    
    // update super block to disk
    lseek(DISK_FP, 0, SEEK_SET);
    write(DISK_FP, &super_block, ONE_KB);
}


/*
Deletes the specified file from some delete_dir.

Usage: D <file name>
*/
void fs_delete(char name[5], uint8_t delete_dir) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    //------------------check if name exists-----------------------------------
    Inode *inode_arr = super_block.inode;
    bool name_exists = false;
    int target_index;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir = inode_arr[i].dir_parent << 1;
        dir = dir >> 1;

        if (check_name_equal(inode_arr[i].name, name)) {
            if ((dir == delete_dir) || (dir == CWD)) {
                name_exists = true;
                target_index = i;
            }
        }
    }
    if (name_exists == false) {
        char temp_name[6];
        strncpy(temp_name, name, 5);
        temp_name[5] = '\0';                   // make name null-terminated
        fprintf(stderr, "Error: File or directory %s does not exist\n", temp_name);
        return;
    }

    //------------------zero out the file/dir----------------------------------
    if ((inode_arr[target_index].dir_parent >> 7) == 0) {       // if it's file
        uint8_t start_block = inode_arr[target_index].start_block;
        uint8_t used_size = inode_arr[target_index].used_size;
        uint8_t file_size;
        file_size = used_size << 1 & 255;
        file_size = file_size >> 1;              // inode status bit is removed

        // clear relevant data blocks
        lseek(DISK_FP, start_block * ONE_KB, SEEK_SET); // reset fp to file start
        char buff[ONE_KB];
        memset(buff, 0, ONE_KB);
        for (int i = 0; i < file_size; i++) {
            write(DISK_FP, buff, ONE_KB);                  // clear data blocks
        }
         
        // zero out the occupied blocks
        for (int j = 0; j < file_size; j++) {
            uint8_t start_index = (start_block + j) / 8; // index of block in free block arr
            uint8_t start_pos = (start_block + j) % 8;
            uint8_t byte = super_block.free_block_list[start_index];
            super_block.free_block_list[start_index] = byte ^ (1 << (7 - start_pos)); // zero out the bit
        }
    } else {                                                     // if it's dir
        for (int i = 0; i < NUM_INODES; i++) {
            uint8_t dir_parent = inode_arr[i].dir_parent & 127;
            if (dir_parent == target_index) {
                fs_delete(inode_arr[i].name, dir_parent);
            }           
        }
    }
    for (int i = 0; i < 5; i++) {
        inode_arr[target_index].name[i] = 0;               // zero out name
    }
    inode_arr[target_index].used_size = 0;            // zero out used_size
    inode_arr[target_index].start_block = 0;        // zero out start_block
    inode_arr[target_index].dir_parent = 0;          // zero out dir_parent

    // update super block to disk
    lseek(DISK_FP, 0, SEEK_SET);
    write(DISK_FP, &super_block, ONE_KB);
}


/*
Reads the block-number-th block from the specified file into buffer.

Usage: R <file name> <block number>
*/
void fs_read(char name[5], int block_num) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int target_index = check_file_exist(name);
    
    if (target_index > -1) {
        if (check_has_block(block_num, name, target_index)) {
            lseek(DISK_FP, (super_block.inode[target_index].start_block + block_num) * ONE_KB, SEEK_SET);
            read(DISK_FP, BUFFER, ONE_KB); 
        }
    }
}


/*
Writes the data in the buffer to the block-number-th block of the specified file.

Usage: W <file name> <block number>
*/
void fs_write(char name[5], int block_num) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int target_index = check_file_exist(name);

    if (target_index > -1) {
        if (check_has_block(block_num, name, target_index)) {
            lseek(DISK_FP, (super_block.inode[target_index].start_block + block_num) * ONE_KB, SEEK_SET);
            write(DISK_FP, BUFFER, ONE_KB); 
        }
    }
}


/*
Updates the buffer with the provided characters. Up to 1024 characters can be
provided. If fewer characters are provided, the remaining bytes are set to 0.

Usage: B <new buffer characters>
*/
void fs_buff(uint8_t buff[1024]) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    memset(BUFFER, 0, ONE_KB);                              // flush the buffer

    // copy new buffer to global buffer
    for (int i = 0; i < ONE_KB; i++) {
        BUFFER[i] = buff[i];
    }
}


/*
Lists all the files/dirs in the current working dir, including . and ..

Usage: L
*/
void fs_ls(void) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    Inode *inode_arr = super_block.inode;

    // print . and ..
    char cur[5] = ".";
    char parent[5] = "..";
    int cur_num_children = get_num_children(CWD);
    printf("%-5s %3d\n", cur, cur_num_children);
    int parent_num_children;

    if (CWD == ROOT_DIR) {                                    // if cwd is root
        parent_num_children = cur_num_children;
    } else {
        // find parent's num_children
        uint8_t dir_parent = inode_arr[CWD].dir_parent << 1;
        dir_parent = dir_parent >> 1;
        parent_num_children = get_num_children(dir_parent);
    }
    printf("%-5s %3d\n", parent, parent_num_children);

    // print children of cwd
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir_parent = inode_arr[i].dir_parent << 1;
        dir_parent = dir_parent >> 1;
        if (dir_parent == CWD) {                             // if is under cwd

            char temp_name[6];
            strncpy(temp_name, inode_arr[i].name, 5);
            temp_name[5] = '\0';                   // make name null-terminated
            
            uint8_t is_dir = inode_arr[i].dir_parent >> 7;
            if (is_dir == 0) {                                  // if it's file
                uint8_t size = inode_arr[i].used_size << 1;
                size = size >> 1;
                printf("%-5s %3d KB\n", temp_name, size);
            } else if (is_dir == 1) {                            // if it's dir
                int num_children = get_num_children(i);
                printf("%-5s %3d\n", temp_name, num_children);
            }
            
        }
    }
}


/*
Changes the size of the given file. If the file size is reduced, the extra blocks
must be deleted (zeroed out).

Usage: E <file name> <new size>
*/
void fs_resize(char name[5], int new_size) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    Inode *inode_arr = super_block.inode;

    int target_index = check_file_exist(name);
    
    if (target_index > -1) {
        uint8_t old_size = inode_arr[target_index].used_size & 127;
        uint8_t old_start_block = inode_arr[target_index].start_block;

        if (new_size > old_size) {          // if new_size > current file size
            uint8_t temp_start_block = old_start_block + old_size;
            if (check_free_blocks(temp_start_block, new_size - old_size)) { // if enough blocks
                ; 
            } else {                                     // if no enough blocks
                uint8_t new_start_block = 0;
                for (int i = 1; i < 128; i++) {
                    if (check_free_blocks(i, new_size + i)) {
                        new_start_block = i;
                        break;
                    }
                }
                
                if (new_start_block == 0) {
                    char temp_name[6];
                    strncpy(temp_name, name, 5);
                    temp_name[5] = '\0';                   // make name null-terminated
                    fprintf(stderr, "Error: File %s cannot expand to size %d\n", temp_name, new_size);
                    return;
                } else {                            // relocating block success
                    inode_arr[target_index].start_block = new_start_block;

                    // copy data to new positions and erase old data
                    int size_blocks = ONE_KB * old_size;
                    char empty_blocks[size_blocks];
                    memset(empty_blocks, 0, size_blocks);
                    char buff[size_blocks];

                    lseek(DISK_FP, old_start_block * ONE_KB, SEEK_SET);
                    read(DISK_FP, buff, size_blocks);   // read old data to buff
                    
                    lseek(DISK_FP, new_start_block * ONE_KB, SEEK_SET);
                    write(DISK_FP, buff, size_blocks); // write to new positions

                    lseek(DISK_FP, old_start_block * ONE_KB, SEEK_SET);
                    write(DISK_FP, empty_blocks, size_blocks); // clear old data


                    // delete old data in free_block_list
                    for (int i = 0; i < old_size; i++) {
                        uint8_t start_index = (old_start_block + i) / 8; // index of block in free block arr
                        uint8_t start_pos = (old_start_block + i) % 8;
                        super_block.free_block_list[start_index] = \
                            set_bit(super_block.free_block_list[start_index], start_pos, 0);
                    }
                }
            }

            // update used_size
            inode_arr[target_index].used_size = new_size | 128; 

            // update free block list
            uint8_t new_start_block = inode_arr[target_index].start_block; // make sure s_b updated
            for (int i = 0; i < new_size; i++) {
                uint8_t start_index = (new_start_block + i) / 8; // index of block in free block arr
                uint8_t start_pos = (new_start_block + i) % 8;
                super_block.free_block_list[start_index] |= (128 >> start_pos);
            }
        } else {                             // if new_size <= current file size
            // zero out extra blocks in file
            uint8_t offset = old_size - new_size;
            char buff[ONE_KB * offset];
            memset(buff, 0, ONE_KB * offset);
            lseek(DISK_FP, (super_block.inode[target_index].start_block + new_size) * ONE_KB, SEEK_SET);
            write(DISK_FP, buff, ONE_KB * offset);

            // update free block list
            for (int i = 0; i < offset; i++) {
                uint8_t start_index = (old_start_block + new_size + i) / 8; // index of block in free block arr
                uint8_t start_pos = (old_start_block + new_size + i) % 8;
                super_block.free_block_list[start_index] &= ~(128 >> start_pos);
            }

            // update used_size
            inode_arr[target_index].used_size = new_size | 128;
        }
        // write s_b back to disk
        lseek(DISK_FP, 0, SEEK_SET);
        write(DISK_FP, &super_block, ONE_KB);
    }
}


/*
Defragments the disk, moving used blocks toward the superblock while maintaning
the file data. As a result of performing defragmentation, contiguous free blocks
can be created.

Usage: O
*/
void fs_defrag(void) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    Inode *inode_arr = super_block.inode;

    Inode *temp_inode_arr[NUM_INODES];
    int temp_inode_index = 0;

    int i = 1;                // reading from 2nd element (skipping superblock)
    while (i < (16 * 8)) {    
        uint8_t start_index = i / 8;
        uint8_t start_pos = i % 8;
        uint8_t byte = super_block.free_block_list[start_index];
        uint8_t bit = byte & (128 >> start_pos);
        bit = bit >> (7 - start_pos);
        if (bit == 1) {                                    // once find a block
            uint8_t file_size;
            for (int j = 0; j < NUM_INODES; j++) {
                if (inode_arr[j].start_block == i) {        // s_b matching bit
                    // temp_inode_arr only has inodes for files (s_b > 0)
                    temp_inode_arr[temp_inode_index] = &inode_arr[j];
                    temp_inode_index++;
                    file_size = inode_arr[j].used_size << 1 & 255;
                    file_size = file_size >> 1;
                }  
            }
            if (file_size == 0) {
                i ++;
            } else {
                i += file_size;
            }
        } else {
            i++;
        }
    }

    // -----------------------update a new free_block_list---------------------
    char new_free_block_list[16];
    memset(new_free_block_list, 0, 16);
    new_free_block_list[0] = (char) 128;                       // s_b occupies first bit
    uint8_t num_occupied_blocks = 0;
    // find how many blocks in total
    for (i = 0; i < temp_inode_index; i++) { // for each inode in temp_inode_arr
        if (temp_inode_arr[i] -> start_block == 0) {
            continue;                          // skip when next inode is a dir
        }
        uint8_t file_size = temp_inode_arr[i] -> used_size << 1 & 255;
        file_size = file_size >> 1;
        num_occupied_blocks += file_size;
    }
    // fill new free block list with 1's
    for (i = 1; i <= num_occupied_blocks; i++) {
        uint8_t start_index = i / 8;
        uint8_t start_pos = i % 8;
        new_free_block_list[start_index] = set_bit(new_free_block_list[start_index], start_pos, 1);
    }

    memcpy(super_block.free_block_list, new_free_block_list, 16); // update s_b

    int temp_inode_arr_len = 0;
    while (temp_inode_arr[temp_inode_arr_len] != NULL) {
        temp_inode_arr_len ++;
    }
    
    // update start_block of each inode
    uint8_t new_start_block = 1;
    uint8_t size;
    for (i = 0; i < temp_inode_arr_len; i++) {
        if (temp_inode_arr[i] -> start_block == 0) {
            continue;                          // skip when next inode is a dir
        }
        size = temp_inode_arr[i] -> used_size << 1 & 255;
        size = size >> 1;

        temp_inode_arr[i] -> start_block = new_start_block;
        new_start_block += size;          // move pointer to next block's start
    }
    
    lseek(DISK_FP, 0, SEEK_SET);
    write(DISK_FP, &super_block, ONE_KB);              // write new s_b to disk

    // -----------------defrag data blocks in disk-----------------------------
    // char *new_data_blocks = malloc(127 * ONE_KB * sizeof(char *));
    // int new_data_blocks_index = 0;
    // for (int j = 1; j < 128; j++) {
    //     lseek(DISK_FP, j * ONE_KB, SEEK_SET); // points to second block (skip s_b)
    //     char buff[ONE_KB];
    //     read(DISK_FP, buff, ONE_KB);            // read one block (size = 1 kb)
    //     // TODO: empty block doesn't mean not allocated to a file
    //     //  TODO: 按照temp_inode的 s_b+size 按顺序copy出来
    //     if (is_empty_block(buff)) {                           // if block empty
    //         continue;
    //     } else {                                           // if block has data
    //         memcpy(&new_data_blocks[new_data_blocks_index], buff, ONE_KB);
    //         printf("%d\n", sizeof(char *));
    //         new_data_blocks_index += ONE_KB;
    //     }
    // }
    // // overwrite disk data blocks
    // lseek(DISK_FP, 1 * ONE_KB, SEEK_SET);
    // write(DISK_FP, new_data_blocks, 127 * ONE_KB);
}


/*
Updates the current working dir to the provided dir. This new dir can be either
a subdir in the current working dir or the parent of the current working dir.

Usage: Y <dir name>
*/
void fs_cd(char name[5]) {
    if (!MOUNTED) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    if (!(strcmp(name, ONE_DOT) == 0) && !(strcmp(name, TWO_DOTS) == 0)) {
        // check if dir name exist (cannot be . or ..)
        if (!check_dir_exist(name)) {
            char temp_name[6];
            strncpy(temp_name, name, 5);
            temp_name[5] = '\0';                   // make name null-terminated
            fprintf(stderr, "Error: Directory %s does not exist\n", temp_name);
            return;
        }
    }
    
    Inode *inode_arr = super_block.inode;

    if (strcmp(name, ONE_DOT) == 0) {
        ;
    } else if (strcmp(name, TWO_DOTS) == 0) {
        if (CWD == ROOT_DIR) {
            ;                                      // do nothing if cwd is root
        } else {
            uint8_t dir_parent = inode_arr[CWD].dir_parent << 1;
            dir_parent = dir_parent >> 1;
            CWD = dir_parent;
        }
    } else {
        for (int i = 0; i < NUM_INODES; i++) {
            // if under cwd && has equal name && is a dir
            uint8_t dir_parent = inode_arr[i].dir_parent << 1;
            dir_parent = dir_parent >> 1;

            if ((dir_parent == CWD) && \
                (check_name_equal(inode_arr[i].name, name)) && \
                (inode_arr[i].dir_parent >> 7 == 1)) {
                CWD = i;
            }
        }
    }
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

// return true if EVERY byte in name1 & name2 are equal
bool check_name_equal(char name1[5], char name2[5]) {
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            if (name1[i] != name2[i]) {
                return false;
            }
        }
    }
    return true;
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
    if (strlen(name) > 5 || strlen(name) == 0) {
        return false;
    }
    return true;
}

// return false if block number outside of [0, 127]
bool check_block_num(int block_num) {
    if ((block_num < 0) || (block_num > 127)) {
        return false;
    }
    return true;
}

// return false if size outside of [0, 127]
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
        if ((dir_parent == CWD) && \
                (check_name_equal(inode_arr[i].name, name)) && \
                    (inode_arr[i].dir_parent >> 7 == 0)) {
            name_found = true;
            target_index = i;
        }
    }
    if (!name_found) {
        char temp_name[6];
        strncpy(temp_name, name, 5);
        temp_name[5] = '\0';                   // make name null-terminated
        fprintf(stderr, "Error: File %s does not exist\n", temp_name);
        return -1;
    }

    return target_index;
}

// return true if given dir name exists in current working dir (cwd)
// return false otherwise
bool check_dir_exist(char name[5]) {
    Inode *inode_arr = super_block.inode;
    bool name_found = false;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir_parent = inode_arr[i].dir_parent << 1;
        dir_parent = dir_parent >> 1;

        // if under cwd & name matched & is a dir
        if ((dir_parent == CWD) && \
                (check_name_equal(inode_arr[i].name, name)) && \
                    (inode_arr[i].dir_parent >> 7 == 1)) {
            name_found = true;
        }            
    }
    return name_found;
}

// return true if inode at target_index has a block_num in range [0, file_size - 1]
// return false otherwise
bool check_has_block(int block_num, char name[5], int target_index) {
    uint8_t used_size = super_block.inode[target_index].used_size;
    uint8_t file_size = used_size << 1 & 255;
    file_size = file_size >> 1;
    // check if block_num within [0, file_size - 1]
    if ((block_num < 0) || (block_num >= file_size)) {
        char temp_name[6];
        strncpy(temp_name, name, 5);
        temp_name[5] = '\0';                   // make name null-terminated
        fprintf(stderr, "Error: %s does not have block %d\n", temp_name, block_num);
        return false;
    }
    return true;
}

// return the number of children of a directory given its index in inode (0-125)
int get_num_children(int targer_index) {

    int num_children = 2;                      // init to 2 because of . and ..

    Inode *inode_arr = super_block.inode;
    for (int i = 0; i < NUM_INODES; i++) {
        uint8_t dir_parent = inode_arr[i].dir_parent << 1;
        dir_parent = dir_parent >> 1;
        if (targer_index == dir_parent) {  // once find a file/dir under target
            num_children += 1;
        }
    }
    return num_children;
}

// return true if there is enough space in free block list for inode that starts
// from start_block with a certain size
bool check_free_blocks(uint8_t start_block, uint8_t size) {
    char *free_block_arr = super_block.free_block_list;

    for (int j = 0; j < size; j++) {
        uint8_t start_index = (start_block + j) / 8; // index of block in free block arr
        uint8_t start_pos = (start_block + j) % 8;
        uint8_t byte = free_block_arr[start_index];
        uint8_t bit = byte & (128 >> start_pos);        // mask out the bit
        bit = bit >> (7 - start_pos);
        if (bit != 0) {
            return false;
        }
    }
    return true;
}

// return true if buff (data block) is empty (all zeros)
bool is_empty_block(char buff[1024]) {
    for (int i = 0; i < 1024; i++) {
        if (buff[i] != 0) {
            return false;
        }
    }
    return true;
}

// example:
// t = 01000000
// t = set_bit(t, 1, 0) = 00000000
uint8_t set_bit(uint8_t ch, int i, int val) {

    uint8_t mask = 128 >> i ;

    if (val == 1) {                                          // setting the bit
        return ch | mask ;                                  // using bitwise OR
    } else {
        mask = ~mask;                                       // clearing the bit
        return ch & mask ;                                 // using bitwise AND
    }
} 
