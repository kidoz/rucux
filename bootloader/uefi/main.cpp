// SPDX-License-Identifier: MIT
#include "uefi.hpp"
#include <kernel/boot_protocol.hpp>

// Minimal ELF structures for the bootloader
struct elf64_ehdr {
    UINT8 e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
};

struct elf64_phdr {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
};

static EFI_SYSTEM_TABLE* gST = nullptr;
static EFI_BOOT_SERVICES* gBS = nullptr;

void Print(const char16_t* str) {
    if (gST && gST->ConOut) {
        gST->ConOut->OutputString(gST->ConOut, (const UINT16*)str);
    }
}

#ifdef __x86_64__
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#endif

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    gST = SystemTable;
    gBS = SystemTable->BootServices;

#ifdef __x86_64__
    // Hardcoded outb to COM1 to confirm we reached the bootloader
    for (const char* p = "\nUEFI BOOTLOADER STARTED!\n"; *p; ++p) {
        while (!(inb(0x3F8 + 5) & 0x20))
            ;
        outb(0x3F8, *p);
    }
#endif

    // Reset console and clear screen
    gST->ConOut->Reset(gST->ConOut, false);
    gST->ConOut->ClearScreen(gST->ConOut);

    Print(u"Rucux UEFI Bootloader Initializing...\r\n");

    EFI_STATUS Status;

    // 1. Locate the filesystem protocol on the boot device
    EFI_GUID loadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage = nullptr;
    Status = gBS->HandleProtocol(ImageHandle, &loadedImageGuid, (void**)&loadedImage);
    if (EFI_ERROR(Status)) {
        Print(u"Failed to get LoadedImage protocol\r\n");
        while (1) {
        }
    }

    EFI_GUID sfsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = nullptr;
    Status = gBS->HandleProtocol(loadedImage->DeviceHandle, &sfsGuid, (void**)&sfs);
    if (EFI_ERROR(Status)) {
        Print(u"Failed to get FileSystem protocol\r\n");
        while (1) {
        }
    }

    // 2. Open Root Directory
    EFI_FILE_PROTOCOL* root = nullptr;
    Status = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(Status)) {
        Print(u"Failed to open root volume\r\n");
        while (1) {
        }
    }

    // 3. Open kernel.elf
    EFI_FILE_PROTOCOL* kernelFile = nullptr;
    Status = root->Open(root, &kernelFile, (const UINT16*)u"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(u"Failed to open kernel.elf\r\n");
        while (1) {
        }
    }

    Print(u"Found kernel.elf. Loading...\r\n");

    // 4. Read ELF Header
    elf64_ehdr ehdr;
    UINTN headerSize = sizeof(ehdr);
    Status = kernelFile->Read(kernelFile, &headerSize, &ehdr);
    if (EFI_ERROR(Status) || headerSize != sizeof(ehdr)) {
        Print(u"Failed to read ELF header\r\n");
        while (1) {
        }
    }

    // 5. Read Program Headers
    UINTN phdrsSize = ehdr.e_phnum * ehdr.e_phentsize;
    elf64_phdr* phdrs = nullptr;
    Status = gBS->AllocatePool(EfiLoaderData, phdrsSize, (void**)&phdrs);
    if (EFI_ERROR(Status)) {
        Print(u"Failed to allocate pool for program headers\r\n");
        while (1) {
        }
    }

    Status = kernelFile->SetPosition(kernelFile, ehdr.e_phoff);
    Status = kernelFile->Read(kernelFile, &phdrsSize, phdrs);

    // 6. Allocate and Load Segments
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == 1) { // PT_LOAD
            UINTN pages = (phdrs[i].p_memsz + 0xFFF) / 0x1000;
            EFI_PHYSICAL_ADDRESS allocAddr = phdrs[i].p_paddr;

            Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &allocAddr);
            if (EFI_ERROR(Status)) {
                Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &allocAddr);
                if (EFI_ERROR(Status)) {
                    Print(u"Failed to allocate pages for segment\r\n");
                    while (1) {
                    }
                }
            }

            UINT8* dest = (UINT8*)allocAddr;
            for (UINTN j = 0; j < phdrs[i].p_memsz; j++)
                dest[j] = 0;

            if (phdrs[i].p_filesz > 0) {
                Status = kernelFile->SetPosition(kernelFile, phdrs[i].p_offset);
                UINTN readSize = phdrs[i].p_filesz;
                Status = kernelFile->Read(kernelFile, &readSize, dest);
                if (EFI_ERROR(Status)) {
                    Print(u"Failed to read segment data\r\n");
                    while (1) {
                    }
                }
            }
        }
    }

    Print(u"Kernel loaded into memory.\r\n");

    // 7. Get Framebuffer
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
    Status = gBS->LocateProtocol(&gopGuid, nullptr, (void**)&gop);

    rucux_boot_info* bootInfo = nullptr;
    gBS->AllocatePool(EfiLoaderData, sizeof(rucux_boot_info), (void**)&bootInfo);

    if (!EFI_ERROR(Status) && gop) {
        bootInfo->fb_address = gop->Mode->FrameBufferBase;
        bootInfo->fb_width = gop->Mode->Info->HorizontalResolution;
        bootInfo->fb_height = gop->Mode->Info->VerticalResolution;
        bootInfo->fb_pitch = gop->Mode->Info->HorizontalResolution * 4;
        bootInfo->fb_bpp = 32;
    } else {
        bootInfo->fb_address = 0;
    }

    bootInfo->magic = RUCUX_BOOT_MAGIC;

    Print(u"Preparing to exit boot services...\r\n");

    // 8. Get Memory Map and Exit Boot Services
    UINTN mapSize = 0;
    EFI_MEMORY_DESCRIPTOR* mmap = nullptr;
    UINTN mapKey, descSize;
    UINT32 descVer;

    gBS->GetMemoryMap(&mapSize, nullptr, &mapKey, &descSize, &descVer);
    mapSize += descSize * 8; // Padding

    gBS->AllocatePool(EfiLoaderData, mapSize, (void**)&mmap);
    gBS->GetMemoryMap(&mapSize, mmap, &mapKey, &descSize, &descVer);

    Status = gBS->ExitBootServices(ImageHandle, mapKey);
    if (EFI_ERROR(Status)) {
        gBS->GetMemoryMap(&mapSize, mmap, &mapKey, &descSize, &descVer);
        Status = gBS->ExitBootServices(ImageHandle, mapKey);
        if (EFI_ERROR(Status)) {
            while (1) {
            } // Dead
        }
    }

    rucux_mmap_entry* rucux_mmap = (rucux_mmap_entry*)mmap;
    int entries = mapSize / descSize;

    for (int i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)mmap + (i * descSize));
        UINT64 p_start = desc->PhysicalStart;
        UINT64 v_start = desc->VirtualStart;
        UINT64 pages = desc->NumberOfPages;
        UINT32 type = desc->Type;

        rucux_mmap[i].physical_start = p_start;
        rucux_mmap[i].virtual_start = v_start;
        rucux_mmap[i].number_of_pages = pages;
        if (type == EfiConventionalMemory || type == EfiBootServicesCode || type == EfiBootServicesData) {
            rucux_mmap[i].type = 1;
        } else {
            rucux_mmap[i].type = 2;
        }
    }

    bootInfo->mmap = rucux_mmap;
    bootInfo->mmap_size = entries * sizeof(rucux_mmap_entry);
    bootInfo->mmap_descriptor_size = sizeof(rucux_mmap_entry);

    // 10. Jump to kernel!
#ifdef __x86_64__
    typedef void(__attribute__((sysv_abi)) * KernelEntry)(rucux_boot_info*);
#else
    typedef void (*KernelEntry)(rucux_boot_info*);
#endif
    KernelEntry kernel = (KernelEntry)ehdr.e_entry;

    kernel(bootInfo);

    while (1) {
#ifdef __x86_64__
        asm volatile("hlt");
#else
        asm volatile("wfi");
#endif
    }
    return EFI_SUCCESS;
}
