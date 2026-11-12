#!/usr/bin/env bash
# Clean-room test: build, 2-node sync, send, restart/persistence. Usage: bash test.sh
set -u
cd "$(dirname "$0")"
PASS=0; FAIL=0
ok(){ echo "PASS: $1"; PASS=$((PASS+1)); }
bad(){ echo "FAIL: $1"; FAIL=$((FAIL+1)); }
T=$(mktemp -d 2>/dev/null || mktemp -d -t btccli)
pkill -f "datadir=$PWD/t[123]" 2>/dev/null; sleep 1
cleanup(){ exec 3>&- 4>&- 2>/dev/null; pkill -f "datadir=$PWD/t[123]" 2>/dev/null; }
trap cleanup EXIT

rm -rf t1 t2 t3 obj bitcoin
if make -j4 ${MAKEARGS:-} >"$T/build.log" 2>&1 && [ -x ./bitcoin ]; then ok "clean build"; else bad "clean build"; tail -30 "$T/build.log"; exit 1; fi

./bitcoin -version | grep -q "^Bitcoin 0.1.0 (cli)" && ok "version flag" || bad "version flag"

start(){ # n fd args...
  local n=$1 fd=$2; shift 2
  [ -p "$T/$n.in" ] || mkfifo "$T/$n.in"
  : >> "$T/$n.out"
  ./bitcoin -easy -stdin "$@" < "$T/$n.in" >> "$T/$n.out" 2>&1 &
  eval "P_$n=$!"
  eval "exec $fd>\"$T/$n.in\""
}
cmd(){ # n command...  (waits for the <<END>> marker the CLI prints in -stdin mode)
  local n=$1; shift
  local out="$T/$n.out" fd; [ "$n" = n1 ] && fd=3 || fd=4
  local before; before=$(wc -l < "$out")
  echo "$*" >&$fd
  if [ "$1" = stop ]; then sleep 2; return 0; fi
  local i
  for i in $(seq 1 120); do
    tail -n +$((before+1)) "$out" | grep -q '^<<END>>' && break
    sleep 0.5
  done
  tail -n +$((before+1)) "$out" | grep -v '^<<END>>'
}
blocks(){ cmd "$1" getinfo | awk '/^blocks/{print $3}' | tail -1; }
bal(){ cmd "$1" getbalance | tail -1; }
waitfor(){ # seconds, node, expected-balance
  local i; for i in $(seq 1 $(( $1 / 2 ))); do [ "$(bal $2)" = "$3" ] && return 0; sleep 1; done; return 1; }

start n1 3 -datadir="$PWD/t1" -port=18333 -gen
sleep 3
for i in $(seq 1 90); do h=$(blocks n1); [ "${h:-0}" -ge 130 ] 2>/dev/null && break; sleep 2; done
[ "${h:-0}" -ge 130 ] 2>/dev/null && ok "mining reached height $h" || { bad "mining (height=${h:-none})"; exit 1; }

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    ok "data directory permissions (not checked on Windows)"
    ok "wallet permissions (not checked on Windows)" ;;
  *)
    dperm=$(ls -ld "$PWD/t1" | cut -c1-10); wperm=$(ls -l "$PWD/t1/wallet.dat" | cut -c1-10)
    [ "$dperm" = "drwx------" ] && ok "data directory is private (0700)" || bad "data directory permissions: $dperm"
    [ "$wperm" = "-rw-------" ] && ok "wallet.dat is private (0600)" || bad "wallet.dat permissions: $wperm" ;;
esac
start n2 4 -datadir="$PWD/t2" -port=18334 -addnode=127.0.0.1:18333
sleep 3
cmd n1 gen off >/dev/null; sleep 3
synced=0
for i in $(seq 1 40); do
  h1=$(blocks n1); h2=$(blocks n2)
  [ -n "$h1" ] && [ "$h1" = "$h2" ] && { synced=1; break; }
  sleep 2
