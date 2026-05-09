// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define major(dev) ((int)(((dev) >> 8) & 0xff))
#define minor(dev) ((int)((dev) & 0xff))
#define makedev(ma, mi) (((ma) << 8) | (mi))

#ifdef __cplusplus
}
#endif
