// SPDX-License-Identifier: MIT
#include <kernel/fdt.hpp>
#include <lib/string.hpp>

namespace kernel::fdt {

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

#define FDT_MAGIC 0xd00dfeed
#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_NOP 4
#define FDT_END 9

static const fdt_header* g_fdt = nullptr;

static inline uint32_t be32(uint32_t v) {
    return __builtin_bswap32(v);
}

static inline uint64_t be64(uint64_t v) {
    return __builtin_bswap64(v);
}

static inline uint32_t align4(uint32_t v) {
    return (v + 3) & ~3;
}

bool init(void* fdt_blob) {
    if (!fdt_blob) return false;
    const fdt_header* hdr = static_cast<const fdt_header*>(fdt_blob);
    if (be32(hdr->magic) != FDT_MAGIC) return false;
    g_fdt = hdr;
    return true;
}

static const char* get_string(uint32_t offset) {
    if (!g_fdt) return nullptr;
    const char* strings = reinterpret_cast<const char*>(g_fdt) + be32(g_fdt->off_dt_strings);
    return strings + offset;
}

// Simple internal FDT walker
template <typename F>
static void walk_fdt(F&& callback) {
    if (!g_fdt) return;
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(g_fdt) + be32(g_fdt->off_dt_struct));

    const char* current_node_name = "";
    int depth = 0;

    while (true) {
        uint32_t token = be32(*ptr++);
        switch (token) {
            case FDT_BEGIN_NODE: {
                current_node_name = reinterpret_cast<const char*>(ptr);
                uint32_t name_len = lib::strlen(current_node_name);
                ptr += align4(name_len + 1) / 4;
                depth++;
                break;
            }
            case FDT_END_NODE: {
                depth--;
                if (depth < 0) return;
                break;
            }
            case FDT_PROP: {
                uint32_t len = be32(*ptr++);
                uint32_t nameoff = be32(*ptr++);
                const char* prop_name = get_string(nameoff);
                const void* prop_data = ptr;
                ptr += align4(len) / 4;
                
                if (depth > 0) {
                    // Call the callback for each property
                    callback(current_node_name, prop_name, prop_data, len);
                }
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
                return;
            default:
                return; // Error or unknown token
        }
    }
}

// Helper to check if a comma-separated compatible string list contains a target
static bool contains_compatible(const char* list, uint32_t len, const char* target) {
    uint32_t i = 0;
    while (i < len) {
        const char* str = list + i;
        if (lib::strcmp(str, target) == 0) {
            return true;
        }
        i += lib::strlen(str) + 1;
    }
    return false;
}

// Get the first memory node
bool get_memory(uint64_t* base, uint64_t* size) {
    bool found = false;
    walk_fdt([&](const char* node, const char* prop, const void* data, uint32_t len) {
        if (found) return;
        if (node[0] == 'm' && node[1] == 'e' && node[2] == 'm' && node[3] == 'o' && node[4] == 'r' && node[5] == 'y') {
            if (lib::strcmp(prop, "reg") == 0 && len >= 8) {
                const uint32_t* regs = static_cast<const uint32_t*>(data);
                if (len == 16) {
                    *base = be64(*reinterpret_cast<const uint64_t*>(&regs[0]));
                    *size = be64(*reinterpret_cast<const uint64_t*>(&regs[2]));
                } else {
                    *base = be32(regs[0]);
                    *size = be32(regs[1]);
                }
                found = true;
            }
        }
    });
    return found;
}

bool get_uart(uintptr_t* base, bool* is_pl011) {
    if (!g_fdt) return false;
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(g_fdt) + be32(g_fdt->off_dt_struct));

    int depth = 0;
    bool current_is_pl011 = false;
    bool current_is_meson = false;
    bool has_reg = false;
    uintptr_t current_base = 0;

