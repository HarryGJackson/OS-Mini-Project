# 🚀 Multi-Container Runtime Project

![OS Project](https://img.shields.io/badge/Project-Operating%20Systems-blue)
![Language](https://img.shields.io/badge/Language-C-green)
![Platform](https://img.shields.io/badge/Platform-Linux-orange)

---

## 👨‍💻 Team Members

* **Aadarsh Reddy Yedhala** — PES1UG24CS706
* **Satvik J** — PES1UG24CS714

📘 **Course:** Operating Systems
📅 **Date:** April 2026

---

## 📌 1. Introduction

This project implements a lightweight Linux container runtime in C. It bridges the gap between user-space management and kernel-space enforcement, providing a complete lifecycle for isolated environments.

### ✨ Key Features

* **Long-running Supervisor:** Manages container state and IPC
* **Kernel-space Monitor:** Custom LKM for real-time memory tracking
* **Multi-container Support:** Run multiple isolated environments simultaneously
* **Logging System:** Producer-consumer model using a bounded buffer
* **CLI IPC:** Unix Domain Sockets for low-latency communication
* **Memory Enforcement:** Dual-tier (Soft/Hard) limit enforcement via `SIGKILL`

---

## ⚙️ 2. Build and Run Instructions

### 🔧 2.1 Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

### 🛠️ 2.2 Build the Project

```bash
make clean
make all
```

### 📦 2.3 Prepare Alpine Root Filesystem

```bash
mkdir rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
sudo tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base
sudo cp -a ./rootfs-base ./rootfs-alpha
sudo cp -a ./rootfs-base ./rootfs-beta
```

### 🧠 2.4 Start Supervisor (Terminal 1)

```bash
sudo insmod monitor.ko
sudo chmod 666 /dev/container_monitor
sudo ./engine supervisor
```

### 💻 2.5 Run CLI Commands (Terminal 2)

```bash
sudo ./engine start alpha ./rootfs-alpha /bin/sleep 500
sudo ./engine start beta ./rootfs-beta /bin/sleep 500
sudo ./engine ps
```

### 🧹 2.6 Teardown

```bash
# Terminal 1: Ctrl + C
# Terminal 2:
sudo rmmod monitor
```

---

## 🧠 3. Engineering Analysis

### 🔐 3.1 Isolation Mechanisms

* **PID Namespace:** Process isolation (container sees itself as PID 1)
* **Mount Namespace:** Filesystem isolation via `chroot`
* **UTS Namespace:** Unique hostnames per container
* **Control Groups (Conceptual):** Replaced by custom LKM

### ⚙️ 3.2 Supervisor Design

The supervisor acts as the “init” process for the container ecosystem. It uses `clone()` to establish namespaces and ensures real-time logging with unbuffered output via `setvbuf`.

### 🔄 3.3 IPC & Synchronization

* **Unix Domain Sockets:** CLI ↔ Supervisor communication
* **IOCTL:** Passes container PIDs from user-space to kernel module

### 🧮 3.4 Memory Enforcement

* **Soft Limit:** Warning logged via `dmesg`
* **Hard Limit:** Kernel sends `SIGKILL` directly to offending process

---

## ⚖️ 4. Design Tradeoffs

| Component   | Choice       | Tradeoff                           |
| ----------- | ------------ | ---------------------------------- |
| Isolation   | Namespaces   | Lightweight but shares host kernel |
| Enforcement | LKM (Kernel) | Faster than user-space monitoring  |
| IPC         | Unix Sockets | Efficient but local-only           |
| Teardown    | Manual       | Requires `rmmod` cleanup           |

---

## 🎯 5. Conclusion

This project demonstrates core Operating Systems concepts including:

* **Process Isolation**
* **Inter-Process Communication**
* **Kernel-level Resource Management**

By implementing a custom Linux Kernel Module, the system achieves fine-grained control over process behavior, going beyond traditional user-space container management.

---

## 🔮 Future Improvements

* Add cgroups-based resource control
* Implement network namespaces
* Add container image management
* Build a web-based monitoring dashboard

