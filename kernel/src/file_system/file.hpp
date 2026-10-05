#pragma once
#include <cstdint>
#include <cstddef>
#include "../locking/lock.h"
#include "../stdio.hpp"
#include "drive.hpp"
#include "vsfs_layout.hpp"
#include "../memory/heap.hpp"

#define STDIN          0
#define STDOUT         1
#define STDERR         2

#define SUPERBLOCK_LBA 0
#define NULL_INODE     0
#define FAULTY_INODE   1
#define ROOT_INODE     2

#define FILE            0
#define DIR             1
#define SLINK           2

#define R_PERM          1
#define W_PERM          2
#define E_PERM          4
#define DEFAULT_PERM    (R_PERM | W_PERM)

class Drive;

class FileSystem {
    protected:
    struct superblock *superblock;
    Drive *drive;
    uint8_t *inode_bitmap;
    struct spinlock inode_lock;
    uint8_t *data_bitmap;
    struct spinlock data_lock;
    // hashmap cache of recentlly used inodes
    // inode_nr % cache_size to find index
    struct inode_cache_entry **inode_cache;
    uint32_t inode_cache_size;
    struct spinlock cache_lock;

    // disk blocks are too big to keep in memory
    // you will have to read them from the disk to fetch them
    
    void write_superblock_to_disk();
    // returns the first free data block and marks it occupied in the bitmap in memory (not on disk!)
    // remeber to update both the bitamp and the superblock on disk after calling this
    // returns UINT64_T max if allocation fails
    uint64_t data_bitmap_alloc();
    void data_bitmap_free(uint64_t bit_pos);
    void write_data_bitmap_to_disk();
    // returns the first free inode block and marks it occupied in the bitmap in memory (not on disk!)
    // remeber to update both the bitamp and the superblock on disk after calling this
    // returns UINT64_T max if allocation fails
    uint64_t inode_bitmap_alloc();
    void inode_bitmap_free(uint64_t bit_pos);
    void write_inode_bitmap_to_disk();
    // returns the inode_nr of the file with given name
    // returns the max value of uint32_t if inode is not found
    uint32_t find_inode_nr(struct inode *parent_dir, char name[NAME_MAX_SIZE]);

    // frees the inode structure from memory. does not check if inode is in filesystem cache before freeing
    void free_memory_inode(struct inode *inode);
    // frees the inode structure from disk. update all relevant structures and writes them to the disk
    void free_disk_inode(struct inode *inode);

    // looks for an entry in directory matching name or inode_nr
    // set name to nullptr if you want to search for the entry by inode_nr
    // returns a copy of the entry if found
    // retuns nullptre if entry is not found
    //
    // the 2 pointers are used to return data
    // set block_nr to nullptr if you do not want the block_nr of the found entry
    // set entry_nr to nullptr if you do not want the entry_nr of the found entry
    struct entry *find_entry_in_dir(struct inode *dir, char name[NAME_MAX_SIZE], uint32_t inode_nr,
            uint64_t *block_nr, uint16_t *entry_nr);

    // makes sure that *slot holds a valid physical block number
    // if *slot is 0 and alloc is true it claims a new block, optionally
    // zeroes it, then stores it in *slot
    // does NOT write back the data to the bitmap/superblock
    // zero=true is required for table blocks
    // zero=false is safe only for data blocks
    uint32_t ensure_block(uint32_t *slot, bool alloc, bool zero);

    // reads entry index of the pointer table stored at table_block
    // returns UINT32_MAX for an invalid table or an unallocated (0) entry
    uint32_t indirect_table_entry(uint32_t table_block, uint64_t index);
    // returns the absolute block nr of relative_block_nr
    // returns UIN32_MAX on failure (invalid table, allocated entry)
    uint32_t lookup_block(struct inode *inode, uint32_t relative_block_nr);

    // grows the file so that blocks == new_block_count
    // writes back changes to the disk
    //
    // on failure, it still writes back how many blocks it managed to allocate
    // and returns false
    bool extend(struct inode *inode, uint32_t new_block_count);

    public:
    FileSystem(struct superblock *superblock, Drive *drive);
    virtual ~FileSystem();

    // reads the given inode from the cache
    // if inode is not found on the cache, reads the disk_inode from the disk and adds it to the cache
    // returns nullptr if inode is not found on the disk
    struct inode *inode_get(uint32_t inode_nr);
    // takes an inode out of the cache. decrements inode ref number and frees the structure if ref becomes 0
    void inode_return(uint32_t inode_nr);
    // allocates a new inode, writes it to disk, updates all relevant structures it touches and puts
    // the newly made inode in the parent directory entries
    struct inode *inode_alloc(uint8_t type, uint8_t perms, char name[NAME_MAX_SIZE], struct inode *parent_dir);

