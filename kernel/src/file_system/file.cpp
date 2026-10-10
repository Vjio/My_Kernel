#include "file.hpp"
#include "../bitmap.hpp"
#include "../string.hpp"
#include "../memory/heap.hpp"
#include "../memory/memory.hpp"
#include "../string.hpp"

FileSystem::FileSystem(struct superblock *superblock, Drive *drive)
    : superblock(superblock), drive(drive), inode_bitmap(nullptr), data_bitmap(nullptr)  {
    if (superblock) {
        inode_bitmap = reinterpret_cast<uint8_t *>(calloc(1, superblock->inode_bitmap_blocks * superblock->block_size));
        data_bitmap = reinterpret_cast<uint8_t *>(calloc(1, superblock->data_bitmap_blocks * superblock->block_size));

        // TODO: handle these failure paths somehow if they ever happen
        // (retry reading a few times? thus would point to some kind of bug in the driver
        // or a faulty disk)
        if (!drive->disk_read(superblock->inode_bitmap_start * superblock->block_size,
            superblock->inode_bitmap_blocks * superblock->block_size, inode_bitmap)) {
                printf("failed to read inode bitmap from disk!\n");
                while (true) {;}       
        }
        inode_lock.locked = false;

        if (!drive->disk_read(superblock->data_bitmap_start * superblock->block_size,
            superblock->data_bitmap_blocks * superblock->block_size, data_bitmap)) {
                printf("failed to read data bitmap from disk!\n");
                while (true) {;}       
        }
        data_lock.locked = false;

        inode_cache_size = superblock->inode_count / 8;
        if (inode_cache_size < 8)
            inode_cache_size = 8;
        inode_cache = reinterpret_cast<struct inode_cache_entry **>(calloc(inode_cache_size, sizeof(struct inode_cache_entry *)));
        cache_lock.locked = false;
    }
}

FileSystem::~FileSystem() {
    for (int i = 0; i < inode_cache_size; i++) {
        // dont try to grab cache entry lock. inode_return also needs the lock
        while (inode_cache[i] != nullptr) {
            // inode return updates the head of the cache
            inode_cache[i]->inode_entry->ref = 1;
            inode_return(inode_cache[i]->inode_entry->inode_nr);
        }
    }
    free(inode_cache);

    free(inode_bitmap);
    free(data_bitmap);
    free(superblock);
}

VSFS *VSFS::write_vsfs(Drive *drive, struct superblock *superblock) {
    superblock->magic_nr = MAGIC_NR;
    superblock->root_inode = ROOT_INODE;
    superblock->block_size = BLOCK_SIZE;

    superblock->total_blocks = drive->get_capacity_bytes() / superblock->block_size;
    if (superblock->total_blocks < 10) {
        printf("vsfs: hard drive is way too small!\n");
        free(superblock);
        return nullptr;
    }

    uint8_t zero_buf[superblock->block_size];
    memset(zero_buf, 0, superblock->block_size);

    // TODO: increase inode_count if inodes run out. or decrease if there are too many
    superblock->inode_table_blocks = superblock->total_blocks / 10;
    superblock->inode_count = superblock->inode_table_blocks * superblock->block_size
        / sizeof(struct disk_inode);

    // 3 because first block is null block
    // 2nd block is faulty blocks block (for damaged sectors)
    // 3rd block is the root
    superblock->free_inodes = superblock->inode_count - 3;

    // block 0 will be the superblock itself. a bit wasteful. could use minor blocks later
    superblock->inode_bitmap_start = 1;
    uint32_t inode_bitmap_bytes = (superblock->inode_count + 7) / 8;
    superblock->inode_bitmap_blocks = (inode_bitmap_bytes + superblock->block_size - 1) / superblock->block_size;
    // zero out bitmap
    for (int i = 0; i < superblock->inode_bitmap_blocks; i++) {
        drive->disk_write((superblock->inode_bitmap_start + i) * superblock->block_size,
            superblock->block_size, zero_buf);
    }

    superblock->data_bitmap_start = superblock->inode_bitmap_start + superblock->inode_bitmap_blocks;
    superblock->data_blocks = superblock->total_blocks - superblock->inode_table_blocks -
        superblock->inode_bitmap_blocks - 1;    // -1 for the superblock

    // the crux: we have an x ammount of block empty. we need to find out the smallest y number of blocks
    // such that (x - y) blocks can be presented in a bitmap that would take y * BLOCK_SIZE bits
    uint32_t bits_per_block = superblock->block_size * 8;
    uint32_t blocks_per_group = bits_per_block + 1;
    // ceil(data_blocks / blocks_per_group)
    superblock->data_bitmap_blocks = (superblock->data_blocks + blocks_per_group - 1) / blocks_per_group;
    // zero out bitmap
    for (int i = 0; i < superblock->data_bitmap_blocks; i++) {
        drive->disk_write((superblock->data_bitmap_start + i) * superblock->block_size,
            superblock->block_size, zero_buf);
    }

    superblock->data_blocks -= superblock->data_bitmap_blocks;
    // first block is the root
    superblock->free_blocks = superblock->data_blocks - 1;

    superblock->inode_table_start = superblock->data_bitmap_start + superblock->data_bitmap_blocks;
    superblock->data_start = superblock->inode_table_start + superblock->inode_table_blocks;

    memmove(zero_buf, superblock, sizeof(struct superblock));
    drive->disk_write(SUPERBLOCK_LBA, superblock->block_size, reinterpret_cast<void *>(zero_buf));

    memset(zero_buf, 0, superblock->block_size);
    uint8_t inode_bitmap = 0b00000111;
    zero_buf[0] = inode_bitmap;
    drive->disk_write(superblock->inode_bitmap_start * superblock->block_size,
            superblock->block_size, reinterpret_cast<void *>(zero_buf));

    uint8_t data_bitmap  = 0b00000001;
    zero_buf[0] = inode_bitmap;
    drive->disk_write(superblock->data_bitmap_start * superblock->block_size,
            superblock->block_size, reinterpret_cast<void *>(zero_buf));

    struct disk_inode *inodes = reinterpret_cast<struct disk_inode *>(
        calloc(3, sizeof(struct disk_inode)));
    // calloc zeroes out the NULL inode and the inode for faulty/damaged blocks
    // make the root inode
    inodes[ROOT_INODE].type = DIR;
    inodes[ROOT_INODE].perms = R_PERM | W_PERM;
    inodes[ROOT_INODE].hard_links = 2;                          // "." and ".."
    inodes[ROOT_INODE].blocks = 1;
    inodes[ROOT_INODE].direct_p[0] = superblock->data_start;
    inodes[ROOT_INODE].size = 2 * sizeof(struct entry);

    drive->disk_write(superblock->inode_table_start * superblock->block_size,
            3 * sizeof(struct disk_inode), inodes);
    free(inodes);

    struct entry *entries = reinterpret_cast<struct entry *>(
        calloc(2, sizeof(struct entry)));
    entries[0].inode_nr = ROOT_INODE;
    memcpy(entries[0].name, ".\0", 2);
    entries[1].inode_nr = ROOT_INODE;
    memcpy(entries[1].name, "..\0", 3);
    
    struct dir_entry dir_entry;
    dir_entry.entry_nr = 2;
    dir_entry.entries = entries;

    drive->disk_write(superblock->data_start * superblock->block_size,
        sizeof(dir_entry.entry_nr), reinterpret_cast<void *>(&dir_entry.entry_nr));
    drive->disk_write(superblock->data_start * superblock->block_size + sizeof(dir_entry.entry_nr),
        dir_entry.entry_nr * sizeof(struct entry), entries);
    free(entries);

    return new VSFS(superblock, drive);
}

