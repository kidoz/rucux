// SPDX-License-Identifier: MIT
#include <arch/amd64/framebuffer.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/tty.hpp>
#include <kernel/vfs/vga_font.hpp>
#include <lib/string.hpp>

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

    // Display state
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t fg_color;
    uint32_t bg_color;

    // ANSI escape parsing state
    bool in_escape;
    bool in_csi;
    int ansi_params[4];
    int ansi_param_count;
};

static tty_state g_tty;

static void draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) return; // Unsupported character

    const uint8_t* glyph = minimal_font[c - 32];
    for (int row = 0; row < 8; row++) {
        uint8_t row_data = glyph[row];
        for (int col = 0; col < 8; col++) {
            bool pixel = (row_data >> (7 - col)) & 1;
            arch::amd64::framebuffer::put_pixel(x + col, y + row, pixel ? fg : bg);
        }
    }
}

static void scroll() {
    // For simplicity, we'll just clear the screen when it's full.
    // A true scroll requires reading the framebuffer or keeping a shadow buffer.
    arch::amd64::framebuffer::clear(0x00000000);
    g_tty.cursor_y = 0;
    g_tty.cursor_x = 0;
}

static void tty_putchar(char c) {
    arch::amd64::uart::putc(c);

    if (g_tty.in_escape) {
        if (g_tty.in_csi) {
            if (c >= '0' && c <= '9') {
                g_tty.ansi_params[g_tty.ansi_param_count] *= 10;
                g_tty.ansi_params[g_tty.ansi_param_count] += (c - '0');
            } else if (c == ';') {
                if (g_tty.ansi_param_count < 3) g_tty.ansi_param_count++;
            } else {
                // Command finished
                if (c == 'H') { // Cursor position (row;col)
                    int row = g_tty.ansi_params[0] > 0 ? g_tty.ansi_params[0] - 1 : 0;
                    int col = g_tty.ansi_params[1] > 0 ? g_tty.ansi_params[1] - 1 : 0;
                    g_tty.cursor_y = row * 8;
                    g_tty.cursor_x = col * 8;
                } else if (c == 'J') { // Clear screen
                    if (g_tty.ansi_params[0] == 2) {
                        arch::amd64::framebuffer::clear(0x00000000);
                        g_tty.cursor_y = 0;
                        g_tty.cursor_x = 0;
                    }
                } else if (c == 'm') { // Color
                    // Very simple basic color support
                    if (g_tty.ansi_params[0] == 0) {
                        g_tty.fg_color = 0xFFFFFFFF; // Reset
                    } else if (g_tty.ansi_params[0] >= 30 && g_tty.ansi_params[0] <= 37) {
                        uint32_t colors[] = {0x000000, 0xFF0000, 0x00FF00, 0xFFFF00,
                                             0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
                        g_tty.fg_color = colors[g_tty.ansi_params[0] - 30];
                    }
                }
                g_tty.in_escape = false;
                g_tty.in_csi = false;
            }
        } else {
            if (c == '[') {
                g_tty.in_csi = true;
                g_tty.ansi_params[0] = 0;
                g_tty.ansi_params[1] = 0;
                g_tty.ansi_params[2] = 0;
                g_tty.ansi_params[3] = 0;
                g_tty.ansi_param_count = 0;
            } else {
                g_tty.in_escape = false;
            }
        }
        return;
    }

    if (c == 0x1B) { // Escape
        g_tty.in_escape = true;
        g_tty.in_csi = false;
        return;
    }

    if (c == 0x0A) { // Newline
        g_tty.cursor_y += 8;
        if (g_tty.term.c_oflag & ECHONL) {
            g_tty.cursor_x = 0;
        }
    } else if (c == 0x0D) { // Carriage return
        g_tty.cursor_x = 0;
    } else if (c == 0x08) { // Backspace
        if (g_tty.cursor_x >= 8) {
            g_tty.cursor_x -= 8;
            arch::amd64::framebuffer::fill_rect(g_tty.cursor_x, g_tty.cursor_y, 8, 8, g_tty.bg_color);
        }
    } else {
        draw_char(c, g_tty.cursor_x, g_tty.cursor_y, g_tty.fg_color, g_tty.bg_color);
        g_tty.cursor_x += 8;
    }

    // Wrap around
    if (g_tty.cursor_x >= arch::amd64::framebuffer::get_width()) {
        g_tty.cursor_x = 0;
        g_tty.cursor_y += 8;
    }

    if (g_tty.cursor_y >= arch::amd64::framebuffer::get_height()) {
        scroll();
    }
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
        // Font is 8x8. Calculate row and column counts.
        ws->ws_xpixel = arch::amd64::framebuffer::get_width();
        ws->ws_ypixel = arch::amd64::framebuffer::get_height();
        ws->ws_col = ws->ws_xpixel / 8;
        ws->ws_row = ws->ws_ypixel / 8;
        return 0;
    }
    return -1;
}

vfs_node* create() noexcept {
    g_tty.rx_head = 0;
    g_tty.rx_tail = 0;
    g_tty.lines_available = 0;
    g_tty.waiting_thread = nullptr;
    g_tty.cursor_x = 0;
    g_tty.cursor_y = 0;
    g_tty.fg_color = 0xFFFFFFFF; // White
    g_tty.bg_color = 0x00000000; // Black
    g_tty.in_escape = false;
    g_tty.in_csi = false;

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