    // adds an entry to a directory. updates both parent and target disk inodes on the disk
    void dir_add_and_link_entry(struct inode *parent_dir, struct inode *target, char name[NAME_MAX_SIZE]);

    // removes an entry from a directory data blocks. calls unlink on the entry
    void dir_remove_and_unlink_entry(struct inode *parent_dir, char name[NAME_MAX_SIZE]);

    // resolves the parent of path and removes the entry named after the last component
    // if remove_dir is false, only non-directories are removed
    // if remove_dir is true, only empty directories are removed
    // returns false if the path can't be resolved, "." or "..", the type doesn't match
    // remove_dir, or the directory isn't empty
    bool unlink_path(uint32_t base_inode_nr, const char *path, bool remove_dir);

    bool is_dir_empty(struct inode *dir);

    // prepares a new block (zeroes it out) to be later populated with new entries 
    void prepare_dir_block(struct inode *inode, uint32_t absolute_block_nr);
    // returns a inode pointer to the file in the parent_dir
    // returns nullptr if the file is not found
    struct inode *fetch_inode_in_dir(struct inode *parent_dir, char name[NAME_MAX_SIZE]);

    // returns the inode of a file at the end of the given path
    // needs the inode of the current working directory to be able to resolve relative paths
    struct inode *path_resolver(uint32_t cwd_inode_nr, const char *path);
    // returns the inode of the parent of the file at the end of a given path
    // writes the name of the file in return_name buffer
    struct inode *resolve_parent_dir(uint32_t cwd_inode_nr, const char *path, char return_name[NAME_MAX_SIZE]);

    // returns the absolute block number of an inode's relative_block_nr
    // allocates all the blocks up to the requested block_nr if the alloc flag is set
    // returns UINT32_T if it failes
    //
    // (example: bmap(inode, 3, false) will return the abosolute block number of
    // the inode's 4rd block (in this case, direct_p[3]))
    uint32_t bmap(struct inode *inode, uint32_t relative_block_nr, bool alloc);

    // updates disk_inode structure on the disk
    void write_disk_inode_to_disk(struct inode *inode);
    // decrements and updates hard_link count on the disk
    // can remove the inode from disk if
    // (ref count == 1) meaning this is the only reference/pointer to the inode
    // and if (hard_link == 0)
    // if ref > 1 but hard_link is 0, the inode is added to the orphans list/journaling (TODO)
    void unlink(struct inode *inode);
    // returns false if child is a directory and still has entries inside of it
    bool delete_file(struct inode *parent_dir, struct inode *child);

    uint64_t file_write(struct inode *inode, void *buf, uint64_t size, uint64_t offset);
    uint64_t file_read(struct inode *inode, void *buf, uint64_t size, uint64_t offset);

    // changes size of a file. allocates more blocks if size exceeds current maximum file size
    // does not free blocks, even if the new requested size would not require them
    void resize_file(struct inode *inode, uint64_t size);

    // returns an array of every entry in a dir
    // writes how many entries were found to *count
    // returns nullptr on failure
    struct entry *readdir(struct inode *dir, uint32_t *count);

    // moves/renames the entry src_name in src_dir to dst_name in dst_dir
    // returns false if move fails
    bool move(uint32_t src_dir_nr, char src_name[NAME_MAX_SIZE],
        uint32_t dst_dir_nr, char dst_name[NAME_MAX_SIZE]);
    // wrapper around move. resolves path then calls move
    bool move_path(uint32_t old_inode_nr, const char *old_path,
        uint32_t new_inode_nr, const char *new_path);

    // creates a new hard link, adds an entry dst_name in the dir dst_dir_nr pointing at the inode target_nr
    // returns false if link fails
    bool link(uint32_t dst_dir_nr, uint32_t target_nr, char dst_name[NAME_MAX_SIZE]);
    // wrapper around link(). resolves paths and then calls link
    // returns false if either path can't be resolved or link() fails
    bool link_path(uint32_t old_inode_nr, const char *old_path,
        uint32_t new_inode_nr, const char *new_path);

    // resolves the parent of path, then creates a directory named after the last component
    // returns false if function fails
    bool mkdir_path(uint32_t base_inode_nr, const char *path);
};

class VSFS : public FileSystem {
    public:
    VSFS(struct superblock *superblock, Drive *drive) : FileSystem(superblock, drive) {}
    
    // tries to mount file system. if no superblock is found, it writes all needed file structures onto the blank disk
    static FileSystem *mount_file_system(Drive *drive);

    private:
    // formats disk to vsfs
    static VSFS *write_vsfs(Drive *drive, struct superblock *superblock);
};
