kernel_common_sources = files(
    'kernel/syscall.cpp',
    'kernel/memory/pmm.cpp',
    'kernel/memory/buddy.cpp',
    'kernel/memory/slab.cpp',
    'kernel/memory/heap.cpp',
    'kernel/memory/vma.cpp',
    'kernel/memory/mmap.cpp',
    'kernel/memory/user_access.cpp',
    'kernel/ds/rbtree.cpp',
    'kernel/scheduler/scheduler.cpp',
    'kernel/scheduler/dispatch.cpp',
    'kernel/scheduler/wait_queue.cpp',
    'kernel/sync/futex.cpp',
    'kernel/sync/rcu.cpp',
    'kernel/console.cpp',
    'kernel/process/elf.cpp',
    'kernel/process/elf_validate.cpp',
    'kernel/process/spawn.cpp',
    'kernel/process/credentials.cpp',
    'kernel/process/signal.cpp',
    'kernel/ipc/ipc.cpp',
    'kernel/log.cpp',
    'kernel/trace.cpp',
    'kernel/time.cpp',
    'kernel/vfs/vfs.cpp',
    'kernel/vfs/tty.cpp',
)

kernel_net_sources = files(
    'kernel/net/socket.cpp',
    'kernel/net/netbuf.cpp',
    'kernel/net/netif.cpp',
    'kernel/net/ethernet.cpp',
    'kernel/net/arp.cpp',
    'kernel/net/ipv4.cpp',
    'kernel/net/checksum.cpp',
    'kernel/net/tcp.cpp',
    'kernel/net/udp.cpp',
)

kernel_local_socket_sources = files('kernel/ipc/local_socket.cpp')
kernel_runtime_services_sources = files('kernel/power.cpp')
kernel_ramfs_sources = files('kernel/vfs/ramfs.cpp')
kernel_storage_sources = files(
    'kernel/vfs/fat32.cpp',
)
kernel_fdt_sources = files('kernel/fdt.cpp')
