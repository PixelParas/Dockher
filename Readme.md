# Dockher — A Mini Docker Clone in C++

Dockher is a minimalist container runtime built in C++ using Linux namespaces and cgroups. It allows you to isolate processes in a chroot environment and apply resource limits via cgroup v2.

## 📅 Prerequisites

Before you use Dockher, ensure the following:

* You're on a **Linux system with cgroup v2 enabled** (Ubuntu 20.04+ recommended)
* You have **`debootstrap`** installed for bootstrapping minimal Ubuntu
* You have **root/sudo privileges**

Install required packages:

```bash
sudo apt update
sudo apt install debootstrap build-essential g++
```

## 💡 Bootstrapping the Container Image

Use `debootstrap` to create a minimal Ubuntu root filesystem:

```bash
sudo debootstrap --arch=amd64 focal ./images/ubuntu http://archive.ubuntu.com/ubuntu/
```

Make sure the image includes a shell (e.g., `/bin/bash`). You can enter the chroot for debugging:

```bash
sudo chroot ./images/ubuntu /bin/bash
```

If `apt` is missing, you may need to:

```bash
sudo cp /etc/resolv.conf ./images/ubuntu/etc/resolv.conf
sudo chroot ./images/ubuntu
apt update
apt install bash coreutils
```

## 📂 Building Dockher

```bash
g++ -o dockher dockher.cpp -lstdc++ -std=c++17
```

Ensure `cxxopts.hpp` is present in the `include/` directory.

## 🔄 Usage

Run a containerized command with memory and CPU limits:

```bash
sudo ./dockher --cmd "stress --vm 1 --vm-bytes 300M" --mem 200 --cpu 50
```

### Arguments:

* `--cmd` (or `-c`) : Command to run inside the container (wrapped with `/bin/sh -c`)
* `--mem` (or `-m`) : Memory limit in MB (e.g., 200)
* `--cpu` (or `-p`) : CPU usage limit in percent (0-100)

## 🏦 What Dockher Does

* Uses **`chroot`** to isolate filesystem to `./images/ubuntu`
* Creates a **new PID and mount namespace** for isolation
* Uses **cgroup v2** to limit memory and CPU:

  * Writes to `memory.max`, `cpu.max`, and `cgroup.procs`
* Executes command via `execvp("sh -c <cmd>")`

## 🛠️ Internals

* Stack is manually allocated and passed to `clone()`
* Namespace flags: `CLONE_NEWPID | CLONE_NEWNS`
* Cgroups are created at: `/sys/fs/cgroup/dockher_<pid>`
* Cleans up the cgroup directory and frees stack memory

## 🚧 Limitations

* Not a secure sandbox — no seccomp, user namespaces, or capabilities
* No overlay filesystem or image layering
* Requires `sudo`

## ✅ Future Improvements

* Auto-create container image if missing
* Network isolation (CLONE\_NEWNET)
* Logging support (via dup2 or threads)

---

Feel free to fork, experiment, and build your own lightweight container tool!

> Built with <3 as a mini-docker project