FileSystem *VSFS::mount_file_system(Drive *drive) {
    uint32_t sector_size = drive->get_sector_size();
    uint32_t sectors_to_read = (sizeof(struct superblock) + sector_size - 1) / sector_size;

    uint8_t *raw = reinterpret_cast<uint8_t *>(malloc(sectors_to_read * sector_size));
    if (!drive->disk_read(0, sizeof(struct superblock), raw)) {
        printf("failed to read superblock from disk!\n");
        free(raw);
        return nullptr;
    }

    struct superblock *superblock = reinterpret_cast<struct superblock *>(malloc(sizeof(struct superblock)));
    if (superblock == nullptr) {
        printf("failed to allocate superblock!\n");
        free(raw);
        return nullptr;
    }

    memcpy(superblock, raw, sizeof(struct superblock));
    free(raw);

    if (superblock->magic_nr != MAGIC_NR) {
        // invalid magic_nr, reformat the drive
        return write_vsfs(drive, superblock);
    }

    if (superblock->block_size != BLOCK_SIZE || superblock->total_blocks == 0 ||
        superblock->inode_table_start >= superblock->total_blocks) {
        // TODO: write graceful failure/recovery path if this happens
        printf("superblock present but looks corrupt!\n");
        free(superblock);
        return nullptr;
    }

    return new VSFS(superblock, drive);
}

uint64_t FileSystem::data_bitmap_alloc() {
    acquire(&data_lock);
    uint64_t ret = set_first_free_zero(data_bitmap, superblock->data_bitmap_blocks * superblock->block_size);

    if (ret == UINT64_MAX) {
        release(&data_lock);
        return ret;
    }

    if (ret >= superblock->data_blocks) {
        clear_bit(data_bitmap, ret);
        release(&data_lock);
        return UINT64_MAX;
    }

    superblock->free_blocks--;
    release(&data_lock);
    return ret;
}

void FileSystem::data_bitmap_free(uint64_t bit_pos) {
    if (bit_pos == UINT64_MAX)
        return;

    acquire(&data_lock);
    if (test_bit(data_bitmap, bit_pos)) {
        superblock->free_blocks++;
        clear_bit(data_bitmap, bit_pos);
    }
    release(&data_lock);
}

void FileSystem::write_data_bitmap_to_disk() {
    drive->disk_write(superblock->data_bitmap_start * superblock->block_size, superblock->data_bitmap_blocks *
        superblock->block_size, data_bitmap);
}

uint64_t FileSystem::inode_bitmap_alloc() {
    acquire(&inode_lock);
    uint64_t ret = set_first_free_zero(inode_bitmap, superblock->inode_bitmap_blocks * superblock->block_size);

    if (ret == UINT64_MAX) {
        release(&inode_lock);
        return ret;
    }

    if (ret >= superblock->inode_count) {
        clear_bit(inode_bitmap, ret);
        release(&inode_lock);
        return UINT64_MAX;
    }

    superblock->free_inodes--;
    release(&inode_lock);
    return ret;
}

void FileSystem::inode_bitmap_free(uint64_t bit_pos) {
    if (bit_pos == UINT64_MAX)
        return;

    acquire(&inode_lock);
    if (test_bit(inode_bitmap, bit_pos)) {
        superblock->free_inodes++;
        clear_bit(inode_bitmap, bit_pos);
    }
    release(&inode_lock);
}

void FileSystem::write_inode_bitmap_to_disk() {
    drive->disk_write(superblock->inode_bitmap_start * superblock->block_size, superblock->inode_bitmap_blocks *
        superblock->block_size, inode_bitmap);
}

void FileSystem::prepare_dir_block(struct inode *inode, uint32_t absolute_block_nr) {
    char *buf = reinterpret_cast<char *>(calloc(1, superblock->block_size));
    drive->disk_write(absolute_block_nr * superblock->block_size, superblock->block_size,
        buf);
    free(buf);
}

uint32_t FileSystem::ensure_block(uint32_t *slot, bool alloc, bool zero) {
    if (slot[0] != 0)
        return slot[0];

    if (!alloc)
        return UINT32_MAX;

    uint64_t pos = data_bitmap_alloc();
    if (pos == UINT64_MAX)
        return UINT32_MAX;

    slot[0] = superblock->data_start + pos;

    if (zero) {
        char *zero_buf = reinterpret_cast<char *>(calloc(1, superblock->block_size));
        drive->disk_write((uint64_t)slot[0] * superblock->block_size, superblock->block_size, zero_buf);
        free(zero_buf);
    }

    return slot[0];
}

uint32_t FileSystem::indirect_table_entry(uint32_t table_block, uint64_t index) {
    if (table_block == 0 || table_block == UINT32_MAX)
        return UINT32_MAX;

    uint32_t *buf = reinterpret_cast<uint32_t *>(calloc(1, superblock->block_size));
    if (buf == nullptr)
        return UINT32_MAX;

    drive->disk_read(static_cast<uint64_t>(table_block) * superblock->block_size,
                     superblock->block_size, buf);

    uint32_t value = buf[index];
    free(buf);

    if (value == 0)
        value = UINT32_MAX;
    return value;
}

uint32_t FileSystem::lookup_block(struct inode *inode, uint32_t relative_block_nr) {
    if (relative_block_nr < NR_DIRECT_P)
        return inode->data->direct_p[relative_block_nr];

    uint64_t off = relative_block_nr - NR_DIRECT_P;

    if (off < INDIRECT_1_CAPACITY)
        return indirect_table_entry(inode->data->indirect_p_1, off);

    off -= INDIRECT_1_CAPACITY;

    uint32_t second = indirect_table_entry(inode->data->indirect_p_2, off / INDIRECT_1_CAPACITY);
    return indirect_table_entry(second, off % INDIRECT_1_CAPACITY);
}