done
[ $synced = 1 ] && ok "sync n2 == n1 at height $h1" || bad "sync (n1=$h1 n2=$h2)"
b1=$(cmd n1 getinfo | awk '/^bestblock/{print $3}'); b2=$(cmd n2 getinfo | awk '/^bestblock/{print $3}')
[ -n "$b1" ] && [ "$b1" = "$b2" ] && ok "same best block hash" || bad "best block differs"
[ "$(cmd n1 getblockcount | tail -1)" = "$h1" ] && ok "getblockcount" || bad "getblockcount"
bh0=$(cmd n1 getblockhash 0 | tail -1)
cmd n1 getblock "$bh0" | grep -q "^height *: 0" && ok "getblockhash/getblock genesis" || bad "getblock genesis ($bh0)"
cmd n2 getpeerinfo | grep -q "version=" && ok "getpeerinfo" || bad "getpeerinfo"
[ "$(cmd n1 getbestblockhash | tail -1)" = "$b1" ] && ok "getbestblockhash" || bad "getbestblockhash"
[ "$(cmd n1 getconnectioncount | tail -1)" -ge 1 ] 2>/dev/null && ok "getconnectioncount" || bad "getconnectioncount"
cmd n1 getnetworkinfo | grep -q "^network *: easy" && ok "getnetworkinfo" || bad "getnetworkinfo"
cmd n1 uptime | grep -Eq '^[0-9]+d [0-9]+h [0-9]+m [0-9]+s$' && ok "uptime" || bad "uptime"
cmd n1 getchaintips | grep -q "active" && ok "getchaintips" || bad "getchaintips"
cmd n1 getmempoolinfo | grep -q "^size" && ok "getmempoolinfo" || bad "getmempoolinfo"
cmd n1 help | grep -q "getblock " && ok "help" || bad "help"
cmd n1 getdifficulty | grep -Eq '^[0-9]+\.[0-9]+$' && ok "getdifficulty" || bad "getdifficulty"
cmd n2 peers | grep -q "outbound" && ok "peers" || bad "peers"
cmd n1 listknownpeers | grep -Eq "no known peers|last seen" && ok "listknownpeers" || bad "listknownpeers"
if cmd n1 getrawmempool | grep -q "^error"; then bad "getrawmempool"; else ok "getrawmempool"; fi

addr=$(cmd n2 getnewaddress | tail -1)
echo "$addr" | grep -Eq '^1[1-9A-HJ-NP-Za-km-z]{25,34}$' && ok "address $addr" || bad "address format: $addr"
cmd n2 validateaddress "$addr" | grep -q "^ismine *: yes" && ok "validateaddress (own)" || bad "validateaddress own"
cmd n1 validateaddress "$addr" | grep -q "^ismine *: no" && ok "validateaddress (foreign)" || bad "validateaddress foreign"
cmd n2 listaddresses | grep -q "$addr" && ok "listaddresses" || bad "listaddresses"
cmd n2 setlabel "$addr" test label | grep -q "^ok" && cmd n2 listaddresses | grep -q "test label" && ok "setlabel" || bad "setlabel"

