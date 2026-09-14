// SPDX-License-Identifier: MIT
#include <kernel/console.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/tty.hpp>

// POSIX ioctl numbers
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCSTI 0x5412
#define TIOCGWINSZ 0x5413

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag; /* input modes */
    tcflag_t c_oflag; /* output modes */
    tcflag_t c_cflag; /* control modes */
    tcflag_t c_lflag; /* local modes */
    cc_t c_line;      /* line discipline */
    cc_t c_cc[NCCS];  /* control characters */
    speed_t c_ispeed; /* input speed */
    speed_t c_ospeed; /* output speed */
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* c_lflag bits */
#define ISIG 0000001
#define ICANON 0000002
#define ECHO 0000010
#define ECHOE 0000020
#define ECHOK 0000040
#define ECHONL 0000100

namespace kernel::vfs::tty {

struct tty_state {
    termios term;

    // Circular buffer for input
    static constexpr size_t BUF_SIZE = 1024;
    char rx_buf[BUF_SIZE];
    size_t rx_head;
    size_t rx_tail;
    size_t lines_available;

    // Wait queue for readers
    scheduler::thread* waiting_thread;
};

static tty_state g_tty;
static kernel::irq_spinlock g_tty_lock;
static bool g_ready = false;

static void tty_putchar(char c) {
    kernel::console::putc(c);
}

void feed_input(char c) noexcept {
    kernel::irq_lock_guard guard(g_tty_lock);
    if (!g_ready) return;
    const bool canonical = g_tty.term.c_lflag & ICANON;
    const bool echo = g_tty.term.c_lflag & ECHO;
    if (c == '\r' && (g_tty.term.c_iflag & 0400)) c = '\n'; // ICRNL
    if (canonical && (c == 127 || c == '\b' || c == 21)) {
        // Erase only the current incomplete line, never a queued complete one.
        while (g_tty.rx_head != g_tty.rx_tail) {
            size_t previous = (g_tty.rx_head + g_tty.BUF_SIZE - 1) % g_tty.BUF_SIZE;
            if (g_tty.rx_buf[previous] == '\n' || g_tty.rx_buf[previous] == 4) break;
            g_tty.rx_head = previous;
            if (echo && (g_tty.term.c_lflag & ECHOE)) {
                tty_putchar('\b');
                tty_putchar(' ');
                tty_putchar('\b');
            }
            if (c != 21) break;
        }
        return;
    }
    size_t next_head = (g_tty.rx_head + 1) % g_tty.BUF_SIZE;
    // Reserve space for the delimiter so a full canonical line can be read.
    size_t after_next = (next_head + 1) % g_tty.BUF_SIZE;
    if (next_head == g_tty.rx_tail || (canonical && c != '\n' && c != 4 && after_next == g_tty.rx_tail)) return;
    g_tty.rx_buf[g_tty.rx_head] = c;
    g_tty.rx_head = next_head;
    if (canonical && (c == '\n' || c == 4)) ++g_tty.lines_available;
    if (echo && !(canonical && c == 4)) tty_putchar(c);
    if (g_tty.waiting_thread && (!canonical || g_tty.lines_available)) {
        auto* reader = g_tty.waiting_thread;
        g_tty.waiting_thread = nullptr;
        if (reader->state == scheduler::thread_state::BLOCKED) scheduler::scheduler::unblock(reader);
    }
}

void detach_reader(scheduler::thread* t) noexcept {
    kernel::irq_lock_guard guard(g_tty_lock);
    if (g_tty.waiting_thread == t) g_tty.waiting_thread = nullptr;
}

static size_t buf_count() noexcept {
    return (g_tty.rx_head - g_tty.rx_tail + g_tty.BUF_SIZE) % g_tty.BUF_SIZE;
}

static size_t tty_read(vfs_node*, size_t, size_t size, void* buffer) noexcept {
    if (!size) return 0;
    auto* out = static_cast<char*>(buffer);
    while (true) {
        uintptr_t flags = kernel::irq_save();
        uintptr_t locked = g_tty_lock.lock();
        const bool canonical = g_tty.term.c_lflag & ICANON;
        size_t need = g_tty.term.c_cc[6]; // VMIN
        if (need > size) need = size;
        bool ready = canonical ? g_tty.lines_available != 0 : buf_count() >= need;
        if (ready) {
            size_t count = 0;
            while (count < size && g_tty.rx_tail != g_tty.rx_head) {
                char c = g_tty.rx_buf[g_tty.rx_tail];
                g_tty.rx_tail = (g_tty.rx_tail + 1) % g_tty.BUF_SIZE;
                if (canonical && c == 4) {
                    --g_tty.lines_available;
                    break;
                }
                out[count++] = c;
                if (canonical && c == '\n') {
                    --g_tty.lines_available;
                    break;
                }
            }
            g_tty_lock.unlock(locked);
            kernel::irq_restore(flags);
            return count;
        }
        auto* reader = scheduler::scheduler::current_thread();
        g_tty.waiting_thread = reader;
        reader->state = scheduler::thread_state::BLOCKED;
        g_tty_lock.unlock(locked);
        // Enrollment and BLOCKED publication precede dropping the lock.
        scheduler::scheduler::schedule();
        kernel::irq_restore(flags);
    }
}

static size_t tty_write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    (void)node;
    (void)offset;
    const char* buf = static_cast<const char*>(buffer);
    uintptr_t flags = kernel::console::lock_output();
    for (size_t i = 0; i < size; i++)
        kernel::console::putc_unlocked(buf[i]);
    kernel::console::unlock_output(flags);
    return size;
}