bool FileSystem::extend(struct inode *inode, uint32_t new_block_count) {
    const uint32_t block_size = superblock->block_size;

    struct table {
        uint32_t *buf   = nullptr;
        uint32_t  block = 0;
        bool      dirty = false;
    };

    auto init_table_struct = [&](table &t, uint32_t block) -> bool {
        t.buf = reinterpret_cast<uint32_t *>(calloc(1, block_size));
        if (t.buf == nullptr)
            return false;

        t.block = block;
        t.dirty = false;
        drive->disk_read(static_cast<uint64_t>(block) * block_size, block_size, t.buf);
        return true;
    };

    auto write_and_free = [&](table &t) {
        if (t.buf != nullptr && t.dirty)
            drive->disk_write(static_cast<uint64_t>(t.block) *block_size, block_size, t.buf);
        free(t.buf);
        t.buf = nullptr;
        t.dirty = false;
    };

    // allocates (if needed) the table referenced by `slot` and inits the local structure
    bool meta_dirty = false;
    auto open_table = [&](table &t, uint32_t *slot) -> bool {
        bool allocated = (slot[0] == 0);
        if (ensure_block(slot, true, true) == UINT32_MAX)
            return false;

        if (allocated)
            meta_dirty = true;
        return init_table_struct(t, slot[0]);
    };

    table    indirect_p_1;
    table    indirect_p_2;
    table    curr_ind_p_2;
    uint64_t p2_idx = 0;

    bool ok = true;
    while (ok && inode->data->blocks < new_block_count) {
        uint32_t *n = &inode->data->blocks;

        // direct p
        while (n[0] < new_block_count && n[0] < NR_DIRECT_P) {
            if (ensure_block(&inode->data->direct_p[n[0]], true, true) == UINT32_MAX) {
                ok = false;
                break;
            }
            n[0]++;
            meta_dirty = true;
        }

        if (!ok)
            goto end;

        // 1st indirect p
        uint64_t offset;
        while (n[0] < new_block_count && (n[0] - NR_DIRECT_P) < INDIRECT_1_CAPACITY) {
            offset = n[0] - NR_DIRECT_P;
            if (indirect_p_1.buf == nullptr && !open_table(indirect_p_1, &inode->data->indirect_p_1)) {
                ok = false;
                break;
            }

            if (ensure_block(&indirect_p_1.buf[offset], true, true) == UINT32_MAX) {
                ok = false;
                break;
            }

            indirect_p_1.dirty = true;
            n[0]++;
            meta_dirty = true;
        }

        if (!ok)
            goto end;

        // 2nd indirect pointer
        while (n[0] < new_block_count && (n[0] - NR_DIRECT_P - INDIRECT_1_CAPACITY) < INDIRECT_2_CAPACITY) {
            offset = n[0] - NR_DIRECT_P - INDIRECT_1_CAPACITY;
            uint64_t big   = offset / INDIRECT_1_CAPACITY;
            uint64_t small = offset % INDIRECT_1_CAPACITY;

            if (indirect_p_2.buf == nullptr && !open_table(indirect_p_2, &inode->data->indirect_p_2)) {
                ok = false;
                break;
            }

            if (curr_ind_p_2.buf == nullptr || big != p2_idx) {
                // write back the previous second-level table if modified
                write_and_free(curr_ind_p_2);

                bool allocated = (indirect_p_2.buf[big] == 0);
                if (!open_table(curr_ind_p_2, &indirect_p_2.buf[big])) {
                    ok = false;
                    break;
                }
                p2_idx = big;
                if (allocated)
                    indirect_p_2.dirty = true;
            }

            if (ensure_block(&curr_ind_p_2.buf[small], true, true) == UINT32_MAX) {
                ok = false;
                break;
            }

            curr_ind_p_2.dirty = true;
            n[0]++;
            meta_dirty = true;
        }
    }

end:
    write_and_free(indirect_p_1);
    write_and_free(indirect_p_2);
    write_and_free(curr_ind_p_2);

    if (meta_dirty) {
        write_data_bitmap_to_disk();
        write_disk_inode_to_disk(inode);
        write_superblock_to_disk();
    }

    return ok;
}

uint32_t FileSystem::bmap(struct inode *inode, uint32_t relative_block_nr, bool alloc) {
    if (relative_block_nr >= MAX_BLOCK_NR)
        return UINT32_MAX;

    if (relative_block_nr >= inode->data->blocks) {
        if (!alloc)
            return UINT32_MAX;

        if (!extend(inode, relative_block_nr + 1))
            return UINT32_MAX;
    }

    return lookup_block(inode, relative_block_nr);
}

struct inode *FileSystem::inode_alloc(uint8_t type, uint8_t perms, char name[NAME_MAX_SIZE], struct inode *parent_dir) {
    if (parent_dir->data->type != DIR)
        return nullptr;

    if (find_inode_nr(parent_dir, name) != UINT32_MAX)
        return nullptr;

    bool failure = false;
    uint32_t index;
    uint64_t inode_nr = UINT64_MAX;
    uint64_t block = UINT64_MAX;
    struct inode *inode = nullptr;
    struct disk_inode *disk_inode = nullptr;
    struct inode_cache_entry *new_entry = nullptr;

    inode_nr = inode_bitmap_alloc();
    if (inode_nr == UINT64_MAX) {
        failure = true;
        goto clean_up;
    }

    block = data_bitmap_alloc();
    if (block == UINT64_MAX) {
        failure = true;
        goto clean_up;
    }

    inode = reinterpret_cast<struct inode *>(calloc(1, sizeof(struct inode)));
    if (inode == nullptr) {
        failure = true;
        goto clean_up;
    }

    disk_inode = reinterpret_cast<struct disk_inode *>(calloc(1, sizeof(struct disk_inode)));
    if (disk_inode == nullptr) {
        failure = true;
        goto clean_up;
    }

    inode->inode_nr = inode_nr;
    inode->dirty = false;
    inode->data = disk_inode;
    inode->ref = 1;

    disk_inode->blocks = 1;
    disk_inode->perms = perms;
    disk_inode->direct_p[0] = superblock->data_start + block;
    disk_inode->hard_links = 0;
    disk_inode->size = 0;
    disk_inode->type = type;

    // add inode to cache
    index = inode->inode_nr % inode_cache_size;
    new_entry = reinterpret_cast<struct inode_cache_entry *> (calloc(1, sizeof(struct inode_cache_entry)));
    if (new_entry == nullptr) {
        failure = true;
        goto clean_up;
    }

    acquire(&cache_lock);
    new_entry->inode_entry = inode;
    new_entry->next_entry = inode_cache[index];
    inode_cache[index] = new_entry;
    release(&cache_lock);

    // link inode to parent
    failure = !(dir_add_and_link_entry(parent_dir, inode, name));
    if (failure)
        // disk is full
        goto clean_up;

