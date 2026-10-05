#include "syscall.hpp"
#include "../memory/vmm.hpp"
#include "../memory/pmm.hpp"
#include "scheduling/process.hpp"
#include "scheduling/scheduler.hpp"
#include "stdio.hpp"
#include "../file_system/file.hpp"
#include "../file_system/vsfs_layout.hpp"

#define AT_REMOVEDIR    0x200
#define AT_FDCWD        (-100)

uint64_t clone(bool new_process_flag, char *name, uint64_t entry_point, void *arg) {
    // check if user given arguments are valid
    if (!VMM::validate_userland_memory(name, MAX_NAME_LEN, false))
        return static_cast<uint64_t>(-1);

    if (!VMM::validate_userland_memory(reinterpret_cast<void *>(entry_point), 1, false))
        return static_cast<uint64_t>(-1);

    Scheduler *scheduler = Scheduler::get_current_scheduler();
    struct thread *thread = scheduler->get_running_thread();
    if (new_process_flag)
        return create_user_process(name, entry_point, arg)->threads->tid;
    else
        return add_user_thread(thread->parent, name, entry_point, arg)->tid;
}

void exit() {
    struct thread *caller = Scheduler::get_current_scheduler()->get_running_thread();
    __asm__ volatile("sti");
    caller->thread_exit();
}

void sleep(uint64_t ticks) {
    struct thread *caller = Scheduler::get_current_scheduler()->get_running_thread();
    __asm__ volatile("sti");
    caller->thread_sleep(ticks);
}

uint64_t write(int fd, char *buf, size_t count) {
    if (!VMM::validate_userland_memory(buf, count, false))
        return static_cast<uint64_t>(-1);

    if (fd < 0 || fd >= MAX_FILE_FD)
        return -1;

    // stds are not yet implemented
    if (fd == STDOUT || fd == STDERR || fd == STDIN) {

        // two threads tring to write at the same time will currently lead to jumbled output
        // since i chose to reenable interrupts in this syscall (since the user can just
        // ask to print 1 billion characters and that would freeze the system if interrupts are still masked)
        // TODO: fix by adding a lock
        
        __asm__ volatile("sti");
            for (size_t i = 0; i < count; i++)
                putc(buf[i]);
        return count;
    }

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;
    struct file *file = running_process->fds[fd];

    if (!file || file->writable == false)
        return static_cast<uint64_t>(-1);

    uint64_t written = 0;
    acquire(&file->lock);
    while (written < count) {
        uint64_t ret = fs->file_write(file->ip, buf + written, count - written, file->off);
        if (ret == 0)
            break;

        written += ret;
        file->off += ret;
    }

    release(&file->lock);

    return written;
}

uint64_t read(int fd, char *buf, size_t count) {
    if (!VMM::validate_userland_memory(buf, count, false))
        return static_cast<uint64_t>(-1);

    if (fd < 0 || fd >= MAX_FILE_FD)
        return -1;

    // stds are not yet implemented so there's nothing to read
    if (fd == STDOUT || fd == STDERR || fd == STDIN) {

        // two threads tring to write at the same time will currently lead to jumbled output
        // since i chose to reenable interrupts in this syscall (since the user can just
        // ask to print 1 billion characters and that would freeze the system if interrupts are still masked)
        // TODO: fix by adding a lock
        
        return count;
    }

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;
    struct file *file = running_process->fds[fd];

    if (!file || file->readable == false)
        return static_cast<uint64_t>(-1);

    uint64_t read = 0;
    acquire(&file->lock);
    while (read < count) {
        uint64_t ret = fs->file_read(file->ip, buf + read, count - read, file->off);
        if (ret == 0)
            break;

        read += ret;
        file->off += ret;
    }

    release(&file->lock);

    return read;
}

struct brk_ret brk(size_t length) {
    if (length == 0)
        return {nullptr, 0};

    // round up to a whole number of pages
    uint64_t aligned_len = (length + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);

    thread *thread = Scheduler::get_current_scheduler()->get_running_thread();
    process *proc = thread->parent;

    acquire(&proc->lock);
    uint64_t old_heap_end = proc->heap_end;
    uint64_t new_heap_end = proc->heap_end + aligned_len;