static int tty_ioctl(vfs_node* node, unsigned long request, void* argp) noexcept {
    (void)node;
    if (request == TIOCSTI) {
        feed_input(*static_cast<char*>(argp));
        return 0;
    }
    kernel::irq_lock_guard guard(g_tty_lock);
    if (request == TCGETS) {
        termios* t = static_cast<termios*>(argp);
        *t = g_tty.term;
        return 0;
    } else if (request == TCSETS) {
        termios* t = static_cast<termios*>(argp);
        g_tty.term = *t;
        return 0;
    } else if (request == TIOCGWINSZ) {
        winsize* ws = static_cast<winsize*>(argp);
        kernel::console::display_info info = kernel::console::get_display_info();
        ws->ws_xpixel = static_cast<unsigned short>(info.width_pixels);
        ws->ws_ypixel = static_cast<unsigned short>(info.height_pixels);
        ws->ws_col = static_cast<unsigned short>(info.columns);
        ws->ws_row = static_cast<unsigned short>(info.rows);
        return 0;
    }
    return -1;
}

static int tty_poll_ready(vfs_node*) noexcept {
    kernel::irq_lock_guard guard(g_tty_lock);
    int events = 4; // POLLOUT — always writable
    // Check if there's data in the input buffer
    if (g_tty.rx_head != g_tty.rx_tail) {
        if (g_tty.term.c_lflag & ICANON) {
            if (g_tty.lines_available > 0) events |= 1; // POLLIN
        } else {
            events |= 1; // POLLIN — any data in raw mode
        }
    }
    return events;
}

vfs_node* create() noexcept {
    g_tty.rx_head = 0;
    g_tty.rx_tail = 0;
    g_tty.lines_available = 0;
    g_tty.waiting_thread = nullptr;

    g_tty.term.c_iflag = 0400; // ICRNL
    g_tty.term.c_cc[6] = 1;    // VMIN
    g_ready = true;

    // Default to canonical mode with echo
    g_tty.term.c_lflag = ICANON | ECHO | ECHOE | ECHOK | ECHONL;
    g_tty.term.c_oflag = ECHONL;

    vfs_node* node = new vfs_node();
    node->name = "tty";
    node->name_hash = vfs_node::hash_name("tty");
    node->type = file_type::CHAR_DEVICE;
    node->ops = new vfs_ops();
    node->ops->read = tty_read;
    node->ops->write = tty_write;
    node->ops->ioctl = tty_ioctl;
    node->ops->open = nullptr;
    node->ops->close = nullptr;
    node->ops->readdir = nullptr;
    node->ops->finddir = nullptr;
    node->ops->mmap = nullptr;
    node->ops->poll = tty_poll_ready;
    return node;
}

} // namespace kernel::vfs::tty
