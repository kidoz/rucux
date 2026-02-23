ARCH ?= x86_64

ifeq ($(ARCH), x86_64)
    CXX = x86_64-w64-mingw32-g++
    TARGET = BOOTX64.EFI
    CXXFLAGS = -ffreestanding -mno-red-zone -maccumulate-outgoing-args -fpic -fshort-wchar -Wno-address-of-packed-member -Iinclude -I../src/include
    LDFLAGS = -nostdlib -Wl,-dll -shared -Wl,--subsystem,10 -e efi_main
    SRCS = uefi/main.cpp
else ifeq ($(ARCH), aarch64)
    CXX = clang
    TARGET = BOOTAA64.EFI
    CXXFLAGS = -target aarch64-unknown-windows -ffreestanding -fshort-wchar -Wno-address-of-packed-member -Iinclude -I../src/include
    LDFLAGS = -nostdlib -fuse-ld=lld -Wl,-subsystem:efi_application -Wl,-entry:efi_main
    SRCS = uefi/main.cpp
else ifeq ($(ARCH), arm)
    CXX = clang
    TARGET = BOOTARM.EFI
    CXXFLAGS = -target arm-unknown-windows -ffreestanding -fshort-wchar -Wno-address-of-packed-member -Iinclude -I../src/include
    LDFLAGS = -nostdlib -fuse-ld=lld -Wl,-subsystem:efi_application -Wl,-entry:efi_main
    SRCS = uefi/main.cpp
else ifeq ($(ARCH), arm-baremetal)
    CXX = arm-none-eabi-g++
    OBJCOPY = arm-none-eabi-objcopy
    TARGET_ELF = bootloader.elf
    TARGET = BOOTARM.BIN
    CXXFLAGS = -ffreestanding -fno-exceptions -fno-rtti -mcpu=cortex-a53 -Iinclude -I../src/include -O2
    LDFLAGS = -nostdlib -T baremetal/linker.ld
    SRCS = baremetal/main.cpp baremetal/drivers/sd.cpp baremetal/drivers/fat32.cpp
endif

all: $(TARGET)

ifeq ($(ARCH), arm-baremetal)
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET_ELF) $^
	$(OBJCOPY) -O binary $(TARGET_ELF) $(TARGET)
else
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
endif

clean:
	rm -f BOOTX64.EFI BOOTAA64.EFI BOOTARM.EFI BOOTARM.BIN bootloader.elf
