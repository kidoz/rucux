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
    bool should_wake = false;
    if (g_tty.term.c_lflag & ICANON) {
        should_wake = (g_tty.lines_available > 0);
    } else {
        // Raw mode: wake on every character (VMIN=1 is the common case)
        should_wake = (g_tty.rx_head != g_tty.rx_tail);
    }

    if (g_tty.waiting_thread && should_wake) {
        scheduler::scheduler::unblock(g_tty.waiting_thread);
        g_tty.waiting_thread = nullptr;
    }
}

static size_t buf_count() noexcept {
    return (g_tty.rx_head - g_tty.rx_tail + g_tty.BUF_SIZE) % g_tty.BUF_SIZE;
}

static size_t tty_read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    (void)node;
    (void)offset;
    char* buf = static_cast<char*>(buffer);
    size_t bytes_read = 0;

    if (g_tty.term.c_lflag & ICANON) {
        // ── Canonical mode: block until a full line (\n) is available ──
        while (g_tty.lines_available == 0) {
            g_tty.waiting_thread = scheduler::scheduler::current_thread();
            scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
        }

        while (bytes_read < size && g_tty.rx_tail != g_tty.rx_head) {
            char c = g_tty.rx_buf[g_tty.rx_tail];
            g_tty.rx_tail = (g_tty.rx_tail + 1) % g_tty.BUF_SIZE;
            buf[bytes_read++] = c;

            if (c == 0x0A) {
                g_tty.lines_available--;
                break;
            }
        }
    } else {
        // ── Raw mode: respect VMIN and VTIME ──
        // VMIN = minimum number of characters before read returns
        // VTIME = timeout in 1/10 second intervals
        //
        // Case 1: VMIN > 0, VTIME = 0 → block until VMIN chars available
        // Case 2: VMIN = 0, VTIME > 0 → block until 1 char or timeout (stub: return immediately)
        // Case 3: VMIN > 0, VTIME > 0 → block until VMIN chars or timeout
        // Case 4: VMIN = 0, VTIME = 0 → return immediately with whatever is available

        unsigned int vmin = g_tty.term.c_cc[6]; // VMIN index = 6
        // unsigned int vtime = g_tty.term.c_cc[5]; // VTIME index = 5 (TODO: timer-based)

        if (vmin == 0) {
            // Non-blocking: return whatever is in the buffer
        } else {
            // Block until at least VMIN characters (or `size`, whichever is less)
            size_t need = (vmin < size) ? vmin : size;
            while (buf_count() < need) {
                g_tty.waiting_thread = scheduler::scheduler::current_thread();
                scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
            }
        }

        // Read available characters up to `size`
        while (bytes_read < size && g_tty.rx_tail != g_tty.rx_head) {
            buf[bytes_read++] = g_tty.rx_buf[g_tty.rx_tail];
            g_tty.rx_tail = (g_tty.rx_tail + 1) % g_tty.BUF_SIZE;
        }

        // Decrement lines_available if we consumed any (for poll readiness tracking)
        if (bytes_read > 0 && g_tty.lines_available > 0) g_tty.lines_available--;
    }

    return bytes_read;
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

static int tty_poll_ready(vfs_node*) noexcept {
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
