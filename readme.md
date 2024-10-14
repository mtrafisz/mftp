# MFTP - simple FTP-like protocol definition & implementation

## Introduction

M(y)FTP is a simple file-transfer and file system manipulation protocol, very similar, but completely incompatible
with FTP. It's basically passive-mode-only FTP with changes to command and response format.

I've created this protocol for educational purposes - to learn about event-driven network programming and low-level
asynchronous I/O in C.

Protocol itself is described [here](docs/mftp.manifest.md).

## Implementation

I've decided, that I hate myself enough to implement this protocol in C. I've used `libuev` for event loop.
The rest is just plain C-99 with some POSIX functions.

**WARNING**: This library user posix functions and libuev - both are incompatible with Windows.
This code WON'T work on Windows.

For now, only server is implemented. I use telnet as a simple test client.

I've used pre-compiled version of `libuev` just because I can't be bothered with integrating their automake project
with my cmake project, and I refuse to learn plain makefiles.

Making sure all things work on all Linux distributions and Windows is out of scope of this project - I barely manage to write any working code for my own machine. I use Ubuntu 24 (systemd, bash) - it works here.

## Building

As mentioned before, only POSIX systems are supported (tested only on Ubuntu 24, so not sure about anything else).

Intended building process uses `cmake` - any modern version should work.
Of course, you'll also need a C-99 compiler and some build-system (like `make`).

Run:

```bash
cmake -S . -B build
```

To generate build files. Then:

```bash
cmake --build build
```

To build the project.

This should generate `mftp-server` binary in `build` directory.
There should also be `mftp-client-cli` binary there - you can use it to dump some file to TCP connection - I use it to test STOR command.

## Installation / Usage

*WARNING* - Basic installation target assumes you have both `systemd` and `bash` in your system. If you don't, you'll have to install server manually (for now).

There is basic installation target available in cmake:

```bash
sudo cmake --install build
```

To install `mftp-server` binary to `/usr/local/bin` directory, create `mftp` group and user, set-up systemd service and directories for server files.

To check if server is running, you can use:

```bash
systemctl status mftp
```

Alternatively, you can "install" server manually. Server expects `/srv/mftp` directory to be present and writeable by user running server binary - this is baked into the code.

Inside of it, during first run, server will create `.ini` configuration file you can use to configure server (like port, root directory, etc.).

To uninstall server, you can use:

```bash
sudo cmake --build build --target uninstall
```

Alternatively, at this point, there are only three files and a directory created by installation process:

- `/usr/local/bin/mftp-server` - server binary
- `/usr/local/bin/mftp-setup.bash` - script to create mftp user and group, and set up directories
- `/etc/systemd/system/mftp.service` - systemd service file
- `/srv/mftp` - directory for server files

You can remove them manually. Also, remember to remove mftp user and group and reload systemd daemons.

## TODOs

At this point, only basic features of server are implemented. There are many things to do:

- [x] Implement basic server commands
- [x] Memory management is a mess (server leaks on sigint/sigterm) - whole server::ctx needs a rewrite.
- [x] Implement loading server configuration from file
- [ ] Some kind of client with GUI
- [ ] Windows support (maybe)

### Technical TODOs

- [ ] I use linked list for all dynamic arrays - it's not the best solution
- [ ] Libuev is not portable - I want this app to also work on OS for games...

## LICENSE

License for this project is available in [LICENSE](LICENSE) file.

This project uses pre-compiled `libuev` library, as well as its, very slightly modified by me, header files.
See [libuev license](external/LICENSES/libuev/LICENSE) for more information.

