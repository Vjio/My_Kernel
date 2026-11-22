#include "syscall.hpp"
#include "../memory/vmm.hpp"
#include "../memory/pmm.hpp"
#include "scheduling/process.hpp"
#include "scheduling/scheduler.hpp"
#include "stdio.hpp"
#include "../file_system/file.hpp"
#include "../file_system/vsfs_layout.hpp"
#include "../elf/elf_loader.hpp"
#include "../keyboard/tty.hpp"

#define AT_REMOVEDIR    0x200
#define AT_FDCWD        (-100)

extern struct tty *g_console_tty;

// copies a NUL terminated string from userspace into dst
// returns the length of the string plus the NULL character
// returns 0 if the pointer is bad or the string doesn't fit in dst_size
static size_t copy_string_from_user(char *dst, const char *user_src, size_t dst_size) {
    if (user_src == nullptr)
        return 0;

    for (size_t i = 0; i < dst_size; i++) {
        if (!VMM::validate_userland_memory(const_cast<char *>(user_src + i), 1, false))
            return 0;

        dst[i] = user_src[i];
        if (dst[i] == '\0')
            return i+1;
    }

    return 0;
}

// copies a NULL terminated argv array from userspace into args
// returns false if a pointer is bad, there are more than MAX_ARGC entries or
// the strings don't fit in MAX_ARG_BYTES
static bool copy_argv_from_user(struct spawn_args *args, char *user_argv[]) {
    args->argc = 0;
    args->strings_size = 0;

    // a NULL argv is the same as an empty one
    if (user_argv == nullptr)
        return true;

    for (;;) {
        // the array itself lives in userspace too
        if (!VMM::validate_userland_memory(&user_argv[args->argc], sizeof(char *), false))
            return false;

        const char *user_str = user_argv[args->argc];
        if (user_str == nullptr)
            return true;

        if (args->argc == MAX_ARGC)
            return false;

        size_t copied = copy_string_from_user(args->strings + args->strings_size, user_str,
                                              MAX_ARG_BYTES - args->strings_size);
        if (copied == 0)
            return false;

        args->strings_size += copied;
        args->argc++;
    }
}

// reads a whole file into buf. buf must hold file->data->size bytes
static bool read_whole_file(FileSystem *fs, struct inode *file, uint8_t *buf) {
    uint64_t size = file->data->size;
    uint64_t done = 0;

    // file_read can return less than asked for, so keep going until everything gets read
    while (done < size) {
        uint64_t ret = fs->file_read(file, buf + done, size - done, done);
        if (ret == 0)
            return false;
        done += ret;
    }

    return true;
}

uint64_t clone(char *name, void(*function)(void*), void *arg) {
    // check if user given arguments are valid
    if (!VMM::validate_userland_memory(name, MAX_NAME_LEN, false))
        return static_cast<uint64_t>(-1);

    if (!VMM::validate_userland_memory(reinterpret_cast<void *>(function), 1, false))
        return static_cast<uint64_t>(-1);

    struct thread *new_thread =  add_user_thread(name, reinterpret_cast<uint64_t>(function), arg);

    // 0 returns as failure return value becuase that is the kernel's initial thread id
    return new_thread ? new_thread->tid : 0;
}

