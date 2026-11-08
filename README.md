# Bitcoin 0.1.0 — CLI port

Replit: upload folder, press Run (`make && ./bitcoin -datadir=./data`).
Ubuntu/Debian: `sudo apt install g++ make pkg-config libssl-dev libdb++-dev`, then `make && ./bitcoin`.

Options: -datadir=DIR -gen -addnode=IP[:PORT] -port=N -proxy=IP:PORT -debug -printtoconsole -noprompt
Commands: help getinfo getbalance getnewaddress listaddresses send listtransactions gen addnode peers stop
Log: <datadir>/debug.log

Docker: `docker build -t bitcoin-cli .` then `docker run -it -v btc:/data bitcoin-cli -easy -gen`
Two-node test network: `docker compose up -d`, console: `docker attach node1`
Tests: `bash test.sh` (clean build + 2-node sync/send/restart). CI: .github/workflows/ci.yml (x86-64 + ARM64, OpenSSL 1.1/3).
