# Changelog

## v0.1.0-cli.4
- Security: the data directory is created with mode 0700 and all files with 0600 (before, `wallet.dat` was readable by other local users on shared systems); an existing data directory owned by the user is tightened to 0700 on start.
- `-version` flag; `getinfo` and `getnetworkinfo` show the build id (release tag).
- `test.sh` no longer kills unrelated `bitcoin -easy` processes; it only touches its own `t1`, `t2`, `t3` directories. 11 more checks (59 in total).
- Added `LICENSE`, `THIRD_PARTY_NOTICES.md` and this changelog; release packages now include them.
- CI: least-privilege token permissions, the Docker job also runs the final image, the Docker image runs as a non-root user.
- Makefile no longer adds `-L/lib -Wl,-rpath,/lib` to default builds.

## v0.1.0-cli.3
- Windows x86-64 package (MSYS2/Cygwin runtime, BerkeleyDB 6.2).

## v0.1.0-cli.2
- Daemon mode (`-daemon`, `-server`) and local client over a Unix socket.
- About 25 more commands (wallet, chain, network); 48 test checks; ASan/UBSan and warning jobs in CI; fixes for a 64-bit `ParseString` loop, a signed shift, a negative hex index and thread exit.

## v0.1.0-cli.1
- First release: port of Bitcoin 0.1.0 to a console program; Linux x86-64/ARM64 and macOS (Apple Silicon) packages.
