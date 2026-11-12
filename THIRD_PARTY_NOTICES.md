# Third-party components

Prebuilt packages bundle or statically link the components below. Licenses are listed to the best of our knowledge; check each package's own license text before redistributing. This is not legal advice.

| Component | Where | License |
|---|---|---|
| Bitcoin 0.1.0 source (Satoshi Nakamoto) | all builds | MIT (see `LICENSE`) |
| OpenSSL 3.x | statically linked on Linux and macOS; DLL on Windows | Apache-2.0 |
| Berkeley DB 5.3 | statically linked on Linux, `.dylib` on macOS | Sleepycat license (redistribution requires making the application's source available, which this repository does) |
| Berkeley DB 6.2 | Windows package (MSYS2 has no 5.3) | AGPL-3.0 (source of this application: https://github.com/Adan-xyz/Bitcoin-CLI) |
| libstdc++ / libgcc | statically linked on Linux | GPL-3.0 with the GCC Runtime Library Exception |
| MSYS2/Cygwin runtime (`msys-2.0.dll`) and companion DLLs | Windows package | LGPL-3.0 and the licenses of the individual MSYS2 packages; their sources are published by the MSYS2 project |

Because Berkeley DB 6.x is AGPL, anyone who runs a modified version of the Windows build as a network service must offer the corresponding source. This program serves nothing on the network (the optional command socket is a local Unix socket).
