#include <stdio.h>
#include <stdint.h>

typedef struct {
	char name[5];        // Name of the file or directory
	uint8_t used_size;   // Inode state and the size of the file or directory
	uint8_t start_block; // Index of the start file block
	uint8_t dir_parent;  // Inode mode and the index of the parent inode
} Inode;

typedef struct {
	char free_block_list[16];
	Inode inode[126];
} Super_block;

void fs_mount(char *new_disk_name);
void fs_create(char name[5], int size);
void fs_delete(char name[5], uint8_t delete_dir);
void fs_read(char name[5], int block_num);
void fs_write(char name[5], int block_num);
void fs_buff(uint8_t buff[1024]);
void fs_ls(void);
void fs_resize(char name[5], int new_size);
void fs_defrag(void);
void fs_cd(char name[5]);

void tokenize(char *str, const char *delim, char *argv[]);
bool check_name_equal(char name1[5], char name2[5]);
void handle_input(char *tokenized[], char *input_file, int line_num, char *line, ssize_t linelen);
bool check_num_args(char *tokenized[], int num_args);
bool check_name_len(char *name);
bool check_block_num(int block_num);
bool check_file_size(int size);
void print_cmd_error(char *name, int line_num);
int check_file_exist(char name[5]);
bool check_dir_exist(char name[5]);
bool check_has_block(int block_num, char name[5], int target_index);
int get_num_children(int targer_index);
bool check_free_blocks(uint8_t start_block, uint8_t size);
bool is_empty_block(char buff[1024]);
uint8_t set_bit(uint8_t ch, int i, int val);
