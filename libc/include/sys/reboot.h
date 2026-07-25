// SPDX-License-Identifier: MIT
#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

#include <uapi/kernel/power.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_AUTOBOOT RUCUX_POWER_CTL_REBOOT
#define RB_POWER_OFF RUCUX_POWER_CTL_POWEROFF

int reboot(int howto);

#ifdef __cplusplus
}
#endif

#endif // _SYS_REBOOT_H