long spawn(char *path, char *argv[]) {
    struct spawn_args *args;
    struct inode *target = nullptr;
    struct elf_source elf_src;
    struct process *new_proc;
    long ret = -1;
    char path_buffer[MAX_PATH_SIZE];
    char name[MAX_NAME_LEN] = {};
    const char *base;
    uint8_t *buf = nullptr;

    struct process *cur_proc = Scheduler::get_current_scheduler()->get_running_thread()->parent;
    FileSystem *fs = cur_proc->fs;

    if (copy_string_from_user(path_buffer, path, MAX_PATH_SIZE) == 0)
        return -1;

    // spawn_args is MAX_ARG_BYTES big. keep it off the syscall's kernel stack
    args = reinterpret_cast<struct spawn_args *>(calloc(1, sizeof(struct spawn_args)));
    if (args == nullptr)
        return -1;

    if (!copy_argv_from_user(args, argv))
        goto end;

    target = fs->path_resolver(cur_proc->cwd->inode_nr, path_buffer);
    if (target == nullptr)
        goto end;
    if (target->data->type != FILE || target->data->size == 0 ||
        target->data->size > MAX_ELF_SIZE)
        goto end;

    // name the program after the file. name is zeroed, so it always ends in a NUL
    base = path_buffer;
    for (const char *c = path_buffer; *c; c++)
        if (*c == '/')
            base = c + 1;
    for (size_t i = 0; i < MAX_NAME_LEN - 1 && base[i]; i++)
        name[i] = base[i];

    buf = reinterpret_cast<uint8_t *>(calloc(target->data->size, 1));
    if (buf == nullptr)
        goto end;

    if (!read_whole_file(fs, target, buf))
        goto end;

    elf_src.ctx = buf;
    elf_src.size = target->data->size;

    new_proc = create_user_process(name, &elf_src, args);
    if (new_proc != nullptr)
        ret = new_proc->pid;

end:
    free(buf);
    free(args);
    if (target)
        fs->inode_return(target->inode_nr);
    return ret;
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

long write(int fd, char *buf, size_t count) {
    if (!VMM::validate_userland_memory(buf, count, false))
        return -1;

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
        return -1;

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

long read(int fd, char *buf, size_t count) {
    if (!VMM::validate_userland_memory(buf, count, true))
        return -1;

    if (fd < 0 || fd >= MAX_FILE_FD)
        return -1;

    // these stds are not yet implemented so there's nothing to read
    if (fd == STDOUT || fd == STDERR) {
        return count;
    }

    if (fd == STDIN) {
        return tty_read(g_console_tty, buf, count);
    }

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;
    struct file *file = running_process->fds[fd];

    if (!file || file->readable == false)
        return -1;

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

    if (!VMM::map_pages(proc->phys_page_table, proc->heap_end, aligned_len / FRAME_SIZE, flags)) {
        // TODO: update this once running out of memory is handled gracefully
        release(&proc->lock);
        return { nullptr, 0 };
    }

    proc->heap_end = new_heap_end;
    release(&proc->lock);

    return { reinterpret_cast<void *>(old_heap_end), aligned_len };
}

int open(char *path, int flags) {
    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;

    struct file *new_file = fs->open_file(flags, running_process->cwd->inode_nr, path);
    if (new_file == nullptr)
        return -1;

    int fd = find_first_free_fd(running_process);
    if (fd == -1) {
        // TODO: allow increasing the fd table in size if a bigger table is ever needed
        printf("you ran out of fds!\n");
        fs->inode_return(new_file->ip->inode_nr);
        free(new_file);
        return -1;
    }

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
    if (copy_string_from_user(kernel_old_path, old_path, sizeof(kernel_old_path)) == 0 ||
        copy_string_from_user(kernel_new_path, new_path, sizeof(kernel_new_path)) == 0)
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
    if (!copy_string_from_user(kernel_old_path, old_path, sizeof(kernel_old_path)) == 0 ||
        !copy_string_from_user(kernel_new_path, new_path, sizeof(kernel_new_path)) == 0)
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
    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) == 0)
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
    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) == 0)
        return -1;

    uint32_t base = at_base_inode(running_process, dir_fd);
    if (base == UINT32_MAX)
        return -1;

    return fs->unlink_path(base, kernel_path, (flags & AT_REMOVEDIR) != 0) ? 0 : -1;
}

int getdents(int fd, struct dirent *buf, size_t max_entries) {
    if (fd < 0 || fd >= MAX_FILE_FD || max_entries == 0)
        return -1;

    if (max_entries > GETDENTS_MAX_ENTRIES)
        max_entries = GETDENTS_MAX_ENTRIES;

    if (!VMM::validate_userland_memory(buf, max_entries * sizeof(struct dirent), true))
        return -1;

    struct thread *running_thread = Scheduler::get_current_scheduler()->get_running_thread();
    struct process *running_process = running_thread->parent;
    FileSystem *fs = running_process->fs;
    struct file *file = running_process->fds[fd];

    if (!file || file->ip->data->type != DIR)
        return -1;

    // only hold the lock while making a copy of the cursor
    acquire(&file->lock);
    uint64_t off = file->off;
    release(&file->lock);

    if (off % sizeof(struct entry) != 0)
        return -1;

    uint32_t entry_nr = 0;
    struct entry *entries = fs->readdir(file->ip, &entry_nr);
    if (entries == nullptr)
        return -1;

    uint64_t start = off / sizeof(struct entry);
    if (start >= entry_nr) {
        free(entries);
        return 0;
    }

    uint64_t n = entry_nr - start;
    if (n > max_entries)
        n = max_entries;

    for (uint64_t i = 0; i < n; i++) {
        const struct entry *src = &entries[start + i];

        buf[i].inode_nr = src->inode_nr;

        size_t j = 0;
        for (; j < DIRENT_NAME_MAX - 1 && src->name[j] != '\0'; j++)
            buf[i].name[j] = src->name[j];
        buf[i].name[j] = '\0';
    }

    free(entries);

    acquire(&file->lock);
    file->off = off + n * sizeof(struct entry);
    release(&file->lock);

    return static_cast<int>(n);
}