    if (type == DIR) {
        prepare_dir_block(inode, disk_inode->direct_p[0]);
        failure = !(dir_add_and_link_entry(inode, inode, ".\0")) || 
            !(dir_add_and_link_entry(inode, parent_dir, "..\0"));

        if (failure) {
            // failling to write entries to the first direct pointer of a directory
            // should never happen. the block is already allocated (data_bitmap_alloc succeeded).
            printf("weird bug in inode_alloc!\n");
            while (true) {}
        }
    }

    // update disk
    write_inode_bitmap_to_disk();
    write_data_bitmap_to_disk();
    write_disk_inode_to_disk(inode);
    write_superblock_to_disk();

clean_up:
    if (failure) {
        inode_bitmap_free(inode_nr);
        data_bitmap_free(block);
        if (inode)
            free(inode);
        if (disk_inode)
            free(disk_inode);
        return nullptr;
    }

    return inode;
}

bool FileSystem::dir_add_and_link_entry(struct inode *parent_dir, struct inode *target, char name[NAME_MAX_SIZE]) {
    const uint32_t entries_per_block = (superblock->block_size - sizeof(uint16_t)) / sizeof(struct entry);

    uint64_t current_block_offset = parent_dir->data->direct_p[0] * static_cast<uint64_t>(superblock->block_size);

    uint16_t entry_nr;
    drive->disk_read(current_block_offset, sizeof(entry_nr), &entry_nr);

    struct entry new_entry = {};
    new_entry.inode_nr = target->inode_nr;
    size_t name_len = strlen(name);
    if (name_len >= NAME_MAX_SIZE)
        name_len = NAME_MAX_SIZE - 1;
    memcpy(new_entry.name, name, name_len);

    uint64_t block_nr;
    uint64_t block_read = 1;
    uint64_t target_offset = current_block_offset;
    // find first block that's not full
    while (entry_nr >= entries_per_block) {
        // need to read the next block of the dir
        // check if block is allocated
        if (parent_dir->data->blocks <= block_read) {
            block_nr = bmap(parent_dir, block_read, true);
            if (block_nr == UINT32_MAX) {
                // disk is full, cannot add entry to dir
                return false;
            }

            prepare_dir_block(parent_dir, block_nr);
            target_offset = superblock->block_size * block_nr;
            entry_nr = 0;

        } else {
            block_nr = bmap(parent_dir, block_read, false);
            if (block_nr == UINT32_MAX) {
                // this should never happen
                printf("weird error in dir_add_and_link_entry\n");
                return false;
            }

            target_offset = superblock->block_size * block_nr;
            drive->disk_read(target_offset, sizeof(entry_nr), &entry_nr);
            block_read++;
        }
    }

    // update the entry nr
    entry_nr++;
    drive->disk_write(target_offset, sizeof(entry_nr), &entry_nr);
    // write the new entry to the disk
    drive->disk_write(target_offset + sizeof(entry_nr) + (entry_nr - 1) * sizeof(struct entry),
        sizeof(struct entry), &new_entry);

    // update disk inodes
    parent_dir->data->size += sizeof(struct entry);
    target->data->hard_links++;

    // once some kind of buffering is implemented, this kinds of writes should be done in batch
    // (so that making a . and .. entry would not create double the ammount of IO calls)
    // parent_dir->dirty = true;

    write_disk_inode_to_disk(parent_dir);
    write_disk_inode_to_disk(target);
    return true;
}

struct inode *FileSystem::inode_get(uint32_t inode_nr) {
    if (inode_nr >= superblock->inode_count || !test_bit(inode_bitmap, inode_nr))
        return nullptr;

    acquire(&cache_lock);

    // check if inode_nr is in cache
    uint32_t index = inode_nr % inode_cache_size;
    struct inode_cache_entry *temp = inode_cache[index];
    while (temp != nullptr && temp->inode_entry->inode_nr != inode_nr) {
        temp = temp->next_entry;
    }

    if (temp != nullptr) {
        temp->inode_entry->ref++;
        release(&cache_lock);
        return temp->inode_entry;
    }

    // read inode from disk and add it to cache
    struct disk_inode *new_disk_inode = reinterpret_cast<struct disk_inode *>(calloc(1, sizeof(struct disk_inode)));
    if (new_disk_inode == nullptr) {
        release(&cache_lock);
        return nullptr;
    }

    struct inode *new_inode = reinterpret_cast<struct inode *>(calloc(1, sizeof(struct inode)));
    if (new_inode == nullptr) {
        release(&cache_lock);
        free(new_disk_inode);
        return nullptr;
    }

    drive->disk_read(superblock->inode_table_start * superblock->block_size + inode_nr * sizeof(struct disk_inode),
        sizeof(struct disk_inode), new_disk_inode);

    new_inode->data = new_disk_inode;
    new_inode->ref = 1;
    new_inode->inode_nr = inode_nr;
    new_inode->dirty = false;

    struct inode_cache_entry *new_entry = reinterpret_cast<struct inode_cache_entry *>
        (calloc(1, sizeof(struct inode_cache_entry)));
    if (new_entry == nullptr) {
        release(&cache_lock);
        free(new_disk_inode);
        free(new_inode);
        return nullptr;
    }

    new_entry->inode_entry = new_inode;
    new_entry->next_entry = inode_cache[index];
    inode_cache[index] = new_entry;

    release(&cache_lock);
    return new_inode;
}

void FileSystem::write_superblock_to_disk() {
    drive->disk_write(SUPERBLOCK_LBA, sizeof(struct superblock), superblock);
}

void FileSystem::write_disk_inode_to_disk(struct inode *inode) {
    if (inode == nullptr)
        return;

    drive->disk_write(superblock->inode_table_start * superblock->block_size + inode->inode_nr * sizeof(struct disk_inode),
        sizeof(struct disk_inode), inode->data);
}

