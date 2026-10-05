#pragma once

#define MAGIC_NR        0x56534653
#define BLOCK_SIZE      (4 * 1024)
#define NAME_MAX_SIZE   64
#define MAX_PATH_SIZE   256

// be wary of changing this number. it affects the on disk structure of the dinode
// increasing this can lead to the struct increasing in size
#define NR_DIRECT_P             10
// move this to the superblock if file systems with different block sizes are ever implemented
#define INDIRECT_1_CAPACITY     BLOCK_SIZE / sizeof(uint32_t)
#define INDIRECT_2_CAPACITY     BLOCK_SIZE / sizeof(uint32_t) * INDIRECT_1_CAPACITY
#define MAX_BLOCK_NR            NR_DIRECT_P + INDIRECT_1_CAPACITY + INDIRECT_2_CAPACITY

struct inode_cache_entry {
    struct inode_cache_entry *next_entry;
    struct inode *inode_entry;
};

// actual inode structure that will be written on the disk
// try to keep this struct as small as possible, so as to be able to fit
// the maximum ammount of inodes into a block
// if (indirect_p != 0) means that they are allocated
struct disk_inode {
    uint8_t  type;
    uint8_t  perms;
    uint16_t hard_links;    // if 0 => delete inode and clean up data on the disk
    uint32_t blocks;        // number of disk blocks currently allocated to it
    uint64_t size;          // size in bytes of the file
    uint32_t direct_p[NR_DIRECT_P];  // absolute block numbers of data blocks pointers to actual data blocks
    uint32_t indirect_p_1;  // single indirect pointer (contains simple block nr)
    uint32_t indirect_p_2;  // double indirect pointer (contains single indirect pointers)
};

// this structure represents the data a directory writes in its data blocks
// each block will have its own entry count
struct dir_entry {
    uint16_t entry_nr;
    void     *entries;
};

struct entry {
    uint32_t inode_nr;
    char     name[NAME_MAX_SIZE];
};

// inode cache struct kept in memory
struct inode {
    // TODO: decide if the valid field is needed
    // bool     valid;         // has this slot actually been read from disk
    bool     dirty;         // has this inode been modified since it was read
                            // a dirty inode should be written before read
    uint32_t inode_nr;
    uint32_t ref;           // 0 => delete struct inode
    struct disk_inode *data;
};

struct file {
    bool            readable;
    bool            writable;
    struct spinlock lock;
    uint32_t        ref;           // how many fds refer to this file (0 => deletion of only the file structs)
    uint64_t        off;
    struct inode    *ip;
};

// sections will be arranged like so on the disk
// superblock, inode bitmap, data bitmap, inode table, data table
struct superblock {
    uint32_t magic_nr;
    uint32_t block_size;
    uint32_t total_blocks;

    uint32_t inode_bitmap_start;    // in blocks
    uint32_t inode_bitmap_blocks;
    uint32_t data_bitmap_start;     // in blocks
    uint32_t data_bitmap_blocks;
    uint32_t inode_table_start;     // in blocks
    uint32_t inode_table_blocks;
    uint32_t data_start;            // in blocks
    uint32_t data_blocks;

    uint32_t inode_count;           // max possible number of inodes
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint32_t root_inode;
};
