// SPDX-License-Identifier: MIT
#include <kernel/console.hpp>
#include <kernel/scheduler/scheduler.hpp>
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

static void tty_putchar(char c) {
    kernel::console::putc(c);
}

void feed_input(char c) noexcept {
    // Echo
    if (g_tty.term.c_lflag & ECHO) {
        tty_putchar(c);
    }

    // Store in buffer
    size_t next_head = (g_tty.rx_head + 1) % g_tty.BUF_SIZE;
    if (next_head != g_tty.rx_tail) {
        g_tty.rx_buf[g_tty.rx_head] = c;
        g_tty.rx_head = next_head;

        if (c == 0x0A || !(g_tty.term.c_lflag & ICANON)) {
            g_tty.lines_available++;
        }
    }

    // Wake up reader if any
    if (g_tty.waiting_thread && g_tty.lines_available > 0) {
        scheduler::scheduler::unblock(g_tty.waiting_thread);
        g_tty.waiting_thread = nullptr;
    }
}

static size_t tty_read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    (void)node;
    (void)offset;
    char* buf = static_cast<char*>(buffer);
    size_t read = 0;

    // Block if nothing available
    while (g_tty.lines_available == 0) {
        g_tty.waiting_thread = scheduler::scheduler::current_thread();
        scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
    }

    while (read < size && g_tty.rx_tail != g_tty.rx_head) {
        char c = g_tty.rx_buf[g_tty.rx_tail];
        g_tty.rx_tail = (g_tty.rx_tail + 1) % g_tty.BUF_SIZE;
        buf[read++] = c;

        if ((g_tty.term.c_lflag & ICANON) && c == 0x0A) {
            g_tty.lines_available--;
            break;
        }
    }

    if (!(g_tty.term.c_lflag & ICANON) && read > 0) {
        g_tty.lines_available--;
    }

    return read;
}

static size_t tty_write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    (void)node;
    (void)offset;
    const char* buf = static_cast<const char*>(buffer);
    for (size_t i = 0; i < size; i++) {
        tty_putchar(buf[i]);
    }
    return size;
}

static int tty_ioctl(vfs_node* node, unsigned long request, void* argp) noexcept {
    (void)node;
    if (request == TCGETS) {
        termios* t = static_cast<termios*>(argp);
        *t = g_tty.term;
        return 0;
    } else if (request == TCSETS) {
        termios* t = static_cast<termios*>(argp);
        g_tty.term = *t;
        return 0;
    } else if (request == TIOCSTI) {
        char* c = static_cast<char*>(argp);
        feed_input(*c);
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

vfs_node* create() noexcept {
    g_tty.rx_head = 0;
    g_tty.rx_tail = 0;
    g_tty.lines_available = 0;
    g_tty.waiting_thread = nullptr;

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
    return node;
}

} // namespace kernel::vfs::tty
