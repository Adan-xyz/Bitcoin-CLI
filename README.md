# Bitcoin 0.1.0 — CLI port

The original Bitcoin 0.1.0 source (Satoshi Nakamoto, 2009) ported to a console program that builds on Linux, macOS and Android (Termux). The wxWidgets GUI is replaced by a small command prompt, and the Win32 calls are replaced by a POSIX compatibility layer (`src/compat.h`). Original code: MIT/X11 license (see source headers).

## Tested platforms

Every push runs `test.sh` (clean build, 2-node sync, send, restart) in CI.

| Platform | Toolchain | Status |
|---|---|---|
| Linux x86-64 (Ubuntu 22.04, 24.04) | GCC, OpenSSL 3, BerkeleyDB 5.3 | passing |
| Linux ARM64 (Ubuntu 24.04) | GCC, OpenSSL 3, BerkeleyDB 5.3 | passing |
| macOS (Apple Silicon) | clang, OpenSSL 3, BerkeleyDB 5.3 | passing |
| Docker (Debian 12) | GCC, OpenSSL 3, BerkeleyDB 5.3 | passing |
| Android (Termux) | clang/libc++, OpenSSL 3, BerkeleyDB 5.3 | passing (manual) |
| Replit | GCC, OpenSSL 1.1.1, BerkeleyDB 5.3 | passing (manual) |
| Windows | use WSL2 or Docker (see below) | not native |

## Download (prebuilt binaries)

Each tagged release on the GitHub **Releases** page has a tarball per platform, built and tested by CI (`.github/workflows/release.yml`): `linux-x86_64`, `linux-arm64` (glibc 2.35 or newer, e.g. Ubuntu 22.04+, Debian 12+) and `macos-arm64` (Apple Silicon, macOS 11+). OpenSSL and BerkeleyDB are linked statically, so nothing needs to be installed.

~~~sh
sha256sum -c bitcoin-cli-*.tar.gz.sha256     # macOS: shasum -a 256 -c
tar xzf bitcoin-cli-*.tar.gz && cd bitcoin-cli-*/
./bitcoin -easy -gen
~~~

Maintainers: push a tag such as `v0.1.0-cli.1` to publish a release (`git tag v0.1.0-cli.1 && git push origin v0.1.0-cli.1`). Running the workflow by hand only builds and tests, it publishes nothing.

## Build

### Debian / Ubuntu
~~~sh
sudo apt install g++ make pkg-config libssl-dev libdb++-dev
make
~~~

### macOS (Homebrew)
~~~sh
brew install openssl@3 berkeley-db@5 pkg-config
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig"
make DB_INC="$(brew --prefix berkeley-db@5)/include" DB_LIBDIR="$(brew --prefix berkeley-db@5)/lib"
~~~

### Android (Termux)
~~~sh
pkg install git clang make pkg-config openssl libdb procps
make CXX=clang++
~~~

### Windows
Install WSL2 (Ubuntu) and follow the Debian/Ubuntu steps inside it, or use Docker Desktop (below). Native Windows builds are not supported.

### Docker
~~~sh
docker build -t bitcoin-cli .                      # add --build-arg RUN_TESTS=1 to run the tests
docker run -it -v btc:/data bitcoin-cli -easy -gen
docker compose up -d                               # two-node test network
docker attach node1                                # detach: Ctrl-P Ctrl-Q
~~~

### Replit
Upload the folder and press Run (`replit.nix` provides the dependencies).

If BerkeleyDB or OpenSSL live in a non-standard place, override the paths:
`make DB_INC=/path/include DB_LIBDIR=/path/lib`

## Run

~~~sh
./bitcoin                      # data in ~/.bitcoin
./bitcoin -datadir=./data -gen
~~~

Options: `-datadir=DIR` `-gen` `-addnode=IP[:PORT]` `-port=N` `-proxy=IP:PORT` `-easy` `-solo` `-debug` `-printtoconsole` `-irc` `-noprompt` `-stdin` `-printblocktree`

Prompt commands:

| Command | Action |
|---|---|
| `help` | list commands |
| `getinfo` | node status (blocks, peers, balance) |
| `getbalance` | wallet balance |
| `getnewaddress [label]` | new receiving address |
| `listaddresses` | wallet addresses |
| `send <address> <amount> [comment]` | send coins (the optional comment is stored with the transaction) |
| `listtransactions [n]` | recent wallet transactions |
| `gen [on\|off]` | start/stop mining |
| `addnode <ip[:port]>` | connect to a peer |
| `peers` / `getpeerinfo` | list connected peers (address, direction, version) |
| `getblockcount` | height of the best chain |
| `getblockhash <height>` | hash of the block at a height |
| `getblock <height\|hash>` | block details and its transactions |
| `gettransaction <txid>` | wallet transaction details |
| `getdifficulty` | current difficulty |
| `validateaddress <addr>` | check an address and whether it is yours |
| `setlabel <addr> <label>` | name an address |
| `backupwallet <path>` | copy `wallet.dat` to a file or directory |
| `setfee [amount]` | show or set the transaction fee |
| `listunspent` | unspent wallet outputs |
| `getreceivedbyaddress <addr> [minconf]` | total received by an address |
| `listreceivedbyaddress [minconf]` | totals for every wallet address |
| `dumpprivkey <addr>` | print a wallet private key (hex DER, not WIF) |
| `importprivkey <hex>` | import a private key (follow with `rescan`) |
| `rescan` | rescan the chain for wallet transactions |
| `rebroadcast` | re-send wallet transactions to peers |
| `getbestblockhash` | hash of the best block |
| `getchaintips` | active tip and side branches |
| `getrawmempool` / `getmempoolinfo` | memory pool contents / size |
| `getrawtransaction <txid>` | any transaction from the memory pool or chain |
| `generate <n>` | mine n blocks (only with `-easy`) |
| `getconnectioncount` | number of peers |
| `getnetworkinfo` | network settings |
| `listknownpeers` | addresses learned from peers |
| `disconnectnode <ip[:port]>` | drop a peer |
| `uptime` | time since start |
| `stop` | shut down (also `quit`, `exit`, Ctrl-C) |

Logs go to `<datadir>/debug.log` (add `-printtoconsole` to also print them).

## Local test network (`-easy`)

The original network no longer exists (the IRC seed and the original nodes are gone), so a normal start is a standalone node with the real genesis block. For testing, `-easy` uses its own genesis block and a fixed, very low difficulty (blocks in about a second):

~~~sh
./bitcoin -easy -datadir=./n1 -port=8333 -gen
./bitcoin -easy -datadir=./n2 -port=8334 -addnode=127.0.0.1:8333
~~~

Never mix an `-easy` data directory with a normal one; the node refuses to start with a "different network" error. Coinbase coins need 120 confirmations before they can be spent. `-easy` defaults to `~/.bitcoin-easy`.

## Tests

~~~sh
bash test.sh
~~~

Builds from scratch, mines, syncs two nodes, sends coins, restarts both and checks that the wallet and chain survive. It takes a few minutes and uses full CPU. Expected last line: `passed=39 failed=0`. If your libraries are in custom paths, set `MAKEARGS`, for example `MAKEARGS="DB_INC=... DB_LIBDIR=..." bash test.sh`.

## What changed from the original

- GUI (`ui.cpp`, `uibase.cpp`) replaced by `cli.cpp`; Boost removed (replaced by C++11 range-for and `std::` traits).
- `CBigNum` wraps an OpenSSL `BIGNUM*` (works with OpenSSL 1.1 and 3).
- Win32 API (threads, critical sections, sockets, `Sleep`) mapped to POSIX in `src/compat.h`.
- External-IP lookup and IRC seeding are off by default (their services are gone); `-irc` re-enables them.
- Miner runs without peers in `-easy` mode.
- Portable byte-swap and `-fsigned-char`, so it also builds on ARM; small fixes for clang/libc++.

See [DEVIATIONS.md](DEVIATIONS.md) for every difference from the original 0.1.0 source.