    while (true) {
        uint32_t token = be32(*ptr++);
        switch (token) {
            case FDT_BEGIN_NODE: {
                uint32_t name_len = lib::strlen(reinterpret_cast<const char*>(ptr));
                ptr += align4(name_len + 1) / 4;
                depth++;
                current_is_pl011 = false;
                current_is_meson = false;
                has_reg = false;
                break;
            }
            case FDT_END_NODE: {
                if ((current_is_pl011 || current_is_meson) && has_reg) {
                    *base = current_base;
                    *is_pl011 = current_is_pl011;
                    return true;
                }
                depth--;
                if (depth < 0) return false;
                break;
            }
            case FDT_PROP: {
                uint32_t len = be32(*ptr++);
                uint32_t nameoff = be32(*ptr++);
                const char* prop_name = get_string(nameoff);
                const void* prop_data = ptr;
                ptr += align4(len) / 4;
                
                if (lib::strcmp(prop_name, "compatible") == 0) {
                    if (contains_compatible(static_cast<const char*>(prop_data), len, "arm,pl011")) {
                        current_is_pl011 = true;
                    } else if (contains_compatible(static_cast<const char*>(prop_data), len, "amlogic,meson-gx-uart")) {
                        current_is_meson = true;
                    }
                } else if (lib::strcmp(prop_name, "reg") == 0) {
                    const uint32_t* regs = static_cast<const uint32_t*>(prop_data);
                    if (len >= 8 && regs[0] == 0) { 
                        current_base = be32(regs[1]);
                        has_reg = true;
                    } else if (len >= 4) {
                        current_base = be32(regs[0]);
                        has_reg = true;
                    }
                }
                if ((current_is_pl011 || current_is_meson) && has_reg) {
                    *base = current_base;
                    *is_pl011 = current_is_pl011;
                    return true;
                }
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
                return false;
            default:
                return false;
        }
    }
    return false;
}

bool get_gic(uintptr_t* dist_base, uintptr_t* cpu_base) {
    if (!g_fdt) return false;
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(g_fdt) + be32(g_fdt->off_dt_struct));

    int depth = 0;
    bool is_gic = false;
    bool has_reg = false;
    uintptr_t cur_dist = 0;
    uintptr_t cur_cpu = 0;

    while (true) {
        uint32_t token = be32(*ptr++);
        switch (token) {
            case FDT_BEGIN_NODE: {
                uint32_t name_len = lib::strlen(reinterpret_cast<const char*>(ptr));
                ptr += align4(name_len + 1) / 4;
                depth++;
                is_gic = false;
                has_reg = false;
                break;
            }
            case FDT_END_NODE: {
                if (is_gic && has_reg) {
                    *dist_base = cur_dist;
                    *cpu_base = cur_cpu;
                    return true;
                }
                depth--;
                if (depth < 0) return false;
                break;
            }
            case FDT_PROP: {
                uint32_t len = be32(*ptr++);
                uint32_t nameoff = be32(*ptr++);
                const char* prop_name = get_string(nameoff);
                const void* prop_data = ptr;
                ptr += align4(len) / 4;
                
                if (lib::strcmp(prop_name, "compatible") == 0) {
                    const char* comp = static_cast<const char*>(prop_data);
                    if (contains_compatible(comp, len, "arm,cortex-a15-gic") ||
                        contains_compatible(comp, len, "arm,cortex-a9-gic")) {
                        is_gic = true;
                    }
                } else if (lib::strcmp(prop_name, "reg") == 0) {
                    const uint32_t* regs = static_cast<const uint32_t*>(prop_data);
                    if (len >= 32 && regs[0] == 0) {
                        // 64-bit addresses
                        cur_dist = be32(regs[1]);
                        cur_cpu = be32(regs[5]);
                        has_reg = true;
                    } else if (len >= 16) {
                        // 32-bit addresses
                        cur_dist = be32(regs[0]);
                        cur_cpu = be32(regs[2]);
                        has_reg = true;
                    }
                }
                if (is_gic && has_reg) {
                    *dist_base = cur_dist;
                    *cpu_base = cur_cpu;
                    return true;
                }
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
                return false;
            default:
                return false;
        }
    }
    return false;
}

device_info get_device_info() {
    device_info info = {false, false, false, false, false};
    if (!g_fdt) return info;
    
    walk_fdt([&](const char*, const char* prop, const void* data, uint32_t len) {
        if (lib::strcmp(prop, "compatible") == 0) {
            const char* comp = static_cast<const char*>(data);
            if (contains_compatible(comp, len, "snps,dwmac")) info.has_dwmac = true;
            if (contains_compatible(comp, len, "arm,mali-450")) info.has_mali450 = true;
            // Add USB and Watchdog heuristics if needed
            if (contains_compatible(comp, len, "snps,dwc2")) info.has_usb = true;
            if (contains_compatible(comp, len, "amlogic,meson-gxbb-wdt")) info.has_watchdog = true;
            if (contains_compatible(comp, len, "amlogic,meson-rng")) info.has_hw_rng = true;
        }
    });
    
    return info;
}

} // namespace kernel::fdt
