# MFTP - My(Mikołaj's) File Transfer Protocol

Simple, FTP like file transfer protocol definition and client/server implementation in C

Read more about protocol itself [here](mftp.manifest.md)

## Building

Intended buiding process involves `cmake`.

The implementation is unix-only for now - Windows is not supported.

```bash
cmake -S . -B build
cmake --build build
```

## Licensing

This project is licensed under [MIT license](LICENSE) 
