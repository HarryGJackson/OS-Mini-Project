#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include "monitor_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Container Memory Monitor (Task 4)");

// Structure to track each container in our linked list
struct monitored_task {
    pid_t pid;
    struct task_struct *task;
    unsigned long soft_limit_mib;
    unsigned long hard_limit_mib;
    bool soft_warned;
    struct list_head list;
};

// The lock-protected kernel linked list
static LIST_HEAD(monitored_tasks);
static DEFINE_MUTEX(monitor_lock);

// The delayed work struct for our periodic timer
static struct delayed_work monitor_work;

// =========================================================
// THE ENFORCER: Wakes up periodically to check RSS limits
// =========================================================
static void monitor_memory_func(struct work_struct *work) {
    struct monitored_task *mt, *tmp;
    struct mm_struct *mm;
    unsigned long rss_pages, rss_mib;

    mutex_lock(&monitor_lock);

    // Loop safely through the linked list
    list_for_each_entry_safe(mt, tmp, &monitored_tasks, list) {
        // 1. Check if the process exited or died naturally
        if (mt->task->__state == TASK_DEAD || mt->task->exit_state != 0) {
            printk(KERN_INFO "Monitor: PID %d exited normally. Removing from list.\n", mt->pid);
            list_del(&mt->list);
            kfree(mt);
            continue;
        }

        // 2. Safely get memory info
        mm = get_task_mm(mt->task);
        if (mm) {
            // Calculate Resident Set Size (RSS) in MiB
            rss_pages = get_mm_rss(mm);
            rss_mib = (rss_pages << PAGE_SHIFT) / (1024 * 1024);
            mmput(mm);

            // 3. HARD LIMIT ENFORCEMENT
            if (rss_mib >= mt->hard_limit_mib) {
                printk(KERN_ALERT "Monitor: [HARD LIMIT] PID %d exceeded %lu MiB (Used: %lu MiB). Terminating!\n", 
                       mt->pid, mt->hard_limit_mib, rss_mib);
                
                // Kill the container
                send_sig(SIGKILL, mt->task, 0); 
                
                // Remove from tracking list
                list_del(&mt->list);
                kfree(mt);
                continue;
            }

            // 4. SOFT LIMIT ENFORCEMENT
            if (rss_mib >= mt->soft_limit_mib && !mt->soft_warned) {
                printk(KERN_WARNING "Monitor: [SOFT LIMIT] PID %d exceeded %lu MiB (Used: %lu MiB).\n", 
                       mt->pid, mt->soft_limit_mib, rss_mib);
                mt->soft_warned = true; // Only warn once
            }
        }
    }

    mutex_unlock(&monitor_lock);

    // Schedule this function to run again in 1000 milliseconds (1 second)
    schedule_delayed_work(&monitor_work, msecs_to_jiffies(1000));
}

// =========================================================
// THE BRIDGE: Handles commands sent from engine.c
// =========================================================
static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct monitor_cmd user_cmd;
    struct task_struct *target_task;
    struct monitored_task *new_task;

    if (cmd == IOCTL_REGISTER_PID) {
        if (copy_from_user(&user_cmd, (struct monitor_cmd __user *)arg, sizeof(user_cmd))) {
            return -EFAULT;
        }

        // Find the Linux task_struct associated with the PID
        rcu_read_lock();
        target_task = pid_task(find_vpid(user_cmd.pid), PIDTYPE_PID);
        rcu_read_unlock();

        if (!target_task) {
            printk(KERN_ERR "Monitor: Could not find PID %d\n", user_cmd.pid);
            return -ESRCH;
        }

        // Allocate memory for our tracking node
        new_task = kmalloc(sizeof(*new_task), GFP_KERNEL);
        if (!new_task) return -ENOMEM;

        new_task->pid = user_cmd.pid;
        new_task->task = target_task;
        new_task->soft_limit_mib = user_cmd.soft_mib;
        new_task->hard_limit_mib = user_cmd.hard_mib;
        new_task->soft_warned = false;

        // Safely add it to the linked list
        mutex_lock(&monitor_lock);
        list_add_tail(&new_task->list, &monitored_tasks);
        mutex_unlock(&monitor_lock);

        printk(KERN_INFO "Monitor: Registered PID %d (Soft: %lu MiB, Hard: %lu MiB)\n", 
               user_cmd.pid, user_cmd.soft_mib, user_cmd.hard_mib);
    }
    return 0;
}

static const struct file_operations monitor_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl,
};

static struct miscdevice monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "container_monitor",
    .fops = &monitor_fops,
};

// =========================================================
// MODULE INIT & EXIT
// =========================================================
static int __init monitor_init(void) {
    int ret = misc_register(&monitor_dev);
    if (ret) return ret;

    // Start the periodic memory checker
    INIT_DELAYED_WORK(&monitor_work, monitor_memory_func);
    schedule_delayed_work(&monitor_work, msecs_to_jiffies(1000));

    printk(KERN_INFO "Monitor: Active and enforcing memory limits.\n");
    return 0;
}

static void __exit monitor_exit(void) {
    struct monitored_task *mt, *tmp;

    // Stop the background checker thread
    cancel_delayed_work_sync(&monitor_work);
    misc_deregister(&monitor_dev);

    // Free any remaining memory
    mutex_lock(&monitor_lock);
    list_for_each_entry_safe(mt, tmp, &monitored_tasks, list) {
        list_del(&mt->list);
        kfree(mt);
    }
    mutex_unlock(&monitor_lock);

    printk(KERN_INFO "Monitor: Module unloaded safely.\n");
}

module_init(monitor_init);
module_exit(monitor_exit);