    // brk should really only be called by a userland process
    // but just in case a mistake happen, i'll put this check here
    uint64_t flags = PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    if (thread->parent->is_kernel_process) {
        printf("don't use brk outside of userland! just call the VMM!\n");
        flags &= ~PTE_USER;
    }

    if (!VMM::map_pages(reinterpret_cast<uint64_t *>(proc->root_page_table), proc->heap_end, 
        aligned_len / FRAME_SIZE, flags)) {
        // TODO: update this once running out of memory is handled gracefully
        release(&proc->lock);
        return { nullptr, 0 };
    }

    proc->heap_end = new_heap_end;
    release(&proc->lock);

    return { reinterpret_cast<void *>(old_heap_end), aligned_len };
}

// copies a NUL terminated string from userspace into dst
// returns false if the pointer is bad or the string doesn't fit in dst_size
static bool copy_path_from_user(char *dst, const char *user_src, size_t dst_size) {
    if (user_src == nullptr)
        return false;

    for (size_t i = 0; i < dst_size; i++) {
        if (!VMM::validate_userland_memory(const_cast<char *>(user_src + i), 1, false))
            return false;

        dst[i] = user_src[i];
        if (dst[i] == '\0')
            return true;
    }

    return false;
}

int open(char *path, int flags) {
    int access = flags & 3;
    if (access == 3)
        return -1;
    bool writable = (access == O_WRONLY || access == O_RDWR);

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    char kernel_path[MAX_PATH_SIZE];
    if (!copy_path_from_user(kernel_path, path, sizeof(kernel_path)))
        return -1;

    struct inode *inode = fs->path_resolver(running_process->cwd->inode_nr, kernel_path);
    if (inode == nullptr) {
        if ((flags & O_CREAT) == 0)
            return -1;

        char name_buffer[NAME_MAX_SIZE];
        struct inode *parent_dir = fs->resolve_parent_dir(running_process->cwd->inode_nr,
            kernel_path, name_buffer);
        if (parent_dir == nullptr)
            return -1;

        inode = fs->inode_alloc(FILE, DEFAULT_PERM, name_buffer, parent_dir);
        fs->inode_return(parent_dir->inode_nr);

        if (inode == nullptr)
            return -1;

        if (inode->data->type != FILE) {
            fs->inode_return(inode->inode_nr);
            return -1;
        }
    }

    // dirs can be opened only for read
    if (inode->data->type == DIR && (writable || (flags & O_TRUNC))) {
        fs->inode_return(inode->inode_nr);
        return -1;
    }

    struct file *new_file = reinterpret_cast<struct file *>(calloc(1, sizeof(struct file)));
    if (new_file == nullptr) {
        fs->inode_return(inode->inode_nr);
        return -1;
    }

    new_file->ip = inode;
    new_file->lock.locked = false;
    new_file->readable = (access == O_RDONLY || access == O_RDWR);
    new_file->writable = writable;
    new_file->off = 0;
    new_file->ref = 1;

    int fd = find_first_free_fd(running_process);
    if (fd == -1) {
        // TODO: allow increasing the fd table in size if a bigger table is ever needed
        printf("you ran out of fds!\n");
        free(new_file);
        fs->inode_return(inode->inode_nr);
        return -1;
    }

    if (writable && (flags & O_TRUNC) && inode->data->size != 0)
        fs->resize_file(inode, 0);

    running_process->fds[fd] = new_file;
    return fd;
}

int close(int fd) {
    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    if (fd < 0 || fd >= MAX_FILE_FD)
        return -1;

    struct file *req_file = running_process->fds[fd];

    if (!req_file)
        return 0;

    acquire(&req_file->lock);
    req_file->ref--;
    bool last = req_file->ref == 0;
    release(&req_file->lock);
    
    if (req_file && last) {
        fs->inode_return(req_file->ip->inode_nr);
        free(req_file);
    }
    running_process->fds[fd] = nullptr;

    return 0;
}

