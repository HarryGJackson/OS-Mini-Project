#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H
#include <linux/ioctl.h>

#define MONITOR_MAGIC 'c'
struct monitor_cmd {
    int pid;
    unsigned long soft_mib;
    unsigned long hard_mib;
};

#define IOCTL_REGISTER_PID _IOW(MONITOR_MAGIC, 1, struct monitor_cmd)
#define IOCTL_UNREGISTER_PID _IOW(MONITOR_MAGIC, 2, int) // For Task 6 Cleanup
#endif