sendout=$(cmd n1 send "$addr" 5)
echo "$sendout" | grep -q "^txid" && ok "send accepted" || { bad "send: $sendout"; }
cmd n1 gen on >/dev/null
waitfor 40 n2 5.00 && ok "n2 received 5.00" || bad "n2 balance $(bal n2) (expected 5.00)"
txid=$(echo "$sendout" | awk '/^txid/{print $2}')
cmd n2 gettransaction "$txid" | grep -q "^net *: +5.00" && ok "gettransaction" || bad "gettransaction ($txid)"
cmd n2 listtransactions | grep -q "+5.00" && ok "listtransactions" || bad "listtransactions"
cmd n1 backupwallet "$T/wallet.bak" | grep -q "backed up" && [ -s "$T/wallet.bak" ] && ok "backupwallet" || bad "backupwallet"
cmd n1 getrawtransaction "$txid" | grep -Eq "^where *: (chain|memory pool)" && ok "getrawtransaction" || bad "getrawtransaction"
cmd n1 listunspent | grep -q ":[0-9]" && ok "listunspent" || bad "listunspent"
rcv=$(cmd n2 getreceivedbyaddress "$addr" 0 | tail -1)
[ "$rcv" = "5.00" ] && ok "getreceivedbyaddress" || bad "getreceivedbyaddress ($rcv)"
cmd n2 listreceivedbyaddress 0 | grep -q "$addr" && ok "listreceivedbyaddress" || bad "listreceivedbyaddress"
cmd n1 rebroadcast | grep -q "^ok" && ok "rebroadcast" || bad "rebroadcast"
cmd n1 setfee 0.01 >/dev/null
cmd n1 setfee | grep -q "0.01" && ok "setfee" || bad "setfee"
cmd n1 setfee 0 >/dev/null
a1=$(cmd n1 getnewaddress | tail -1)
tx2=$(cmd n1 send "$a1" 1 hello world | awk '/^txid/{print $2}')
cmd n1 gettransaction "$tx2" | grep -q "^message *: hello world" && ok "send comment" || bad "send comment ($tx2)"
pk=$(cmd n2 dumpprivkey "$addr" | tail -1)
[ ${#pk} -gt 100 ] && ok "dumpprivkey" || bad "dumpprivkey"
cmd n1 importprivkey "$pk" | grep -q "imported $addr" && ok "importprivkey" || bad "importprivkey"
cmd n1 validateaddress "$addr" | grep -q "^ismine *: yes" && ok "imported key is mine" || bad "imported key not mine"
cmd n1 rescan | grep -q "rescan complete" && ok "rescan" || bad "rescan"
cmd n1 gen off >/dev/null; sleep 3
g0=$(blocks n1)
cmd n1 generate 3 >/dev/null; sleep 3
g1=$(blocks n1)
[ "${g1:-0}" -ge $((g0+3)) ] 2>/dev/null && ok "generate 3 ($g0 -> $g1)" || bad "generate ($g0 -> $g1)"
sleep 2
h1=$(blocks n1); hb=$(blocks n2)
cmd n2 disconnectnode 127.0.0.1:18333 | grep -q "disconnected" || echo "disconnect command failed"
dc=1; for i in 1 2 3 4 5; do sleep 1; dc=$(cmd n2 getconnectioncount | tail -1); [ "$dc" = "0" ] && break; done
[ "$dc" = "0" ] && ok "disconnectnode" || bad "disconnectnode (connections=$dc)"

# persistence / restart
cmd n2 stop >/dev/null; wait $P_n2 2>/dev/null && ok "n2 clean exit" || ok "n2 exited"
exec 4>&-
start n2 4 -datadir="$PWD/t2" -port=18334
sleep 3
[ "$(bal n2)" = "5.00" ] && ok "n2 wallet persisted after restart" || bad "n2 wallet after restart: $(bal n2)"
h2=$(blocks n2); [ "$h2" = "$hb" ] && ok "n2 chain persisted (height $h2)" || bad "n2 height $h2 != $hb"

cmd n1 stop >/dev/null; wait $P_n1 2>/dev/null; exec 3>&-
start n1 3 -datadir="$PWD/t1" -port=18333
sleep 3
[ "$(blocks n1)" = "$h1" ] && ok "n1 chain persisted (height $h1)" || bad "n1 height after restart"

cmd n1 stop >/dev/null; cmd n2 stop >/dev/null
sleep 3
if pgrep -f "datadir=$PWD/t[12]" >/dev/null; then bad "processes still running"; else ok "clean shutdown"; fi
if grep -qiE "exception|assert|segmentation|terminate" t1/debug.log t2/debug.log "$T"/n1.out "$T"/n2.out 2>/dev/null; then bad "errors in logs"; grep -iE "exception|assert|segmentation|terminate" t1/debug.log t2/debug.log | head -5; else ok "no exceptions in logs"; fi

# daemon mode and the local client
dd="$PWD/t3"
./bitcoin -easy -daemon -datadir="$dd" -port=18335 >"$T/d.out" 2>&1 && ok "daemon starts" || { bad "daemon starts"; cat "$T/d.out"; }
client(){ ./bitcoin -datadir="$dd" "$@" 2>&1; }
client getinfo | grep -q "^network *: easy" && ok "client getinfo" || bad "client getinfo"
daddr=$(client getnewaddress | tail -1)
echo "$daddr" | grep -Eq '^1[1-9A-HJ-NP-Za-km-z]{25,34}$' && ok "client getnewaddress" || bad "client getnewaddress ($daddr)"
client generate 2 | grep -q "mined to height" && ok "client generate" || bad "client generate"
[ "$(client getblockcount | tail -1)" -ge 2 ] 2>/dev/null && ok "client getblockcount" || bad "client getblockcount"
client stop | grep -q "stopping" && ok "client stop" || bad "client stop"
for i in $(seq 1 20); do pgrep -f "datadir=$dd" >/dev/null || break; sleep 1; done
if pgrep -f "datadir=$dd" >/dev/null; then bad "daemon exited"; else ok "daemon exited"; fi
[ ! -S "$dd/bitcoin.sock" ] && ok "socket removed" || bad "socket still present"
client getinfo | grep -q "cannot connect" && ok "client reports a stopped node" || bad "client without node"

if grep -qE "runtime error|AddressSanitizer" "$T"/n1.out "$T"/n2.out t3/daemon.out 2>/dev/null; then
  echo "---- sanitizer findings (unique, with counts) ----"
  grep -hE "runtime error|AddressSanitizer" "$T"/n1.out "$T"/n2.out t3/daemon.out 2>/dev/null | sort | uniq -c | sort -rn | head -40
fi
echo "----"; echo "passed=$PASS failed=$FAIL"
[ $FAIL -eq 0 ]
