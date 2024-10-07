# MFTP - simple FTP-like protocol definition & implementation

## Introduction

M(y)FTP is a simple file-transfer and file system manipulation protocol, very similar, but completely incompatible
with FTP. It's basically passive-mode-only FTP with changes to command and response format.

I've created this protocol for educational purposes - to learn about event-driven network programming and low-level
asynchronous I/O in C.

Protocol itself is described [here](mftp.manifest.md).

## Implementation

I've decided, that I hate myself enough to implement this protocol in C. I've used `libuev` for event loop.
The rest is just plain C-99 with some POSIX functions.

**WARNING**: This library user posix functions and libuev - both are incompatible with Windows.
This code WON'T work on Windows.

For now, only server is implemented. I use telnet as a simple test client.

I've used pre-compiled version of `libuev` just because I can't be bothered with integrating their automake project
with my cmake project, and I refuse to learn plain makefiles.

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

## TODOs

At this point, only basic features of server are implemented. There are many things to do:

- [x] Implement basic server commands
- [x] Memory management is a mess (server leaks on sigint/sigterm) - whole server::ctx needs a rewrite.
- [ ] Implement loading server configuration from file
- [ ] Some kind of client with GUI
- [ ] Windows support (maybe)

## LICENSE

License for this project is available in [LICENSE](LICENSE) file.

This project uses pre-compiled `libuev` library, as well as its, very slightly modified by me, header files.
See [libuev license](external/LICENSES/libuev/LICENSE) for more information.