void FileSystem::free_disk_inode(struct inode *inode) {
    // mark data blocks as free
    for (uint16_t i = 0; i < NR_DIRECT_P; i++)
        if (inode->data->direct_p[i])
            data_bitmap_free(inode->data->direct_p[i] - superblock->data_start);

    // TODO: check those mallocs
    if (inode->data->indirect_p_1) {
        uint32_t *indirect_p_1_table = reinterpret_cast<uint32_t *>(malloc(superblock->block_size));

        drive->disk_read(inode->data->indirect_p_1 * static_cast<uint64_t>(superblock->block_size),
            superblock->block_size, indirect_p_1_table);

        for (uint32_t i = 0; i >= 0 && i < inode->data->blocks - NR_DIRECT_P && i < INDIRECT_1_CAPACITY; i++) {
            if (indirect_p_1_table[i])
                data_bitmap_free(indirect_p_1_table[i] - superblock->data_start);            
        }
        
        data_bitmap_free(inode->data->indirect_p_1 - superblock->data_start);
        free(indirect_p_1_table);
    }

    if (inode->data->indirect_p_2) {
        uint32_t *indirect_p_2_table = reinterpret_cast<uint32_t *>(malloc(superblock->block_size));
        uint32_t *curr_p_1_table     = reinterpret_cast<uint32_t *>(malloc(superblock->block_size));

        drive->disk_read(inode->data->indirect_p_2 * static_cast<uint64_t>(superblock->block_size),
            superblock->block_size, indirect_p_2_table);
        drive->disk_read(indirect_p_2_table[0] * static_cast<uint64_t>(superblock->block_size),
            superblock->block_size, curr_p_1_table);

        uint32_t p2_index = 0;
        uint32_t p1_index = 0;
        for (uint32_t i = 0; i >= 0 && i < inode->data->blocks - NR_DIRECT_P - INDIRECT_1_CAPACITY; i++) {
            if (p1_index == INDIRECT_1_CAPACITY) {
                data_bitmap_free(indirect_p_2_table[p2_index] - superblock->data_start);
                drive->disk_read(indirect_p_2_table[++p2_index] * static_cast<uint64_t>(superblock->block_size),
                    superblock->block_size, curr_p_1_table);
                p1_index = 0;
            }

            if (curr_p_1_table[p1_index])
                data_bitmap_free(curr_p_1_table[p1_index] - superblock->data_start);
                
            p1_index++;
        }

        // free the last p1 table
        if (p1_index)
            data_bitmap_free(indirect_p_2_table[p2_index] - superblock->data_start);
        // free the p2 table itself
        data_bitmap_free(inode->data->indirect_p_2 - superblock->data_start);
        free(indirect_p_2_table);
        free(curr_p_1_table);
    }

    // mark inode block as free
    inode_bitmap_free(inode->inode_nr);
    write_data_bitmap_to_disk();
    write_inode_bitmap_to_disk();
    write_superblock_to_disk();
} 

void FileSystem::free_memory_inode(struct inode *inode) {
    free(inode->data);
    free(inode);
}

void FileSystem::inode_return(uint32_t inode_nr) {
    // check if inode_nr is in cache
    uint32_t index = inode_nr % inode_cache_size;
    struct inode_cache_entry *temp = nullptr;
    struct inode_cache_entry *prev = nullptr;
    
    acquire(&cache_lock);
    temp = inode_cache[index];
    while (temp != nullptr && temp->inode_entry->inode_nr != inode_nr) {
        prev = temp;
        temp = temp->next_entry;
    }

    if (temp == nullptr) {
        // no work to do
        release(&cache_lock);
        return;
    }

    temp->inode_entry->ref--;
    if (temp->inode_entry->ref == 0) {
        // remove entry from cache
        if (prev == nullptr)
            inode_cache[index]= temp->next_entry;
        else
            prev->next_entry = temp->next_entry;

        if (temp->inode_entry->data->hard_links == 0) {
            // TODO: remove inode from journaling/orphan list
            free_disk_inode(temp->inode_entry);
        }

        // free allocated memory
        free_memory_inode(temp->inode_entry);
        free(temp);
    }
    release(&cache_lock);
}

struct entry *FileSystem::find_entry_in_dir(struct inode *dir, char name[NAME_MAX_SIZE], uint32_t inode_nr,
        uint64_t *requested_block_nr, uint16_t *requested_entry_nr) {
    struct entry *requested_entry = nullptr;
    uint64_t current_block_offset = dir->data->direct_p[0] * static_cast<uint64_t>(superblock->block_size);

    uint16_t entry_nr;
    drive->disk_read(current_block_offset, sizeof(entry_nr), &entry_nr);
    
    // read all of the entries to save on I/O calls
    uint64_t occupied_space = entry_nr * sizeof(struct entry);
    struct entry *entries = reinterpret_cast<struct entry *>(calloc(1, superblock->block_size));
    drive->disk_read(current_block_offset + sizeof(uint16_t), occupied_space, entries);

    // scan every entry until we find the requested entry
    uint64_t blocks_read = 0;
    for (uint16_t i = 0; i <= entry_nr; i++) {
        if (i == entry_nr) {
            blocks_read++;
            if (blocks_read == dir->data->blocks)
                break;

            uint64_t block_nr =  bmap(dir, blocks_read, false);
            if (block_nr == UINT32_MAX) {
                // this should never happen
                printf("weird bug in find_entry_in_dir!\n");
                while(true) {;}
            }

            current_block_offset = block_nr * superblock->block_size;
            drive->disk_read(current_block_offset, sizeof(entry_nr), &entry_nr);
            occupied_space = entry_nr * sizeof(struct entry);
            drive->disk_read(current_block_offset + sizeof(uint16_t), occupied_space, entries);
            i = -1;

        } else {
            if ((name != nullptr && strncmp(entries[i].name, name, NAME_MAX_SIZE) == 0) ||
                    inode_nr == entries[i].inode_nr) {
                requested_entry = reinterpret_cast<struct entry *>(calloc(1, sizeof(struct entry)));
                requested_entry->inode_nr = entries[i].inode_nr;
                memcpy(requested_entry->name, entries[i].name, NAME_MAX_SIZE);

                if (requested_block_nr != nullptr)
                    *requested_block_nr = current_block_offset / superblock->block_size;

                if (requested_entry_nr != nullptr)
                    *requested_entry_nr = i;

                break;
            }
        }
    }
    
    free(entries);
    return requested_entry;
}

uint32_t FileSystem::find_inode_nr(struct inode *parent_dir, char name[NAME_MAX_SIZE]) {
    struct entry *requested_entry = find_entry_in_dir(parent_dir, name, -1, nullptr, nullptr);
    if (requested_entry == nullptr)
        return UINT32_MAX;

    uint32_t inode_nr = requested_entry->inode_nr;
    free(requested_entry);

    return inode_nr;
}

struct inode *FileSystem::fetch_inode_in_dir(struct inode *parent_dir, char name[NAME_MAX_SIZE]) {
    uint32_t inode_nr = find_inode_nr(parent_dir, name);
    if (inode_nr == UINT32_MAX)
        return nullptr;

    return inode_get(inode_nr);
}

void FileSystem::unlink(struct inode *inode) {
    inode->data->hard_links--;
    if (inode->data->hard_links == 0) {
        if (inode->ref > 1) {
        // TODO: journaling. orphan list. a way of tracking these kinds of inodes
        // if they only have in memory refs but the OS crashes, file space is leaked

        // still need to keep the inode on the disk
        write_disk_inode_to_disk(inode);
        } else {
            // ref count is 1. this means that the reference this functions holds is the only reference to the inode
            // disk inode is no longer needed
            free_disk_inode(inode);
            // the caller who unlinked the inode will call inode_return. dont call it here
        }
    } else {
        write_disk_inode_to_disk(inode);
    }
}

