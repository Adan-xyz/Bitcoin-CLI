#!/usr/bin/env bash
# Clean-room test: build, 2-node sync, send, restart/persistence. Usage: bash test.sh
set -u
cd "$(dirname "$0")"
PASS=0; FAIL=0
ok(){ echo "PASS: $1"; PASS=$((PASS+1)); }
bad(){ echo "FAIL: $1"; FAIL=$((FAIL+1)); }
T=$(mktemp -d 2>/dev/null || mktemp -d -t btccli)
pkill -f "bitcoin -easy" 2>/dev/null; sleep 1
cleanup(){ exec 3>&- 4>&- 2>/dev/null; pkill -f "bitcoin -easy" 2>/dev/null; }
trap cleanup EXIT

rm -rf t1 t2 obj bitcoin
if make -j4 ${MAKEARGS:-} >"$T/build.log" 2>&1 && [ -x ./bitcoin ]; then ok "clean build"; else bad "clean build"; tail -30 "$T/build.log"; exit 1; fi

start(){ # n fd args...
  local n=$1 fd=$2; shift 2
  [ -p "$T/$n.in" ] || mkfifo "$T/$n.in"
  : > "$T/$n.out"
  ./bitcoin -easy -stdin "$@" < "$T/$n.in" > "$T/$n.out" 2>&1 &
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

addr=$(cmd n2 getnewaddress | tail -1)
echo "$addr" | grep -Eq '^1[1-9A-HJ-NP-Za-km-z]{25,34}$' && ok "address $addr" || bad "address format: $addr"

sendout=$(cmd n1 send "$addr" 5)
echo "$sendout" | grep -q "^txid" && ok "send accepted" || { bad "send: $sendout"; }
cmd n1 gen on >/dev/null
waitfor 40 n2 5.00 && ok "n2 received 5.00" || bad "n2 balance $(bal n2) (expected 5.00)"
cmd n1 gen off >/dev/null; sleep 3
h1=$(blocks n1); hb=$(blocks n2)

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
if pgrep -f "bitcoin -easy" >/dev/null; then bad "processes still running"; else ok "clean shutdown"; fi
if grep -qiE "exception|assert|segmentation|terminate" t1/debug.log t2/debug.log "$T"/n1.out "$T"/n2.out 2>/dev/null; then bad "errors in logs"; grep -iE "exception|assert|segmentation|terminate" t1/debug.log t2/debug.log | head -5; else ok "no exceptions in logs"; fi

echo "----"; echo "passed=$PASS failed=$FAIL"
[ $FAIL -eq 0 ]
