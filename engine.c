#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024) 
#define SOCKET_PATH "/tmp/engine.sock"
#define MAX_CONTAINERS 10
#define BUFFER_SIZE 50

char log_buffer[BUFFER_SIZE][256];
int buffer_count = 0, buffer_head = 0, buffer_tail = 0;
pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_not_empty = PTHREAD_COND_INITIALIZER;

struct Container {
    char id[32];
    pid_t pid;
    char state[16];
    int stop_requested;
};
struct Container containers[MAX_CONTAINERS];
int container_count = 0;

struct CloneArgs {
    char rootfs[128];
    char exec_path[128]; // Task 5: Dynamic commands
    int write_pipe;
};

int container_main(void *arg) {
    struct CloneArgs *args = (struct CloneArgs *)arg;
    dup2(args->write_pipe, STDOUT_FILENO);
    dup2(args->write_pipe, STDERR_FILENO);
    close(args->write_pipe);

    if (chroot(args->rootfs) != 0) return 1;
    chdir("/"); 
    mount("proc", "/proc", "proc", 0, NULL);
    
    // Task 5: Execute the specific workload passed from CLI
    execl(args->exec_path, args->exec_path, NULL);
    
    // Task 6 Cleanup: If the process ever finishes, unmount /proc
    umount2("/proc", MNT_DETACH);
    return 0;
}

void *producer_thread(void *arg) {
    int read_pipe = *(int *)arg;
    char temp_buf[256];
    while (read(read_pipe, temp_buf, sizeof(temp_buf) - 1) > 0) {
        pthread_mutex_lock(&buffer_lock);
        while (buffer_count == BUFFER_SIZE) pthread_cond_wait(&cond_not_full, &buffer_lock);
        strncpy(log_buffer[buffer_tail], temp_buf, 255);
        buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
        buffer_count++;
        pthread_cond_signal(&cond_not_empty);
        pthread_mutex_unlock(&buffer_lock);
        memset(temp_buf, 0, sizeof(temp_buf));
    }
    return NULL;
}

void *consumer_thread(void *arg) {
    FILE *log_file = fopen("containers.log", "a");
    while (1) {
        pthread_mutex_lock(&buffer_lock);
        while (buffer_count == 0) pthread_cond_wait(&cond_not_empty, &buffer_lock);
        fprintf(log_file, "%s", log_buffer[buffer_head]);
        fflush(log_file);
        buffer_head = (buffer_head + 1) % BUFFER_SIZE;
        buffer_count--;
        pthread_cond_signal(&cond_not_full);
        pthread_mutex_unlock(&buffer_lock);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    if (strcmp(argv[1], "supervisor") == 0) {
        pthread_t c_thread;
        pthread_create(&c_thread, NULL, consumer_thread, NULL);
        int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        unlink(SOCKET_PATH); 
        bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 5);

        while (1) {
            int client_fd = accept(server_fd, NULL, NULL);
            char buffer[512] = {0};
            read(client_fd, buffer, sizeof(buffer));
            char cmd[32], id[32], rootfs[128], exec_path[128];
            sscanf(buffer, "%s %s %s %s", cmd, id, rootfs, exec_path);

            if (strcmp(cmd, "start") == 0) {
                int pipefd[2]; pipe(pipefd);
                struct CloneArgs *args = malloc(sizeof(struct CloneArgs));
                strcpy(args->rootfs, rootfs);
                strcpy(args->exec_path, exec_path);
                args->write_pipe = pipefd[1];
                
                pid_t pid = clone(container_main, malloc(STACK_SIZE) + STACK_SIZE, 
                                  CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD, args);
                
                close(pipefd[1]);
                pthread_t p_thread;
                pthread_create(&p_thread, NULL, producer_thread, &pipefd[0]);

                if (container_count < MAX_CONTAINERS) {
                    strcpy(containers[container_count].id, id);
                    containers[container_count].pid = pid;
                    strcpy(containers[container_count].state, "running");
                    containers[container_count].stop_requested = 0;
                    container_count++;
                }

                int dev_fd = open("/dev/container_monitor", O_WRONLY);
                if (dev_fd >= 0) {
                    struct monitor_cmd m_cmd = {pid, 40, 64};
                    ioctl(dev_fd, IOCTL_REGISTER_PID, &m_cmd);
                    close(dev_fd);
                }
                write(client_fd, "SUCCESS: Started.", 17);
            }
            else if (strcmp(cmd, "ps") == 0) {
                char resp[1024] = "ID\tPID\tSTATE\n";
                for (int i = 0; i < container_count; i++) {
                    char line[128];
                    sprintf(line, "%s\t%d\t%s\n", containers[i].id, containers[i].pid, containers[i].state);
                    strcat(resp, line);
                }
                write(client_fd, resp, strlen(resp));
            }
            else if (strcmp(cmd, "stop") == 0) {
                for (int i = 0; i < container_count; i++) {
                    if (strcmp(containers[i].id, id) == 0) {
                        containers[i].stop_requested = 1;
                        kill(containers[i].pid, SIGKILL);
                        strcpy(containers[i].state, "stopped");
                        
                        // Task 6: Cleanup Kernel Tracking
                        int dev_fd = open("/dev/container_monitor", O_WRONLY);
                        if (dev_fd >= 0) {
                            ioctl(dev_fd, IOCTL_UNREGISTER_PID, &containers[i].pid);
                            close(dev_fd);
                        }
                        write(client_fd, "SUCCESS: Stopped and Cleaned.", 29);
                    }
                }
            }
            close(client_fd);
        }
    } else {
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
        char command[512] = {0};
        for (int i = 1; i < argc; i++) { strcat(command, argv[i]); strcat(command, " "); }
        write(sock_fd, command, strlen(command));
        char resp[1024] = {0};
        read(sock_fd, resp, sizeof(resp));
        printf("%s\n", resp);
    }
    return 0;
}