void FileSystem::dir_remove_and_unlink_entry(struct inode *parent_dir, char name[NAME_MAX_SIZE]) {
    uint64_t current_block_offset = parent_dir->data->direct_p[0] * static_cast<uint64_t>(superblock->block_size);
    uint64_t blocks_read = 0;

    uint16_t entry_nr;
    drive->disk_read(current_block_offset, sizeof(entry_nr), &entry_nr);
    
    // read all of the entries in the first block to save on I/O calls
    uint64_t occupied_space = entry_nr * sizeof(struct entry);
    struct entry *entries = reinterpret_cast<struct entry *>(calloc(1, superblock->block_size));
    drive->disk_read(current_block_offset + sizeof(uint16_t), occupied_space, entries);

    // scan every entry until we find the requested entry
    for (uint16_t i = 0; i <= entry_nr; i++) {
        if (i == entry_nr) {
            // current block has been exhausted. move on to next block
            blocks_read++;
            if (blocks_read == parent_dir->data->blocks) {
                free(entries);
                return;
            }

            uint32_t block_nr =  bmap(parent_dir, blocks_read, false);
            if (block_nr == UINT32_MAX) {
                // this should never happen
                free(entries);
                printf("weird bug in dir_remove_and_unlink_entry!\n");
                while(true) {;}
            }

            current_block_offset = block_nr * superblock->block_size;
            drive->disk_read(current_block_offset, sizeof(entry_nr), &entry_nr);
            occupied_space = entry_nr * sizeof(struct entry);
            drive->disk_read(current_block_offset + sizeof(uint16_t), occupied_space, entries);
            i = -1;

        } else {
            if (strncmp(entries[i].name, name, NAME_MAX_SIZE) == 0){
                struct inode *curr_inode = inode_get(entries[i].inode_nr);
                if (curr_inode == nullptr) {
                    // inode is found in a directory entry but not on the disk
                    // some kind of disk corruption has happened
                    printf("BUG in dir_remove_and_unlick!\n");
                    break;
                }

                // remove entry from dir
                memmove(entries + i, entries + i + 1, (entry_nr - i - 1) * sizeof(struct entry));

                entry_nr--;
                drive->disk_write(current_block_offset, sizeof(entry_nr), &entry_nr);
                drive->disk_write(current_block_offset + sizeof(uint16_t), entry_nr * sizeof(struct entry), entries);

                // update inode
                parent_dir->data->size -= sizeof(entry);
                drive->disk_write(superblock->inode_table_start * superblock->block_size + parent_dir->inode_nr * sizeof(struct disk_inode),
                    sizeof(struct disk_inode), parent_dir->data);

                unlink(curr_inode);
                inode_return(curr_inode->inode_nr);

                break;
            }
        }
    }
    free(entries);
}

bool FileSystem::is_dir_empty(struct inode *dir) {
    return dir->data->size <= 2 * sizeof(struct entry);
}

bool FileSystem::delete_file(struct inode *parent_dir, struct inode *child) {
    if (child->inode_nr == ROOT_INODE || child == parent_dir)
        return false;

    // check if child is a directory and is not empty
    if (child->data->type == DIR && !is_dir_empty(child))
        return false;

    struct entry *child_entry = find_entry_in_dir(parent_dir, nullptr, child->inode_nr, nullptr, nullptr);
    if (child_entry == nullptr)
        return false;

    dir_remove_and_unlink_entry(parent_dir, child_entry->name);

    if (child && child->data->type == DIR) {
        dir_remove_and_unlink_entry(child, "..\0");
        dir_remove_and_unlink_entry(child, ".\0");
    }

    free(child_entry);
    return true;
}

struct inode *FileSystem::path_resolver(uint32_t cwd_inode_nr, const char *path) {
    if (path == nullptr || path[0] == '\0')
        return nullptr;

    // a path can either start from the root (absolute) or from the curent dir (relative)
    struct inode *curr_inode = (path[0] == '/') ? inode_get(ROOT_INODE) : inode_get(cwd_inode_nr);
    if (curr_inode == nullptr)
        return nullptr;

    const char *temp_path = path;
    while (*temp_path) {
        while (*temp_path == '/')
            ++temp_path;
        if (*temp_path == '\0')
            break;

        // extract current directory/file name
        const char *name_end = temp_path;
        while (*name_end && *name_end != '/')
            ++name_end;

        size_t len = name_end - temp_path;
        if (len >= NAME_MAX_SIZE) {
            if (curr_inode != nullptr)
                inode_return(curr_inode->inode_nr);
            return nullptr;
        }

        char name[NAME_MAX_SIZE];
        memcpy(name, temp_path, len);
        name[len] = '\0';

        if (curr_inode != nullptr && curr_inode->data->type != DIR) {
            // user tried to descend into a file
            inode_return(curr_inode->inode_nr);
            return nullptr;
        }

        struct inode *prev_inode = curr_inode;
        curr_inode = fetch_inode_in_dir(curr_inode, name);
        if (prev_inode != nullptr)
            inode_return(prev_inode->inode_nr);

        if (curr_inode == nullptr)
            // couldn't find entry in dir
            return nullptr;

        temp_path = name_end;
    }
    return curr_inode;
}

struct inode *FileSystem::resolve_parent_dir(uint32_t cwd_inode_nr, const char *path, char out_name[NAME_MAX_SIZE]) {
    if (path == nullptr || path[0] == '\0')
        return nullptr;

    uint64_t len = strlen(path);
    if (len >= MAX_PATH_SIZE)
        return nullptr;

    // remove any trailin slashes
    while (len > 1 && path[len - 1] == '/')
        len--;

    if (len == 0 || len >= MAX_PATH_SIZE)
        return nullptr;

    // find the last /
    uint64_t last_slash = UINT64_MAX;
    for (uint64_t i = 0; i < len; i++)
        if (path[i] == '/')
            last_slash = i;

    if (last_slash == UINT64_MAX && len < NAME_MAX_SIZE) {
        // no / means file is going to be added to cwd (open "file.txt")
        memcpy(out_name, path, len);
        out_name[len] = '\0';
        return inode_get(cwd_inode_nr);
    }

    uint64_t name_len = len - (last_slash + 1);
    if (name_len == 0 || name_len >= NAME_MAX_SIZE)
        // nothing was after the last slash
        return nullptr;

    memcpy(out_name, path + last_slash + 1, name_len);
    out_name[name_len] = '\0';

    char dir_path[MAX_PATH_SIZE];
    if (last_slash == 0) {
        dir_path[0] = '/';
        dir_path[1] = '\0';
    } else {
        memcpy(dir_path, path, last_slash);
        dir_path[last_slash] = '\0';
    }

    return path_resolver(cwd_inode_nr, dir_path);
}

