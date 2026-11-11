# Deviations from the original Bitcoin 0.1.0

Audit of this port against the original source (23 source files compared). Summary: no change to block or transaction validation logic. Changes are portability fixes, default-behavior changes, removed GUI features, and opt-in additions.

## Unchanged files
`key.h`, `script.h`, `db.h`, `irc.cpp`, `sha.cpp`, `market.h`, `market.cpp`. (`uint256.h` changed only by two `(unsigned char)` casts in hex parsing, see section 1b.)
Validation in `main.cpp` (`CheckBlock`, `ConnectInputs`, ...) and script evaluation in `script.cpp` are untouched. `script.cpp` changed only 3 lines (`BN_mul`/`BN_div`/`BN_mod` calls adapted to the new `CBigNum`).

## 1. Portability changes (no intended behavior change)
- Win32 to POSIX through `src/compat.h`: threads (`_beginthread` to pthreads, 8 MB stacks), recursive mutexes for critical sections, sockets, `Sleep`, `QueryPerformanceCounter`.
- Format strings `%I64d` to `%lld`; path separators `\` to `/`.
- Boost removed: `foreach` is C++11 range-for, type traits are `std::`.
- `CBigNum` (`bignum.h`, ~115 lines changed) wraps a `BIGNUM*` instead of inheriting from `BIGNUM` (OpenSSL 1.1+ made it opaque).
- OpenSSL thread-locking callbacks and Windows perfmon seeding removed; `RandAddSeed` reads `/dev/urandom`.
- `bswap` inline assembly replaced by `__builtin_bswap32/64` (ARM support).
- Build flags `-fsigned-char` (matches MSVC `char`), `-fpermissive -w`.
- clang/libc++ fixes: `::bind` (clashes with `std::bind` under `using namespace std`), `(char)0xf9...` for `pchMessageStart`, `DbEnv((u_int32_t)0)`, `CDataStream::insert(iterator, const char*, const char*)` implemented directly (same special case, no iterator cast), `min<unsigned int>`.
- `catch (...)` no longer swallows glibc forced unwind (thread exit) as an exception.
- `*(long*)` host-address read changed to `unsigned int` (64-bit).

## 1b. Fixes found by the CI sanitizer and warning jobs
- `ParseString` (`util.cpp`, used by the IRC code): indices were `unsigned int` but compared with `npos` (64-bit), so the loop never ended on 64-bit builds. Now `size_t`.
- `CBigNum::setint64`: signed left shift (`n <<= 8`) overflowed, which is undefined behavior; now shifts as unsigned. Same result.
- Hex parsing (`uint256.h`, `bignum.h`): a `char` index into the digit table could be negative for non-ASCII input (out-of-bounds read); now cast to `unsigned char`.
- Thread exit: `_endthread()` now throws a `ThreadExit` exception (caught in the thread start routine) instead of calling `pthread_exit`. This unwinds the stack properly on every platform and removes a sanitizer report about glibc's forced-unwind exception.
- Reviewed and left as is: `-Winfinite-recursion` on the three `Thread*Handler` functions is a false positive of the original `IMPLEMENT_RANDOMIZE_STACK` trick (it recurses a bounded, random number of times); ~210 `-Wdeprecated-declarations` come from OpenSSL 3 deprecating the `EC_KEY`/`BN` APIs the code relies on (a reason to replace them in a modern version).

## 2. Changed default behavior
- Data directory: `~/.bitcoin` (original: `%APPDATA%\Bitcoin`). Log goes to `<datadir>/debug.log`; every `printf` is redirected there.
- Network: external-IP lookup and IRC seeding are disabled unless `-irc` (their services no longer exist). Peers are added with `addnode`.
- Sockets are set non-blocking (accepted and outgoing); listening socket binds all interfaces with `SO_REUSEADDR` (original: detected local IP); falls back to 127.0.0.1 if the hostname does not resolve.
- Self-connection guard compares IP and port (original: IP only), so two nodes on one host can connect.
- Miner thread priority (`THREAD_PRIORITY_LOWEST`) is a no-op; the miner runs at normal priority.
- `DB_PRIVATE` removed from the BerkeleyDB environment flags (fails with `DB_RECOVER` on BDB 5.x).
- Genesis debug prints and the debug-only merkle-root assertion removed (`assert(block.GetHash() == hashGenesisBlock)` remains).
- Single-instance protection uses a `flock` on `<datadir>/.lock`.

## 3. Removed GUI features
Direct IP-to-IP payment dialog, Options dialog (fee, proxy, start-up settings), product/market UI (market code still compiles but is unreachable), visual address book and wxWidgets message boxes. The transaction fee setting (`nTransactionFee`) is restored as the `setfee` command; the GUI "message" field is restored as the optional comment of `send`.

## 4. Additions (opt-in)
- `-easy`: separate test network (own genesis, fixed low difficulty). The mainline genesis hash is unchanged (`000000000019d6689c08...ce26f`). A data directory refuses to load under the other mode.
- `-solo`: lets the miner run without peers (the original miner waits for at least one peer). In `-easy` mode this is implied.
- `-stdin`, hashrate and estimated block time in `getinfo`.
- `-daemon` / `-server` and client mode (`bitcoin <command>`): the node serves the same commands over a Unix domain socket in the data directory (mode 0600, same user only, no network port, no password). Commands are tokenized on whitespace, exactly like the prompt.
- CLI commands: `getinfo`, `getbalance`, `getnewaddress`, `listaddresses`, `send`, `listtransactions`, `gen`, `addnode`, `peers`/`getpeerinfo`, `getblockcount`, `getblockhash`, `getblock`, `gettransaction`, `getdifficulty`, `validateaddress`, `setlabel`, `backupwallet`, `setfee`, `listunspent`, `getreceivedbyaddress`, `listreceivedbyaddress`, `dumpprivkey`, `importprivkey`, `rescan`, `rebroadcast`, `getbestblockhash`, `getchaintips`, `getrawmempool`, `getmempoolinfo`, `getrawtransaction`, `generate`, `getconnectioncount`, `getnetworkinfo`, `listknownpeers`, `disconnectnode`, `uptime`.
- `dumpprivkey` prints the raw DER private key as hex (0.1 has no WIF format). Private keys are stored unencrypted in `wallet.dat`, as in the original.
- `rescan` re-adds wallet transactions found in the chain and refreshes the wallet's per-transaction spent flag; rescanned transactions get the rescan time as their received time.

## 5. Not verified to match the original
1. **Signature verification:** OpenSSL 1.1+ rejects non-canonical DER signatures that OpenSSL 0.9.8 accepted. Old blocks with such signatures could validate differently. Tests only use signatures created by this code.
2. **Script arithmetic through `CBigNum`:** only the standard pay-to-pubkey-hash path is tested; edge cases of `OP_MUL`, `OP_DIV`, `OP_MOD` (negative numbers) are not.
3. **64-bit and data formats:** `long` is 8 bytes here (4 in the original); `wallet.dat` and `blkindex.dat` are written by BerkeleyDB 5.3 (original: 4.7) and may not be interchangeable with the Windows build. The genesis hash matches, which covers basic serialization, but no real historical blocks were available to compare.
4. **All compiler warnings are disabled** (`-w -fpermissive`), so 64-bit conversion issues could be hidden.
5. **Windows build:** it runs on the MSYS2/Cygwin POSIX runtime (not Winsock) and links BerkeleyDB 6.2 (MSYS2 has no 5.3). BerkeleyDB 6.x is under the AGPL-3.0 and its files are not guaranteed to be readable by the 5.3 builds, so do not move data directories between the Windows build and the others.

## 6. Known flaws of 0.1.0 that remain
The port is faithful to the original, including weaknesses fixed in later releases (for example an output value overflow, an `OP_RETURN` bug and an `OP_LSHIFT` crash). Do not use it to hold real value or connect it to a live network.

## Suggestions for a modernized version
- Treat `uint256.h`, `script.*`, `key.h` and the validation code in `main.cpp` as the consensus boundary and refactor outside it first.
- Write reference tests before refactoring: genesis hash, block/transaction serialization vectors, and the `-easy` flow in `test.sh`.
- Real early-chain block data (if obtained) would be the best regression test for items 1 to 3 above.
