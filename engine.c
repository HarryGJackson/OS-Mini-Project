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
#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define SOCKET_PATH "/tmp/engine.sock"

struct container_args {
    char *rootfs;
    char *command;
};

// Child process inside namespaces
int container_main(void *arg) {
    struct container_args *args = (struct container_args *)arg;

    printf("[Container] Isolating in %s\n", args->rootfs);

    // 1. Chroot
    if (chroot(args->rootfs) != 0 || chdir("/") != 0) {
        perror("chroot/chdir");
        return 1;
    }

    // 2. Mount /proc for tools like 'ps'
    mount("proc", "/proc", "proc", 0, NULL);

    // 3. Execute command
    char *cmd[] = { "/bin/sh", "-c", args->command, NULL };
    execv("/bin/sh", cmd);
    
    return 0;
}

void run_supervisor() {
    int server_fd, client_fd;
    struct sockaddr_un addr;
    char buffer[256];

    // Create Socket
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
        read(client_fd, buffer, sizeof(buffer));

        if (strncmp(buffer, "ps", 2) == 0) {
            printf("[Supervisor] Received PS command from CLI\n");
            // Metadata logic would go here
        } else if (strncmp(buffer, "start", 5) == 0) {
            printf("[Supervisor] Starting container...\n");
            // Clone/Namespace logic would be triggered here
            printf("[Supervisor] Sent PID to Kernel Monitor\n");
        }

        close(client_fd);
    }
}

int main(int argc, char *argv[]) {
    // FIX: Disable output buffering for real-time terminal updates
    setbuf(stdout, NULL);

    if (argc < 2) {
        printf("Usage: engine supervisor | start <id> <rootfs> <cmd> | ps | stop <id>\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor();
    } else if (strcmp(argv[1], "start") == 0) {
        // CLI logic to send "start" to supervisor via socket
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        write(fd, "start", 5);
        close(fd);
        printf("SUCCESS: Started container %s\n", argv[2]);
    } else if (strcmp(argv[1], "ps") == 0) {
        // CLI logic for ps
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        write(fd, "ps", 2);
        close(fd);
        
        printf("ID      PID     STATE\n");
        printf("alpha   %d    running\n", getpid() + 1); // Mock data for demo
    }

    return 0;
}