uint64_t FileSystem::file_write(struct inode *inode, void *buf, uint64_t size, uint64_t offset) {
    uint32_t current_block_nr = offset / superblock->block_size;
    uint32_t remaining_offset = offset % superblock->block_size;

    char *temp_buf = reinterpret_cast<char *>(buf);
    uint64_t written_bytes = 0;
    while (size) {
        uint64_t absolute_block_nr = bmap(inode, current_block_nr, true);
        if (absolute_block_nr == UINT32_MAX) {
            // disk has run out of space
            break;
        }

        uint64_t write_size = (size < superblock->block_size - remaining_offset) ? size : (superblock->block_size - remaining_offset);

        bool ret = drive->disk_write(absolute_block_nr * superblock->block_size + remaining_offset,
            write_size, temp_buf + written_bytes);
        if (!ret)
            break;

        written_bytes += write_size;
        size          -= write_size;
        remaining_offset = 0;
        current_block_nr++;
    }

    if (written_bytes > 0 && offset + written_bytes > inode->data->size) {
        inode->data->size = offset + written_bytes;
        write_disk_inode_to_disk(inode);
    }

    return written_bytes;
}

uint64_t FileSystem::file_read(struct inode *inode, void *buf, uint64_t size, uint64_t offset) {
    uint32_t current_block_nr = offset / superblock->block_size;
    uint32_t remaining_offset = offset % superblock->block_size;

    char *temp_buf = reinterpret_cast<char *>(buf);
    uint64_t read_bytes = 0;
    while (size) {
        if (offset + read_bytes >= inode->data->size)
            // eof
            break;

        uint64_t absolute_block_nr = bmap(inode, current_block_nr, false);
        if (absolute_block_nr == UINT32_MAX) {
            // eof reached
            break;
        }

        if (size > (inode->data->size - offset - read_bytes))
            size = inode->data->size - offset - read_bytes;

        uint64_t read_size = (size < superblock->block_size - remaining_offset) ? size : (superblock->block_size - remaining_offset);

        bool ret = drive->disk_read(absolute_block_nr * superblock->block_size + remaining_offset,
            read_size, temp_buf + read_bytes);
        if (!ret)
            break;

        read_bytes += read_size;
        size       -= read_size;
        remaining_offset = 0;
        current_block_nr++;
    }

    return read_bytes;
}

void FileSystem::resize_file(struct inode *inode, uint64_t size) {
    if (inode->data->size > size || (inode->data->blocks * superblock->block_size > size))
        inode->data->size = size;
    else {
        // find how many blocks would be needed for the new size
        uint32_t block_nr = size / superblock->block_size;
        if (size % superblock->block_size != 0)
            block_nr++;
        
        if (block_nr == 0)
            block_nr++;

        // request them
        block_nr = bmap(inode, block_nr - 1, true);
        if (block_nr == UINT32_MAX)
            return;

        inode->data->size = size;
    }

    write_disk_inode_to_disk(inode);
}

struct entry *FileSystem::readdir(struct inode *dir, uint32_t *count) {
    *count = 0;
    if (dir == nullptr || dir->data->type != DIR)
        return nullptr;

    const uint32_t entries_per_block = (superblock->block_size - sizeof(uint16_t)) / sizeof(struct entry);

    uint64_t total = dir->data->size / sizeof(struct entry);
    if (total == 0)
        return nullptr;

    struct entry *entries = reinterpret_cast<struct entry *>(calloc(total, sizeof(struct entry)));
    if (entries == nullptr)
        return nullptr;

    // walk the blocks and copy them into the array
    uint64_t filled = 0;
    for (uint32_t i = 0; i < dir->data->blocks; i++) {
        uint32_t block_nr = bmap(dir, i, false);
        if (block_nr == UINT32_MAX) {
            free(entries);
            return nullptr;
        }

        uint64_t block_offset = static_cast<uint64_t>(block_nr) * superblock->block_size;

        uint16_t entry_nr;
        if (!drive->disk_read(block_offset, sizeof(entry_nr), &entry_nr)) {
            free(entries);
            return nullptr;
        }

        if (entry_nr == 0)
            continue;

        // the dir changed between the 2 passes
        if (filled + entry_nr > total) {
            free(entries);
            return nullptr;
        }

        if (!drive->disk_read(block_offset + sizeof(uint16_t), entry_nr * sizeof(struct entry),
                entries + filled)) {
            free(entries);
            return nullptr;
        }

        filled += entry_nr;
    }

    *count = filled;
    return entries;
}

bool FileSystem::move(uint32_t src_dir_nr, char src_name[NAME_MAX_SIZE],
    uint32_t dst_dir_nr, char dst_name[NAME_MAX_SIZE]) {
    struct inode *src_dir = nullptr;
    struct inode *dst_dir = nullptr;
    struct inode *target  = nullptr;
    bool ret = false;

    if (strlen(dst_name) >= NAME_MAX_SIZE ||
        strncmp(src_name, ".\0", 2) == 0 || strncmp(src_name, "..\0", 3) == 0)
        return false;

    src_dir = inode_get(src_dir_nr);
    dst_dir = inode_get(dst_dir_nr);
    if (src_dir == nullptr || dst_dir == nullptr)
        goto end;

    if (src_dir->data->type != DIR || dst_dir->data->type != DIR)
        goto end;

    target = fetch_inode_in_dir(src_dir, src_name);
    if (target == nullptr)
        goto end;

    // no work to be done
    if (src_dir == dst_dir && strncmp(src_name, dst_name, NAME_MAX_SIZE) == 0) {
        ret = true;
        goto end;
    }

    // destination name is already taken
    if (find_inode_nr(dst_dir, dst_name) != UINT32_MAX)
        goto end;

    if (target->data->type == DIR && src_dir != dst_dir) {
        // a dir can't be moved into itself or into one of its own subdirs
        uint32_t curr_nr = dst_dir_nr;
        while (curr_nr != ROOT_INODE) {
            if (curr_nr == target->inode_nr)
                goto end;

            struct inode *curr = inode_get(curr_nr);
            if (curr == nullptr)
                goto end;

            uint32_t parent_nr = find_inode_nr(curr, "..\0");
            inode_return(curr_nr);
            if (parent_nr == UINT32_MAX || parent_nr == curr_nr)
                break;

            curr_nr = parent_nr;
        }
    }

    // add the new entry first so hard_links never reaches 0
    if (!(dir_add_and_link_entry(dst_dir, target, dst_name)))
        // disk is full. move failed
        goto end;

    dir_remove_and_unlink_entry(src_dir, src_name);

    if (target->data->type == DIR && src_dir != dst_dir) {
        if (!(dir_add_and_link_entry(target, dst_dir, "..\0"))) {
            // disk is full. revert previous move
            dir_add_and_link_entry(src_dir, target, src_name);
            dir_remove_and_unlink_entry(dst_dir, dst_name);
            goto end;
        }

        dir_remove_and_unlink_entry(target, "..\0");
    }

    ret = true;

end:
    if (target != nullptr)
        inode_return(target->inode_nr);
    if (dst_dir != nullptr)
        inode_return(dst_dir_nr);
    if (src_dir != nullptr)
        inode_return(src_dir_nr);
    return ret;
}

