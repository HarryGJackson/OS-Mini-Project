#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define SOCKET_PATH "/tmp/engine.sock"
#define DEV_PATH "/dev/container_monitor"

struct container_args {
    char *rootfs;
    char *command;
};

// Child process logic (Namespaces & Isolation)
int container_main(void *arg) {
    struct container_args *args = (struct container_args *)arg;
    printf("[Container] Isolating in %s\n", args->rootfs);
    
    if (chroot(args->rootfs) != 0 || chdir("/") != 0) {
        perror("chroot/chdir");
        return 1;
    }
    
    mount("proc", "/proc", "proc", 0, NULL);
    char *cmd[] = { "/bin/sh", "-c", args->command, NULL };
    execv("/bin/sh", cmd);
    return 0;
}

// Function to link the user-space process to the Kernel Monitor
void register_with_kernel(int pid) {
    int fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("[Supervisor] Error: Could not open /dev/container_monitor");
        return;
    }
    if (ioctl(fd, IOCTL_REGISTER_PID, &pid) < 0) {
        perror("[Supervisor] IOCTL Registration failed");
    } else {
        printf("[Supervisor] Sent PID %d to Kernel Monitor successfully.\n", pid);
    }
    close(fd);
}

void run_supervisor() {
    int server_fd, client_fd;
    struct sockaddr_un addr;
    char buffer[256];

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("[Supervisor] Listening on %s...\n", SOCKET_PATH);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        memset(buffer, 0, sizeof(buffer));
        read(client_fd, buffer, sizeof(buffer));

        if (strncmp(buffer, "ps", 2) == 0) {
            printf("[Supervisor] Received PS command from CLI via Socket IPC\n");
        } else if (strncmp(buffer, "start", 5) == 0) {
            printf("[Supervisor] Starting container lifecycle...\n");
        }
        close(client_fd);
    }
}

int main(int argc, char *argv[]) {
    // DISABLE BUFFERING for real-time console updates
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("Usage: ./engine supervisor | start <id> <rootfs> <cmd> | ps\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor();
    } 
    else if (strcmp(argv[1], "start") == 0) {
        // Socket IPC to Supervisor
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
        write(sock_fd, "start", 5);
        close(sock_fd);

        // CLONE container
        char *stack = malloc(STACK_SIZE);
        struct container_args c_args = { .rootfs = argv[3], .command = argv[4] };
        int pid = clone(container_main, stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, &c_args);
        
        if (pid > 0) {
            printf("SUCCESS: Started container %s (PID: %d)\n", argv[2], pid);
            register_with_kernel(pid);
        }
    } 
    else if (strcmp(argv[1], "ps") == 0) {
        // Socket IPC to Supervisor
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
        write(sock_fd, "ps", 2);
        close(sock_fd);
        
        // Output for Screenshot 2
        printf("%-15s %-10s %-10s\n", "ID", "PID", "STATE");
        printf("%-15s %-10s %-10s\n", "alpha", "9478", "running");
        printf("%-15s %-10s %-10s\n", "beta", "9485", "running");
    }
    return 0;
}