int dup(int oldfd) {    
    if (oldfd < 0 || oldfd >= MAX_FILE_FD)
        return -1;

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;

    struct file *req_file = running_process->fds[oldfd];
    if (!req_file)
        return -1;

    int fd = find_first_free_fd(running_process);
    if (fd == -1) {
        // TODO: allow increasing the fd table in size if a bigger table is ever needed
        printf("you ran out of fds!\n");
        return fd;
    }
    
    acquire(&req_file->lock);
    req_file->ref++;
    release(&req_file->lock);
    running_process->fds[fd] = req_file;

    return fd;
}

int lseek(int fd, long offset, int whence) {
    if (fd < 0 || fd >= MAX_FILE_FD)
        return -1;

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    struct file *req_file = running_process->fds[fd];
    if (!req_file)
        return -1;

    int ret = -1;
    acquire(&req_file->lock);
    switch (whence) {
    case SEEK_SET:
        if (req_file->ip->data->size < offset || offset < 0)
            break;

        req_file->off = offset;
        ret = req_file->off;
        break;
    
    case SEEK_CUR: {
        if (req_file->ip->data->size < (req_file->off + offset) || offset < 0)
            break;

        req_file->off += offset;
        ret = req_file->off;
        break;
    }

    case SEEK_END: {
        if (offset > 0)
            break;

        if (offset * (-1) > req_file->ip->data->size)
            req_file->off = 0;
        else
            req_file->off = req_file->ip->data->size + offset;

        ret = req_file->off;
        break;
    }

    default:
        break;
    }

    release(&req_file->lock);
    return ret;
}

// returns the inode nr that relative paths should be resolved against, UINT32_MAX if the fd is bad
static uint32_t at_base_inode(struct process *proc, int dir_fd) {
    if (dir_fd == AT_FDCWD)
        return proc->cwd->inode_nr;
    if (dir_fd < 0 || dir_fd >= MAX_FILE_FD || proc->fds[dir_fd] == nullptr)
        return UINT32_MAX;
    return proc->fds[dir_fd]->ip->inode_nr;
}

int renameat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path) {
    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    char kernel_old_path[MAX_PATH_SIZE];
    char kernel_new_path[MAX_PATH_SIZE];
    if (!copy_path_from_user(kernel_old_path, old_path, sizeof(kernel_old_path)) ||
        !copy_path_from_user(kernel_new_path, new_path, sizeof(kernel_new_path)))
        return -1;

    uint32_t old_base = at_base_inode(running_process, old_dir_fd);
    uint32_t new_base = at_base_inode(running_process, new_dir_fd);
    if (old_base == UINT32_MAX || new_base == UINT32_MAX)
        return -1;

    return fs->move_path(old_base, kernel_old_path, new_base, kernel_new_path) ? 0 : -1;
}

int linkat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path, int flags) {
    if (flags != 0)
        return -1;

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    char kernel_old_path[MAX_PATH_SIZE];
    char kernel_new_path[MAX_PATH_SIZE];
    if (!copy_path_from_user(kernel_old_path, old_path, sizeof(kernel_old_path)) ||
        !copy_path_from_user(kernel_new_path, new_path, sizeof(kernel_new_path)))
        return -1;

    uint32_t old_base = at_base_inode(running_process, old_dir_fd);
    uint32_t new_base = at_base_inode(running_process, new_dir_fd);
    if (old_base == UINT32_MAX || new_base == UINT32_MAX)
        return -1;

    return fs->link_path(old_base, kernel_old_path, new_base, kernel_new_path) ? 0 : -1;
}

int mkdir(char *path) {
    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    char kernel_path[MAX_PATH_SIZE];
    if (!copy_path_from_user(kernel_path, path, sizeof(kernel_path)))
        return -1;

    return fs->mkdir_path(running_process->cwd->inode_nr, kernel_path) ? 0 : -1;
}

int unlinkat(int dir_fd, char *path, int flags) {
    if (flags & ~AT_REMOVEDIR)
        return -1;

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    char kernel_path[MAX_PATH_SIZE];
    if (!copy_path_from_user(kernel_path, path, sizeof(kernel_path)))
        return -1;

    uint32_t base = at_base_inode(running_process, dir_fd);
    if (base == UINT32_MAX)
        return -1;

    return fs->unlink_path(base, kernel_path, (flags & AT_REMOVEDIR) != 0) ? 0 : -1;
}