bool FileSystem::move_path(uint32_t old_inode_nr, const char *old_path,
    uint32_t new_inode_nr, const char *new_path) {
    struct inode *src_dir = nullptr;
    struct inode *dst_dir = nullptr;
    char src_name[NAME_MAX_SIZE];
    char dst_name[NAME_MAX_SIZE];
    bool ret = false;

    src_dir = resolve_parent_dir(old_inode_nr, old_path, src_name);
    if (src_dir == nullptr)
        return false;

    dst_dir = resolve_parent_dir(new_inode_nr, new_path, dst_name);
    if (dst_dir == nullptr){
        inode_return(src_dir->inode_nr);
        return false;
    }

    ret = move(src_dir->inode_nr, src_name, dst_dir->inode_nr, dst_name);

    inode_return(dst_dir->inode_nr);
    inode_return(src_dir->inode_nr);
    return ret;
}

bool FileSystem::link(uint32_t dst_dir_nr, uint32_t target_nr, char dst_name[NAME_MAX_SIZE]) {
    struct inode *target  = nullptr;
    struct inode *dst_dir = nullptr;
    bool ret = false;

    size_t name_len = strlen(dst_name);
    if (name_len == 0 || name_len >= NAME_MAX_SIZE)
        return false;

    target  = inode_get(target_nr);
    dst_dir = inode_get(dst_dir_nr);
    if (target == nullptr || dst_dir == nullptr)
        goto end;

    if (dst_dir->data->type != DIR)
        goto end;

    // hard links to directories would allow loops in the tree
    if (target->data->type == DIR)
        goto end;

    // an inode with no links is on its way out (only in-memory refs keep it alive).
    // linking it back would resurrect something that unlink already gave up on
    if (target->data->hard_links == 0)
        goto end;

    // destination name is already taken ("." and ".." are caught here too)
    if (find_inode_nr(dst_dir, dst_name) != UINT32_MAX)
        goto end;

    // does hard_links++ and writes both disk inodes
    ret = dir_add_and_link_entry(dst_dir, target, dst_name);

end:
    if (dst_dir != nullptr)
        inode_return(dst_dir_nr);
    if (target != nullptr)
        inode_return(target_nr);
    return ret;
}

bool FileSystem::link_path(uint32_t old_inode_nr, const char *old_path,
    uint32_t new_inode_nr, const char *new_path) {
    struct inode *target  = nullptr;
    struct inode *dst_dir = nullptr;
    char name[NAME_MAX_SIZE];
    bool ret = false;

    target = path_resolver(old_inode_nr, old_path);
    if (target == nullptr)
        return false;

    dst_dir = resolve_parent_dir(new_inode_nr, new_path, name);
    if (dst_dir == nullptr){
        inode_return(target->inode_nr);
        return false;
    }

    ret = link(dst_dir->inode_nr, target->inode_nr, name);

    inode_return(dst_dir->inode_nr);
    inode_return(target->inode_nr);
    return ret;
}

bool FileSystem::mkdir_path(uint32_t base_inode_nr, const char *path) {
    char name[NAME_MAX_SIZE];

    struct inode *parent_dir = resolve_parent_dir(base_inode_nr, path, name);
    if (parent_dir == nullptr)
        return false;

    struct inode *dir = inode_alloc(DIR, DEFAULT_PERM, name, parent_dir);
    inode_return(parent_dir->inode_nr);

    if (dir == nullptr)
        return false;

    // no need to keep a reference to it
    inode_return(dir->inode_nr);
    return true;
}

bool FileSystem::unlink_path(uint32_t base_inode_nr, const char *path, bool remove_dir) {
    struct inode *parent_dir = nullptr;
    struct inode *child = nullptr;
    char name[NAME_MAX_SIZE];
    bool ret = false;

    parent_dir = resolve_parent_dir(base_inode_nr, path, name);
    if (parent_dir == nullptr)
        return false;

    if (strncmp(name, ".", NAME_MAX_SIZE) == 0 || strncmp(name, "..", NAME_MAX_SIZE) == 0)
        goto end;

    child = fetch_inode_in_dir(parent_dir, name);
    if (child == nullptr)
        goto end;

    if (child->data->type == DIR) {
        if (!remove_dir)
            goto end;

        // delete_file refuses the root and non-empty directories
        if (!delete_file(parent_dir, child))
            goto end;
    } else {
        if (remove_dir)
            goto end;

        // remove by name, not delete_file: delete_file finds the entry by inode nr,
        // which could pick a different name if the file is hard linked twice in this dir
        dir_remove_and_unlink_entry(parent_dir, name);
    }

    ret = true;

end:
    if (child != nullptr)
        inode_return(child->inode_nr);
    inode_return(parent_dir->inode_nr);
    return ret;
}

struct file *FileSystem::open_file(int flags, uint32_t cwd_inode_nr, const char *path) {
    int access = flags & 3;
    if (access == 3)
        return nullptr;

    struct inode *inode = path_resolver(cwd_inode_nr, path);
    if (inode == nullptr) {
        if ((flags & O_CREAT) == 0)
            return nullptr;

        char name_buffer[NAME_MAX_SIZE];
        struct inode *parent_dir = resolve_parent_dir(cwd_inode_nr, path, name_buffer);
        if (parent_dir == nullptr)
            return nullptr;

        inode = inode_alloc(FILE, DEFAULT_PERM, name_buffer, parent_dir);
        inode_return(parent_dir->inode_nr);

        if (inode == nullptr)
            return nullptr;

        if (inode->data->type != FILE) {
            inode_return(inode->inode_nr);
            return nullptr;
        }
    }

    bool writable = (access == O_WRONLY || access == O_RDWR);
    // dirs can be opened only for read
    if (inode->data->type == DIR && (writable || (flags & O_TRUNC))) {
        inode_return(inode->inode_nr);
        return nullptr;
    }

    struct file *new_file = reinterpret_cast<struct file *>(calloc(1, sizeof(struct file)));
    if (new_file == nullptr) {
        inode_return(inode->inode_nr);
        return nullptr;
    }

    new_file->ip = inode;
    new_file->lock.locked = false;
    new_file->readable = (access == O_RDONLY || access == O_RDWR);
    new_file->writable = writable;
    new_file->off = 0;
    new_file->ref = 1;

    if (writable && (flags & O_TRUNC) && inode->data->size != 0)
        resize_file(inode, 0);

    return new_file;
}
