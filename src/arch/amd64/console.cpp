// SPDX-License-Identifier: MIT
#include <arch/amd64/console.hpp>
#include <arch/amd64/framebuffer.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/console.hpp>
#include <kernel/vfs/vga_font.hpp>
#include <lib/string.hpp>

namespace arch::amd64::console {
namespace {

constexpr uint32_t FONT_WIDTH = 8;
constexpr uint32_t FONT_HEIGHT = 8;
constexpr int MAX_PARAMS = 8;

// Standard 8 + bright 8 ANSI colors (RGB)
constexpr uint32_t COLORS[16] = {
    0x00000000, 0x00AA0000, 0x0000AA00, 0x00AA5500, // black, red, green, brown
    0x000000AA, 0x00AA00AA, 0x0000AAAA, 0x00AAAAAA, // blue, magenta, cyan, white
    0x00555555, 0x00FF5555, 0x0055FF55, 0x00FFFF55, // bright variants
    0x005555FF, 0x00FF55FF, 0x0055FFFF, 0x00FFFFFF,
};

struct fb_state {
    uint32_t cols;        // Terminal columns
    uint32_t rows;        // Terminal rows
    uint32_t cx, cy;      // Cursor position (character cells, 0-based)
    uint32_t fg, bg;      // Current colors (RGB)
    bool bold;
    bool reverse;
    bool cursor_visible;

    // ANSI parser state
    bool in_escape;
    bool in_csi;
    bool got_question;    // CSI ? prefix (private modes)
    int params[MAX_PARAMS];
    int nparam;

    // Screen buffer for scrolling (character + attribute per cell)
    struct cell {
        char ch;
        uint32_t fg, bg;
    };
    cell* screen;         // cols * rows cells (allocated after framebuffer init)
};

fb_state g_fb = {};
bool g_fb_registered = false;

// ─── Low-level drawing ─────────────────────────────────────────────────────

void draw_char_at(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg) noexcept {
    uint32_t px = col * FONT_WIDTH;
    uint32_t py = row * FONT_HEIGHT;
    if (c < 32 || c > 126) c = ' ';
    const uint8_t* glyph = kernel::vfs::tty::minimal_font[c - 32];
    for (uint32_t r = 0; r < FONT_HEIGHT; ++r) {
        uint8_t bits = glyph[r];
        for (uint32_t cl = 0; cl < FONT_WIDTH; ++cl)
            framebuffer::put_pixel(px + cl, py + r, ((bits >> (7 - cl)) & 1) ? fg : bg);
    }
}

void clear_cell(uint32_t col, uint32_t row) noexcept {
    if (g_fb.screen) {
        auto& cell = g_fb.screen[row * g_fb.cols + col];
        cell.ch = ' ';
        cell.fg = g_fb.fg;
        cell.bg = g_fb.bg;
    }
    draw_char_at(col, row, ' ', g_fb.fg, g_fb.bg);
}

void redraw_cell(uint32_t col, uint32_t row) noexcept {
    if (!g_fb.screen) return;
    auto& cell = g_fb.screen[row * g_fb.cols + col];
    draw_char_at(col, row, cell.ch, cell.fg, cell.bg);
}

void set_cell(uint32_t col, uint32_t row, char c) noexcept {
    uint32_t fg = g_fb.reverse ? g_fb.bg : g_fb.fg;
    uint32_t bg = g_fb.reverse ? g_fb.fg : g_fb.bg;
    if (g_fb.screen) {
        auto& cell = g_fb.screen[row * g_fb.cols + col];
        cell.ch = c;
        cell.fg = fg;
        cell.bg = bg;
    }
    draw_char_at(col, row, c, fg, bg);
}

// Scroll the screen up by one line
void scroll_up() noexcept {
    if (!g_fb.screen) {
        framebuffer::clear(g_fb.bg);
        g_fb.cx = g_fb.cy = 0;
        return;
    }
    // Move rows 1..N-1 to 0..N-2
    size_t line_cells = g_fb.cols;
    lib::memmove(g_fb.screen, g_fb.screen + line_cells,
                 line_cells * (g_fb.rows - 1) * sizeof(fb_state::cell));
    // Clear last row
    for (uint32_t c = 0; c < g_fb.cols; ++c) {
        auto& cell = g_fb.screen[(g_fb.rows - 1) * g_fb.cols + c];
        cell.ch = ' '; cell.fg = g_fb.fg; cell.bg = g_fb.bg;
    }
    // Redraw entire screen (could be optimized with framebuffer scroll later)
    for (uint32_t r = 0; r < g_fb.rows; ++r)
        for (uint32_t c = 0; c < g_fb.cols; ++c)
            redraw_cell(c, r);
}

// Scroll down by one line (insert blank line at top)
void scroll_down() noexcept {
    if (!g_fb.screen) return;
    size_t line_cells = g_fb.cols;
    lib::memmove(g_fb.screen + line_cells, g_fb.screen,
                 line_cells * (g_fb.rows - 1) * sizeof(fb_state::cell));
    for (uint32_t c = 0; c < g_fb.cols; ++c) {
        auto& cell = g_fb.screen[c];
        cell.ch = ' '; cell.fg = g_fb.fg; cell.bg = g_fb.bg;
    }
    for (uint32_t r = 0; r < g_fb.rows; ++r)
        for (uint32_t c = 0; c < g_fb.cols; ++c)
            redraw_cell(c, r);
}

// ─── ANSI parser helpers ───────────────────────────────────────────────────

int param(int idx, int def = 0) {
    return (idx < g_fb.nparam && g_fb.params[idx] > 0) ? g_fb.params[idx] : def;
}

void reset_parser() noexcept {
    g_fb.in_escape = false;
    g_fb.in_csi = false;
    g_fb.got_question = false;
    g_fb.nparam = 0;
    for (auto& p : g_fb.params) p = 0;
}

void apply_sgr(int code) noexcept {
    if (code == 0)       { g_fb.fg = COLORS[7]; g_fb.bg = COLORS[0]; g_fb.bold = false; g_fb.reverse = false; }
    else if (code == 1)  { g_fb.bold = true; }
    else if (code == 2)  { g_fb.bold = false; } // dim
    else if (code == 7)  { g_fb.reverse = true; }
    else if (code == 27) { g_fb.reverse = false; }
    else if (code >= 30 && code <= 37)   { g_fb.fg = COLORS[code - 30 + (g_fb.bold ? 8 : 0)]; }
    else if (code == 39)                 { g_fb.fg = COLORS[7]; } // default fg
    else if (code >= 40 && code <= 47)   { g_fb.bg = COLORS[code - 40]; }
    else if (code == 49)                 { g_fb.bg = COLORS[0]; } // default bg
    else if (code >= 90 && code <= 97)   { g_fb.fg = COLORS[code - 90 + 8]; } // bright fg
    else if (code >= 100 && code <= 107) { g_fb.bg = COLORS[code - 100 + 8]; } // bright bg
}

void finish_csi(char cmd) noexcept {
    uint32_t n;
    switch (cmd) {
    case 'A': // Cursor Up
        n = param(0, 1);
        g_fb.cy = (g_fb.cy >= n) ? g_fb.cy - n : 0;
        break;
    case 'B': // Cursor Down
        n = param(0, 1);
        g_fb.cy = (g_fb.cy + n < g_fb.rows) ? g_fb.cy + n : g_fb.rows - 1;
        break;
    case 'C': // Cursor Forward
        n = param(0, 1);
        g_fb.cx = (g_fb.cx + n < g_fb.cols) ? g_fb.cx + n : g_fb.cols - 1;
        break;
    case 'D': // Cursor Back
        n = param(0, 1);
        g_fb.cx = (g_fb.cx >= n) ? g_fb.cx - n : 0;
        break;
    case 'H': // Cursor Position (row;col) — 1-based
    case 'f':
        g_fb.cy = static_cast<uint32_t>(param(0, 1) - 1);
        g_fb.cx = static_cast<uint32_t>(param(1, 1) - 1);
        if (g_fb.cy >= g_fb.rows) g_fb.cy = g_fb.rows - 1;
        if (g_fb.cx >= g_fb.cols) g_fb.cx = g_fb.cols - 1;
        break;
    case 'J': // Erase in Display
        if (param(0) == 0) { // Erase from cursor to end
            for (uint32_t c = g_fb.cx; c < g_fb.cols; ++c) clear_cell(c, g_fb.cy);
            for (uint32_t r = g_fb.cy + 1; r < g_fb.rows; ++r)
                for (uint32_t c = 0; c < g_fb.cols; ++c) clear_cell(c, r);
        } else if (param(0) == 1) { // Erase from start to cursor
            for (uint32_t r = 0; r < g_fb.cy; ++r)
                for (uint32_t c = 0; c < g_fb.cols; ++c) clear_cell(c, r);
            for (uint32_t c = 0; c <= g_fb.cx; ++c) clear_cell(c, g_fb.cy);
        } else if (param(0) == 2) { // Erase entire screen
            for (uint32_t r = 0; r < g_fb.rows; ++r)
                for (uint32_t c = 0; c < g_fb.cols; ++c) clear_cell(c, r);
            g_fb.cx = g_fb.cy = 0;
        }
        break;
    case 'K': // Erase in Line
        if (param(0) == 0) { // Erase from cursor to end of line
            for (uint32_t c = g_fb.cx; c < g_fb.cols; ++c) clear_cell(c, g_fb.cy);
        } else if (param(0) == 1) { // Erase from start to cursor
            for (uint32_t c = 0; c <= g_fb.cx; ++c) clear_cell(c, g_fb.cy);
        } else if (param(0) == 2) { // Erase entire line
            for (uint32_t c = 0; c < g_fb.cols; ++c) clear_cell(c, g_fb.cy);
        }
        break;
    case 'L': // Insert N lines (scroll down from cursor)
        n = param(0, 1);
        for (uint32_t i = 0; i < n; ++i) scroll_down();
        break;
    case 'M': // Delete N lines (scroll up from cursor)
        n = param(0, 1);
        for (uint32_t i = 0; i < n; ++i) scroll_up();
        break;
    case 'm': // SGR (Select Graphic Rendition)
        if (g_fb.nparam == 0) { apply_sgr(0); }
        else { for (int i = 0; i < g_fb.nparam; ++i) apply_sgr(g_fb.params[i]); }
        break;
    case 'h': // Set Mode
        if (g_fb.got_question && param(0) == 25) g_fb.cursor_visible = true;
        break;
    case 'l': // Reset Mode
        if (g_fb.got_question && param(0) == 25) g_fb.cursor_visible = false;
        break;
    case 'G': // Cursor Horizontal Absolute
        g_fb.cx = static_cast<uint32_t>(param(0, 1) - 1);
        if (g_fb.cx >= g_fb.cols) g_fb.cx = g_fb.cols - 1;
        break;
    case 'd': // Cursor Vertical Absolute
        g_fb.cy = static_cast<uint32_t>(param(0, 1) - 1);
        if (g_fb.cy >= g_fb.rows) g_fb.cy = g_fb.rows - 1;
        break;
    case 'S': // Scroll Up N lines
        n = param(0, 1);
        for (uint32_t i = 0; i < n; ++i) scroll_up();
        break;
    case 'T': // Scroll Down N lines
        n = param(0, 1);
        for (uint32_t i = 0; i < n; ++i) scroll_down();
        break;
    case 'X': // Erase N characters
        n = param(0, 1);
        for (uint32_t i = 0; i < n && g_fb.cx + i < g_fb.cols; ++i)
            clear_cell(g_fb.cx + i, g_fb.cy);
        break;
    case 'P': // Delete N characters (shift left)
        n = param(0, 1);
        if (g_fb.screen) {
            for (uint32_t c = g_fb.cx; c + n < g_fb.cols; ++c) {
                g_fb.screen[g_fb.cy * g_fb.cols + c] = g_fb.screen[g_fb.cy * g_fb.cols + c + n];
                redraw_cell(c, g_fb.cy);
            }
            for (uint32_t c = g_fb.cols - n; c < g_fb.cols; ++c) clear_cell(c, g_fb.cy);
        }
        break;
    case '@': // Insert N blank characters (shift right)
        n = param(0, 1);
        if (g_fb.screen) {
            for (uint32_t c = g_fb.cols - 1; c >= g_fb.cx + n; --c) {
                g_fb.screen[g_fb.cy * g_fb.cols + c] = g_fb.screen[g_fb.cy * g_fb.cols + c - n];
                redraw_cell(c, g_fb.cy);
            }
            for (uint32_t c = g_fb.cx; c < g_fb.cx + n && c < g_fb.cols; ++c) clear_cell(c, g_fb.cy);
        }
        break;
    default:
        break; // Unknown — ignore
    }
    reset_parser();
}

// ─── Main putc entry ───────────────────────────────────────────────────────

void framebuffer_sink_putc(char c) noexcept {
    if (framebuffer::get_width() == 0) return;

    // ANSI escape processing
    if (g_fb.in_escape) {
        if (g_fb.in_csi) {
            if (c >= '0' && c <= '9') {
                if (g_fb.nparam == 0) g_fb.nparam = 1;
                g_fb.params[g_fb.nparam - 1] = g_fb.params[g_fb.nparam - 1] * 10 + (c - '0');
            } else if (c == ';') {
                if (g_fb.nparam < MAX_PARAMS) g_fb.nparam++;
            } else if (c == '?') {
                g_fb.got_question = true;
            } else {
                finish_csi(c);
            }
        } else if (c == '[') {
            g_fb.in_csi = true;
            g_fb.nparam = 0;
            g_fb.got_question = false;
            for (auto& p : g_fb.params) p = 0;
        } else {
            reset_parser();
        }
        return;
    }

    if (c == 0x1B) {
        g_fb.in_escape = true;
        g_fb.in_csi = false;
        return;
    }

    // Control characters
    if (c == '\n') {
        g_fb.cx = 0;
        g_fb.cy++;
    } else if (c == '\r') {
        g_fb.cx = 0;
    } else if (c == '\b') {
        if (g_fb.cx > 0) g_fb.cx--;
    } else if (c == '\t') {
        uint32_t next_tab = (g_fb.cx + 8) & ~7U;
        while (g_fb.cx < next_tab && g_fb.cx < g_fb.cols)
            set_cell(g_fb.cx++, g_fb.cy, ' ');
    } else {
        set_cell(g_fb.cx, g_fb.cy, c);
        g_fb.cx++;
    }

    // Wrap
    if (g_fb.cx >= g_fb.cols) {
        g_fb.cx = 0;
        g_fb.cy++;
    }
    // Scroll if past bottom
    if (g_fb.cy >= g_fb.rows) {
        scroll_up();
        g_fb.cy = g_fb.rows - 1;
    }
}

kernel::console::display_info framebuffer_display_info() noexcept {
    uint32_t w = framebuffer::get_width();
    uint32_t h = framebuffer::get_height();
    if (w == 0 || h == 0) return {0, 0, 0, 0, false};
    return {w / FONT_WIDTH, h / FONT_HEIGHT, w, h, true};
}

// ─── Sinks ─────────────────────────────────────────────────────────────────

void uart_sink_putc(char c) noexcept {
    uart::putc(c);
}

constexpr kernel::console::sink UART_SINK = {uart_sink_putc, nullptr};
constexpr kernel::console::sink FRAMEBUFFER_SINK = {framebuffer_sink_putc, framebuffer_display_info};

} // namespace

void init_early() noexcept {
    kernel::console::reset();
    g_fb_registered = false;
    g_fb = {};
    g_fb.fg = COLORS[7];
    g_fb.bg = COLORS[0];
    g_fb.cursor_visible = true;
    kernel::console::register_sink(UART_SINK);
}

void enable_framebuffer() noexcept {
    if (g_fb_registered || framebuffer::get_width() == 0) return;

    g_fb.cols = framebuffer::get_width() / FONT_WIDTH;
    g_fb.rows = framebuffer::get_height() / FONT_HEIGHT;

    // Allocate screen buffer for scrolling
    // Use a static buffer to avoid heap dependency at this early stage
    static fb_state::cell screen_buf[256 * 128]; // Up to 256 cols x 128 rows
    if (g_fb.cols * g_fb.rows <= sizeof(screen_buf) / sizeof(screen_buf[0])) {
        g_fb.screen = screen_buf;
        for (uint32_t i = 0; i < g_fb.cols * g_fb.rows; ++i) {
            screen_buf[i] = {' ', g_fb.fg, g_fb.bg};
        }
    }

    framebuffer::clear(g_fb.bg);
    kernel::console::register_sink(FRAMEBUFFER_SINK);
    g_fb_registered = true;
}

} // namespace arch::amd64::console
