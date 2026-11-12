// Console (CLI) front-end replacing the wxWidgets GUI of Bitcoin 0.1.0
#include "headers.h"
#include <sys/file.h>
#include <sys/un.h>
#include <sys/wait.h>
#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif
#undef printf
// all command output goes through g_out so that a command run for a socket client can be captured
static FILE* g_out = stdout;
#define printf_cli(...) fprintf(g_out, __VA_ARGS__)
#define puts(s) (fputs((s), g_out), fputc('\n', g_out))
#define printf(...) OutputDebugStringF(__VA_ARGS__)

map<string, string> mapAddressBook;
static volatile sig_atomic_t fQuitSignal = 0;
static bool fMinerStarted = false;

string DateTimeStr(int64 nTime)
{
    time_t t = (time_t)nTime;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

static void OnSignal(int) { fQuitSignal = 1; }

static void Shutdown()
{
    static bool fDone = false;
    if (fDone) return;
    fDone = true;
    fShutdown = true;
    nTransactionsUpdated++;
    DBFlush(false);
    StopNode();
    DBFlush(true);
    printf("Bitcoin exiting\n");
}

static void StartMiner()
{
    fGenerateBitcoins = true;
    if (!fMinerStarted && !vfThreadRunning[3])
    {
        fMinerStarted = true;
        if (_beginthread(ThreadBitcoinMiner, 0, NULL) == -1)
            fprintf(stderr, "Error: could not start miner thread\n");
    }
}

static void StopMiner()
{
    fGenerateBitcoins = false;
    fMinerStarted = false;
}

static double ExpectedHashes(unsigned int nBits)
{
    double target = ldexp((double)(nBits & 0x007fffff), 8 * ((int)(nBits >> 24) - 3));
    return target > 0 ? ldexp(1.0, 256) / target : 0;
}

static vector<string> Tokenize(const string& line)
{
    vector<string> v;
    istringstream iss(line);
    string tok;
    while (iss >> tok) v.push_back(tok);
    return v;
}

static string ScriptAddress(const CScript& script)
{
    uint160 hash160;
    if (ExtractHash160(script, hash160))
        return Hash160ToAddress(hash160);
    vector<unsigned char> vchPubKey;
    if (ExtractPubKey(script, false, vchPubKey))
        return PubKeyToAddress(vchPubKey);
    return "(non-standard script)";
}

static CBlockIndex* FindBlockIndex(const string& str)
{
    if (str.empty())
        return NULL;
    if (str.size() <= 9 && str.find_first_not_of("0123456789") == string::npos)
    {
        int nHeight = atoi(str.c_str());
        if (nHeight < 0 || nHeight > nBestHeight || !pindexBest)
            return NULL;
        CBlockIndex* pindex = pindexBest;
        while (pindex && pindex->nHeight > nHeight)
            pindex = pindex->pprev;
        return pindex;
    }
    uint256 hash;
    hash.SetHex(str);
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
    return mi == mapBlockIndex.end() ? NULL : (*mi).second;
}

extern CCriticalSection cs_mapTransactions;
extern bool AddToWalletIfMine(const CTransaction& tx, const CBlock* pblock);
static int64 nStartTime = GetTime();

static bool ParseHexString(const string& str, vector<unsigned char>& vchRet)
{
    vchRet.clear();
    if (str.empty() || str.size() % 2 != 0)
        return false;
    for (size_t i = 0; i < str.size(); i += 2)
    {
        char pair[3] = { str[i], str[i + 1], 0 };
        char* pend = NULL;
        unsigned long n = strtoul(pair, &pend, 16);
        if (*pend != 0)
            return false;
        vchRet.push_back((unsigned char)n);
    }
    return true;
}

static void PrintHelp()
{
    puts(
        "Commands:\n"
        "  help                      show this help\n"
        "  getinfo                   node status\n"
        "  getbalance                wallet balance\n"
        "  getnewaddress [label]     create a new receiving address\n"
        "  listaddresses             list wallet addresses\n"
        "  send <address> <amount> [comment]  send coins to a bitcoin address\n"
        "  setfee [amount]           show or set the transaction fee\n"
        "  listunspent               unspent wallet outputs\n"
        "  getreceivedbyaddress <addr> [minconf]  total received by an address\n"
        "  listreceivedbyaddress [minconf]        totals for all wallet addresses\n"
        "  dumpprivkey <addr>        print the private key (hex) of a wallet address\n"
        "  importprivkey <hex>       import a private key (then run rescan)\n"
        "  rescan                    rescan the chain for wallet transactions\n"
        "  rebroadcast               re-send wallet transactions to peers\n"
        "  listtransactions [n]      wallet transactions (default 20)\n"
        "  gen [on|off]              start/stop generating coins (mining)\n"
        "  addnode <ip[:port]>       connect to a peer\n"
        "  peers | getpeerinfo       list connected peers\n"
        "  getblockcount             height of the best chain\n"
        "  getbestblockhash          hash of the best block\n"
        "  getchaintips              chain tips (active and side branches)\n"
        "  getrawmempool             transactions waiting in the memory pool\n"
        "  getmempoolinfo            memory pool size\n"
        "  getrawtransaction <txid>  any transaction (memory pool or chain)\n"
        "  generate <n>              mine n blocks (-easy network only)\n"
        "  getconnectioncount        number of peers\n"
        "  getnetworkinfo            network settings\n"
        "  listknownpeers            addresses learned from peers\n"
        "  disconnectnode <ip[:port]>  drop a peer\n"
        "  uptime                    time since start\n"
        "  getblockhash <height>     hash of the block at a height\n"
        "  getblock <height|hash>    block details and transactions\n"
        "  gettransaction <txid>     wallet transaction details\n"
        "  getdifficulty             current difficulty\n"
        "  validateaddress <addr>    check an address (and if it is yours)\n"
        "  setlabel <addr> <label>   name an address\n"
        "  backupwallet <path>       copy wallet.dat to <path> (file or directory)\n"
        "  stop | quit | exit        shut down");
}

static bool DoCommand(const vector<string>& v)
{
    const string& cmd = v[0];
    if (cmd == "help" || cmd == "?")
        PrintHelp();
    else if (cmd == "stop" || cmd == "quit" || cmd == "exit")
        return false;
    else if (cmd == "getbalance")
        printf_cli("%s\n", FormatMoney(GetBalance()).c_str());
    else if (cmd == "getinfo")
    {
        printf_cli("version       : 0.1.0 (cli) %s\n", BUILD_ID);
        printf_cli("datadir       : %s\n", GetAppDir().c_str());
        printf_cli("blocks        : %d\n", nBestHeight);
        printf_cli("bestblock     : %s\n", hashBestChain.ToString().c_str());
        if (pindexBest)
            printf_cli("difficulty bits: %08x\n", pindexBest->nBits);
        int nNodes = 0;
        CRITICAL_BLOCK(cs_vNodes)
            nNodes = vNodes.size();
        printf_cli("connections   : %d\n", nNodes);
        printf_cli("listening on  : %s\n", addrLocalHost.ToString().c_str());
        printf_cli("generating    : %s\n", fGenerateBitcoins ? "yes" : "no");
        {
            static int64 nPrevHashes = 0, nPrevTime = GetTime();
            int64 nNow = GetTime(), nHashes = nMinerHashes;
            double dRate = nNow > nPrevTime ? (double)(nHashes - nPrevHashes) / (nNow - nPrevTime) : 0;
            if (nNow > nPrevTime) { nPrevHashes = nHashes; nPrevTime = nNow; }
            printf_cli("hashrate      : %.0f H/s (since last getinfo)\n", dRate);
            if (pindexBest && dRate > 0)
            {
                double dEst = ExpectedHashes(pindexBest->nBits) / dRate;
                printf_cli("est. per block: %.0f s (%.1f h)\n", dEst, dEst / 3600);
            }
            printf_cli("network       : %s\n", fEasy ? "easy (test)" : "main genesis");
        }
        printf_cli("balance       : %s\n", FormatMoney(GetBalance()).c_str());
        printf_cli("keys          : %d\n", (int)mapKeys.size());
    }
    else if (cmd == "getnewaddress")
    {
        string strAddr = PubKeyToAddress(GenerateNewKey());
        if (v.size() > 1)
        {
            string strLabel;
            for (size_t i = 1; i < v.size(); i++)
                strLabel += (i > 1 ? " " : "") + v[i];
            SetAddressBookName(strAddr, strLabel);
        }
        printf_cli("%s\n", strAddr.c_str());
    }
    else if (cmd == "listaddresses")
    {
        CRITICAL_BLOCK(cs_mapKeys)
        {
            foreach(const PAIRTYPE(const vector<unsigned char>, CPrivKey)& item, mapKeys)
            {
                string strAddr = PubKeyToAddress(item.first);
                map<string, string>::iterator mi = mapAddressBook.find(strAddr);
                printf_cli("%s  %s\n", strAddr.c_str(), mi != mapAddressBook.end() ? (*mi).second.c_str() : "");
            }
        }
    }
    else if (cmd == "send")
    {
        if (v.size() < 3) { puts("usage: send <address> <amount> [comment]"); return true; }
        uint160 hash160;
        if (!AddressToHash160(v[1], hash160)) { puts("error: invalid address"); return true; }
        int64 nValue;
        if (!ParseMoney(v[2].c_str(), nValue) || nValue <= 0) { puts("error: invalid amount"); return true; }
        if (nValue > GetBalance()) { puts("error: insufficient funds"); return true; }
        CWalletTx wtx;
        wtx.mapValue["to"] = v[1];
        if (v.size() > 3)
        {
            string strComment;
            for (size_t i = 3; i < v.size(); i++)
                strComment += (i > 3 ? " " : "") + v[i];
            wtx.mapValue["message"] = strComment;
        }
        CScript scriptPubKey;
        scriptPubKey << OP_DUP << OP_HASH160 << hash160 << OP_EQUALVERIFY << OP_CHECKSIG;
        try
        {
            if (SendMoney(scriptPubKey, nValue, wtx))
                printf_cli("sent %s to %s\ntxid %s\n", FormatMoney(nValue).c_str(), v[1].c_str(), wtx.GetHash().ToString().c_str());
            else
                puts("error: send failed (see debug.log)");
        }
        catch (std::exception& e) { printf_cli("error: %s\n", e.what()); }
    }
    else if (cmd == "listtransactions")
    {
        int nMax = v.size() > 1 ? atoi(v[1].c_str()) : 20;
        vector<pair<int64, uint256> > vSorted;
        CRITICAL_BLOCK(cs_mapWallet)
        {
            foreach(const PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
                vSorted.push_back(make_pair(item.second.GetTxTime(), item.first));
            sort(vSorted.begin(), vSorted.end());
            int nSkip = (int)vSorted.size() - nMax;
            for (int i = (nSkip > 0 ? nSkip : 0); i < (int)vSorted.size(); i++)
            {
                const CWalletTx& wtx = mapWallet[vSorted[i].second];
                int64 nNet = wtx.GetCredit() - wtx.GetDebit();
                string strStatus;
                if (wtx.IsCoinBase())
                    strStatus = wtx.GetBlocksToMaturity() > 0 ? strprintf("generated, matures in %d blocks", wtx.GetBlocksToMaturity()) : "generated";
                else
                    strStatus = strprintf("%d confirmations", wtx.GetDepthInMainChain());
                printf_cli("%s  %16s  %s  %s\n", DateTimeStr(vSorted[i].first).c_str(),
                           FormatMoney(nNet, true).c_str(), vSorted[i].second.ToString().substr(0, 16).c_str(), strStatus.c_str());
            }
        }
    }
    else if (cmd == "gen")
    {
        if (v.size() > 1 && v[1] == "off") StopMiner(); else StartMiner();
        printf_cli("generating: %s\n", fGenerateBitcoins ? "on" : "off");
        CWalletDB().WriteSetting("fGenerateBitcoins", fGenerateBitcoins);
    }
    else if (cmd == "addnode")
    {
        if (v.size() != 2) { puts("usage: addnode <ip[:port]>"); return true; }
        CAddress addr(v[1].c_str());
        if (addr.ip == 0 || addr.ip == INADDR_NONE) { puts("error: invalid address"); return true; }
        CNode* pnode = ConnectNode(addr);
        if (!pnode) { puts("error: connection failed"); return true; }
        pnode->fNetworkNode = true;
        pnode->PushMessage("getaddr");
        printf_cli("connected to %s\n", addr.ToString().c_str());
    }
    else if (cmd == "peers" || cmd == "getpeerinfo")
    {
        CRITICAL_BLOCK(cs_vNodes)
        {
            foreach(CNode* pnode, vNodes)
                printf_cli("%s  %s  version=%d  services=%llx\n", pnode->addr.ToString().c_str(), pnode->fInbound ? "inbound" : "outbound", pnode->nVersion, (unsigned long long)pnode->nServices);
            if (vNodes.empty()) puts("no peers");
        }
    }
    else if (cmd == "getblockcount")
        printf_cli("%d\n", nBestHeight);
    else if (cmd == "getblockhash")
    {
        if (v.size() != 2) { puts("usage: getblockhash <height>"); return true; }
        CRITICAL_BLOCK(cs_main)
        {
            CBlockIndex* pindex = FindBlockIndex(v[1]);
            if (!pindex) { puts("error: height out of range"); return true; }
            printf_cli("%s\n", pindex->GetBlockHash().ToString().c_str());
        }
    }
    else if (cmd == "getblock")
    {
        if (v.size() != 2) { puts("usage: getblock <height|hash>"); return true; }
        CRITICAL_BLOCK(cs_main)
        {
            CBlockIndex* pindex = FindBlockIndex(v[1]);
            if (!pindex) { puts("error: block not found"); return true; }
            CBlock block;
            if (!block.ReadFromDisk(pindex, true)) { puts("error: cannot read block from disk"); return true; }
            bool fMainChain = (pindex->pnext || pindex == pindexBest);
            printf_cli("hash          : %s\n", pindex->GetBlockHash().ToString().c_str());
            printf_cli("height        : %d\n", pindex->nHeight);
            if (fMainChain)
                printf_cli("confirmations : %d\n", nBestHeight - pindex->nHeight + 1);
            else
                printf_cli("confirmations : not in main chain\n");
            printf_cli("time          : %s\n", DateTimeStr(block.nTime).c_str());
            printf_cli("bits          : %08x\n", block.nBits);
            printf_cli("nonce         : %u\n", block.nNonce);
            printf_cli("merkleroot    : %s\n", block.hashMerkleRoot.ToString().c_str());
            printf_cli("prev          : %s\n", block.hashPrevBlock.ToString().c_str());
            if (pindex->pnext)
                printf_cli("next          : %s\n", pindex->pnext->GetBlockHash().ToString().c_str());
            printf_cli("tx            : %d\n", (int)block.vtx.size());
            foreach(const CTransaction& tx, block.vtx)
            {
                printf_cli("  %s  %s\n", tx.GetHash().ToString().c_str(),
                           tx.IsCoinBase() ? "coinbase" : strprintf("%d inputs", (int)tx.vin.size()).c_str());
                for (int i = 0; i < (int)tx.vout.size(); i++)
                    printf_cli("    out %d: %s -> %s\n", i, FormatMoney(tx.vout[i].nValue).c_str(),
                               ScriptAddress(tx.vout[i].scriptPubKey).c_str());
            }
        }
    }
    else if (cmd == "gettransaction")
    {
        if (v.size() != 2) { puts("usage: gettransaction <txid>"); return true; }
        uint256 hash;
        hash.SetHex(v[1]);
        CRITICAL_BLOCK(cs_mapWallet)
        {
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(hash);
            if (mi == mapWallet.end()) { puts("error: transaction not in wallet"); return true; }
            const CWalletTx& wtx = (*mi).second;
            string strStatus;
            if (wtx.IsCoinBase())
                strStatus = wtx.GetBlocksToMaturity() > 0 ? strprintf("generated, matures in %d blocks", wtx.GetBlocksToMaturity()) : "generated";
            else
                strStatus = strprintf("%d confirmations", wtx.GetDepthInMainChain());
            int64 nDebit = wtx.GetDebit(), nCredit = wtx.GetCredit();
            printf_cli("txid          : %s\n", hash.ToString().c_str());
            printf_cli("time          : %s\n", DateTimeStr(wtx.GetTxTime()).c_str());
            printf_cli("status        : %s\n", strStatus.c_str());
            printf_cli("debit         : %s\n", FormatMoney(nDebit).c_str());
            printf_cli("credit        : %s\n", FormatMoney(nCredit).c_str());
            printf_cli("net           : %s\n", FormatMoney(nCredit - nDebit, true).c_str());
            map<string, string>::const_iterator mt = wtx.mapValue.find("to");
            if (mt != wtx.mapValue.end())
                printf_cli("to            : %s\n", (*mt).second.c_str());
            map<string, string>::const_iterator mm = wtx.mapValue.find("message");
            if (mm != wtx.mapValue.end())
                printf_cli("message       : %s\n", (*mm).second.c_str());
            if (wtx.IsCoinBase())
                printf_cli("input         : coinbase\n");
            else
                for (int i = 0; i < (int)wtx.vin.size(); i++)
                    printf_cli("input %d: %s:%u\n", i, wtx.vin[i].prevout.hash.ToString().c_str(), wtx.vin[i].prevout.n);
            for (int i = 0; i < (int)wtx.vout.size(); i++)
                printf_cli("output %d: %s -> %s%s\n", i, FormatMoney(wtx.vout[i].nValue).c_str(),
                           ScriptAddress(wtx.vout[i].scriptPubKey).c_str(), wtx.vout[i].IsMine() ? "  (mine)" : "");
        }
    }
    else if (cmd == "getdifficulty")
    {
        unsigned int nBits = pindexBest ? pindexBest->nBits : bnProofOfWorkLimit.GetCompact();
        printf_cli("%.8f\n", ExpectedHashes(nBits) / 4294967296.0);
    }
    else if (cmd == "validateaddress")
    {
        if (v.size() != 2) { puts("usage: validateaddress <address>"); return true; }
        uint160 hash160;
        bool fValid = AddressToHash160(v[1], hash160);
        printf_cli("valid         : %s\n", fValid ? "yes" : "no");
        if (fValid)
        {
            CScript script;
            script << OP_DUP << OP_HASH160 << hash160 << OP_EQUALVERIFY << OP_CHECKSIG;
            printf_cli("ismine        : %s\n", IsMine(script) ? "yes" : "no");
            map<string, string>::iterator mi = mapAddressBook.find(v[1]);
            if (mi != mapAddressBook.end())
                printf_cli("label         : %s\n", (*mi).second.c_str());
        }
    }
    else if (cmd == "setlabel")
    {
        uint160 hash160;
        if (v.size() < 3 || !AddressToHash160(v[1], hash160)) { puts("usage: setlabel <address> <label>  (address must be valid)"); return true; }
        string strLabel;
        for (size_t i = 2; i < v.size(); i++)
            strLabel += (i > 2 ? " " : "") + v[i];
        SetAddressBookName(v[1], strLabel);
        puts("ok");
    }
    else if (cmd == "backupwallet")
    {
        if (v.size() != 2) { puts("usage: backupwallet <path>"); return true; }
        string strDest = v[1];
        struct stat st;
        if (stat(strDest.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            strDest += "/wallet.dat";
        if (stat(strDest.c_str(), &st) == 0) { puts("error: destination already exists"); return true; }
        DBFlush(false);
        FILE* fin = fopen((GetAppDir() + "/wallet.dat").c_str(), "rb");
        FILE* fout = fin ? fopen(strDest.c_str(), "wb") : NULL;
        if (!fin || !fout) { if (fin) fclose(fin); puts("error: cannot open wallet.dat or destination"); return true; }
        char buf[65536];
        size_t n, nTotal = 0;
        bool fOk = true;
        while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
        {
            if (fwrite(buf, 1, n, fout) != n) { fOk = false; break; }
            nTotal += n;
        }
        fclose(fin);
        if (fclose(fout) != 0) fOk = false;
        chmod(strDest.c_str(), 0600);
        if (fOk)
            printf_cli("wallet backed up to %s (%d bytes)\n", strDest.c_str(), (int)nTotal);
        else
            puts("error: write failed");
    }
    else if (cmd == "getbestblockhash")
        printf_cli("%s\n", hashBestChain.ToString().c_str());
    else if (cmd == "getconnectioncount")
    {
        int nNodes = 0;
        CRITICAL_BLOCK(cs_vNodes)
            nNodes = vNodes.size();
        printf_cli("%d\n", nNodes);
    }
    else if (cmd == "getnetworkinfo")
    {
        int nNodes = 0;
        CRITICAL_BLOCK(cs_vNodes)
            nNodes = vNodes.size();
        int nKnown = 0;
        CRITICAL_BLOCK(cs_mapAddresses)
            nKnown = mapAddresses.size();
        printf_cli("version       : 0.1.0 (cli) %s\n", BUILD_ID);
        printf_cli("protocol      : %d\n", VERSION);
        printf_cli("network       : %s\n", fEasy ? "easy (test)" : "main genesis");
        printf_cli("listening on  : %s\n", addrLocalHost.ToString().c_str());
        printf_cli("services      : %llx\n", (unsigned long long)nLocalServices);
        printf_cli("proxy         : %s\n", addrProxy.ip ? addrProxy.ToString().c_str() : "none");
        printf_cli("connections   : %d\n", nNodes);
        printf_cli("known peers   : %d\n", nKnown);
        printf_cli("irc seeding   : %s\n", fUseIRC ? "on" : "off");
    }
    else if (cmd == "uptime")
    {
        int64 n = GetTime() - nStartTime;
        printf_cli("%lldd %lldh %lldm %llds\n", n / 86400, (n / 3600) % 24, (n / 60) % 60, n % 60);
    }
    else if (cmd == "listknownpeers")
    {
        int nShown = 0;
        CRITICAL_BLOCK(cs_mapAddresses)
        {
            foreach(const PAIRTYPE(const vector<unsigned char>, CAddress)& item, mapAddresses)
            {
                if (nShown++ >= 100) break;
                printf_cli("%s  last seen %s  services=%llx\n", item.second.ToString().c_str(),
                           DateTimeStr(item.second.nTime).c_str(), (unsigned long long)item.second.nServices);
            }
        }
        if (nShown == 0) puts("no known peers");
    }
    else if (cmd == "disconnectnode")
    {
        if (v.size() != 2) { puts("usage: disconnectnode <ip[:port]>"); return true; }
        CAddress addr(v[1].c_str());
        bool fPort = v[1].find(':') != string::npos;
        int nDropped = 0;
        CRITICAL_BLOCK(cs_vNodes)
        {
            foreach(CNode* pnode, vNodes)
                if (pnode->addr.ip == addr.ip && (!fPort || pnode->addr.port == addr.port))
                {
                    pnode->fDisconnect = true;
                    nDropped++;
                }
        }
        if (nDropped) printf_cli("disconnected %d node(s)\n", nDropped);
        else puts("error: no such peer");
    }
    else if (cmd == "getmempoolinfo")
    {
        int nTx = 0;
        CRITICAL_BLOCK(cs_mapTransactions)
            nTx = mapTransactions.size();
        printf_cli("size          : %d\n", nTx);
    }
    else if (cmd == "getrawmempool")
    {
        CRITICAL_BLOCK(cs_mapTransactions)
        {
            foreach(const PAIRTYPE(const uint256, CTransaction)& item, mapTransactions)
                printf_cli("%s\n", item.first.ToString().c_str());
        }
    }
    else if (cmd == "getrawtransaction")
    {
        if (v.size() != 2) { puts("usage: getrawtransaction <txid>"); return true; }
        uint256 hash;
        hash.SetHex(v[1]);
        CTransaction tx;
        const char* pszWhere = NULL;
        CRITICAL_BLOCK(cs_mapTransactions)
        {
            map<uint256, CTransaction>::iterator mi = mapTransactions.find(hash);
            if (mi != mapTransactions.end())
            {
                tx = (*mi).second;
                pszWhere = "memory pool";
            }
        }
        if (!pszWhere)
        {
            CTxDB txdb("r");
            if (txdb.ReadDiskTx(hash, tx))
                pszWhere = "chain";
        }
        if (!pszWhere) { puts("error: transaction not found"); return true; }
        printf_cli("txid          : %s\n", tx.GetHash().ToString().c_str());
        printf_cli("where         : %s\n", pszWhere);
        printf_cli("version       : %d\n", tx.nVersion);
        printf_cli("locktime      : %u\n", tx.nLockTime);
        if (tx.IsCoinBase())
            printf_cli("input         : coinbase\n");
        else
            for (int i = 0; i < (int)tx.vin.size(); i++)
                printf_cli("input %d: %s:%u\n", i, tx.vin[i].prevout.hash.ToString().c_str(), tx.vin[i].prevout.n);
        for (int i = 0; i < (int)tx.vout.size(); i++)
            printf_cli("output %d: %s -> %s\n", i, FormatMoney(tx.vout[i].nValue).c_str(),
                       ScriptAddress(tx.vout[i].scriptPubKey).c_str());
    }
    else if (cmd == "getchaintips")
    {
        CRITICAL_BLOCK(cs_main)
        {
            set<CBlockIndex*> setParents;
            for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
                if ((*mi).second->pprev)
                    setParents.insert((*mi).second->pprev);
            for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
            {
                CBlockIndex* pindex = (*mi).second;
                if (setParents.count(pindex))
                    continue;
                int nBranchLen = 0;
                CBlockIndex* p = pindex;
                while (p && !(p->pnext || p == pindexBest))
                {
                    nBranchLen++;
                    p = p->pprev;
                }
                printf_cli("height=%d  hash=%s  branchlen=%d  %s\n", pindex->nHeight,
                           pindex->GetBlockHash().ToString().c_str(), nBranchLen,
                           pindex == pindexBest ? "active" : "fork");
            }
        }
    }
    else if (cmd == "generate")
    {
        if (!fEasy) { puts("error: generate only works with -easy"); return true; }
        int nBlocks = v.size() > 1 ? atoi(v[1].c_str()) : 1;
        if (nBlocks < 1 || nBlocks > 1000) { puts("usage: generate <1-1000>"); return true; }
        int nTarget = nBestHeight + nBlocks;
        bool fWasOn = fGenerateBitcoins;
        StartMiner();
        for (int i = 0; i < 1200 && nBestHeight < nTarget && !fQuitSignal; i++)
            Sleep(100);
        if (!fWasOn)
            StopMiner();
        printf_cli("mined to height %d\n", nBestHeight);
    }
    else if (cmd == "rebroadcast")
    {
        RelayWalletTransactions();
        puts("ok");
    }
    else if (cmd == "setfee")
    {
        if (v.size() > 2) { puts("usage: setfee [amount]"); return true; }
        if (v.size() == 2)
        {
            int64 nFee;
            if (!ParseMoney(v[1].c_str(), nFee) && v[1] != "0") { puts("error: invalid amount"); return true; }
            if (v[1] == "0") nFee = 0;
            if (nFee < 0 || nFee > COIN) { puts("error: fee must be between 0 and 1"); return true; }
            nTransactionFee = nFee;
            CWalletDB().WriteSetting("nTransactionFee", nTransactionFee);
        }
        printf_cli("transaction fee: %s\n", FormatMoney(nTransactionFee).c_str());
    }
    else if (cmd == "listunspent")
    {
        int nShown = 0;
        CRITICAL_BLOCK(cs_mapWallet)
        {
            foreach(const PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            {
                const CWalletTx& wtx = item.second;
                if (!wtx.IsFinal() || wtx.fSpent || wtx.GetCredit() == 0)
                    continue;
                for (int i = 0; i < (int)wtx.vout.size(); i++)
                {
                    if (!wtx.vout[i].IsMine())
                        continue;
                    printf_cli("%s:%d  %s  %s  %d confirmations\n", item.first.ToString().c_str(), i,
                               FormatMoney(wtx.vout[i].nValue).c_str(),
                               ScriptAddress(wtx.vout[i].scriptPubKey).c_str(), wtx.GetDepthInMainChain());
                    nShown++;
                }
            }
        }
        if (nShown == 0) puts("no unspent outputs");
    }
    else if (cmd == "getreceivedbyaddress" || cmd == "listreceivedbyaddress")
    {
        bool fList = (cmd == "listreceivedbyaddress");
        if (!fList && (v.size() < 2 || v.size() > 3)) { puts("usage: getreceivedbyaddress <address> [minconf]"); return true; }
        int nMinConf = fList ? (v.size() > 1 ? atoi(v[1].c_str()) : 1) : (v.size() > 2 ? atoi(v[2].c_str()) : 1);
        string strWanted = fList ? "" : v[1];
        map<string, int64> mapTotals;
        CRITICAL_BLOCK(cs_mapWallet)
        {
            foreach(const PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            {
                const CWalletTx& wtx = item.second;
                if (!wtx.IsFinal() || wtx.GetDepthInMainChain() < nMinConf)
                    continue;
                if (wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
                    continue;
                foreach(const CTxOut& txout, wtx.vout)
                {
                    if (!txout.IsMine())
                        continue;
                    string strAddr = ScriptAddress(txout.scriptPubKey);
                    if (!fList && strAddr != strWanted)
                        continue;
                    mapTotals[strAddr] += txout.nValue;
                }
            }
        }
        if (!fList)
            printf_cli("%s\n", FormatMoney(mapTotals[strWanted]).c_str());
        else
            foreach(const PAIRTYPE(const string, int64)& item, mapTotals)
            {
                map<string, string>::iterator mi = mapAddressBook.find(item.first);
                printf_cli("%s  %s  %s\n", item.first.c_str(), FormatMoney(item.second).c_str(),
                           mi != mapAddressBook.end() ? (*mi).second.c_str() : "");
            }
    }
    else if (cmd == "dumpprivkey")
    {
        if (v.size() != 2) { puts("usage: dumpprivkey <address>"); return true; }
        uint160 hash160;
        if (!AddressToHash160(v[1], hash160)) { puts("error: invalid address"); return true; }
        CRITICAL_BLOCK(cs_mapKeys)
        {
            map<uint160, vector<unsigned char> >::iterator mp = mapPubKeys.find(hash160);
            if (mp == mapPubKeys.end()) { puts("error: address is not in this wallet"); return true; }
            map<vector<unsigned char>, CPrivKey>::iterator mk = mapKeys.find((*mp).second);
            if (mk == mapKeys.end()) { puts("error: private key not found"); return true; }
            printf_cli("%s\n", HexStr((*mk).second.begin(), (*mk).second.end(), false).c_str());
        }
    }
    else if (cmd == "importprivkey")
    {
        if (v.size() != 2) { puts("usage: importprivkey <hex>  (as printed by dumpprivkey)"); return true; }
        vector<unsigned char> vch;
        if (!ParseHexString(v[1], vch)) { puts("error: not a hex string"); return true; }
        CKey key;
        if (!key.SetPrivKey(CPrivKey(vch.begin(), vch.end()))) { puts("error: invalid private key"); return true; }
        if (!AddKey(key)) { puts("error: could not store key"); return true; }
        printf_cli("imported %s (run rescan to find its past transactions)\n", PubKeyToAddress(key.GetPubKey()).c_str());
    }
    else if (cmd == "rescan")
    {
        int nBefore = 0, nAfter = 0, nBlocks = 0;
        CRITICAL_BLOCK(cs_main)
        CRITICAL_BLOCK(cs_mapWallet)
        {
            nBefore = mapWallet.size();
            vector<CBlockIndex*> vChain;
            for (CBlockIndex* p = pindexBest; p; p = p->pprev)
                vChain.push_back(p);
            for (int i = (int)vChain.size() - 1; i >= 0; i--)
            {
                CBlock block;
                if (!block.ReadFromDisk(vChain[i], true))
                    continue;
                nBlocks++;
                foreach(const CTransaction& tx, block.vtx)
                    AddToWalletIfMine(tx, &block);
            }
            // refresh the spent flag from the transaction index
            CTxDB txdb("r");
            foreach(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            {
                CWalletTx& wtx = item.second;
                CTxIndex txindex;
                if (wtx.fSpent || !txdb.ReadTxIndex(wtx.GetHash(), txindex))
                    continue;
                for (int i = 0; i < (int)wtx.vout.size() && i < (int)txindex.vSpent.size(); i++)
                    if (!txindex.vSpent[i].IsNull() && wtx.vout[i].IsMine())
                    {
                        wtx.fSpent = true;
                        wtx.WriteToDisk();
                        break;
                    }
            }
            nAfter = mapWallet.size();
        }
        printf_cli("rescan complete: %d blocks, %d new wallet transaction(s)\n", nBlocks, nAfter - nBefore);
    }
    else
        printf_cli("unknown command '%s' (try: help)\n", cmd.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Daemon mode and local command socket (Unix domain socket in the data directory,
// mode 0600, so only the same user can connect)
// ---------------------------------------------------------------------------
static pthread_mutex_t g_cmdMutex = PTHREAD_MUTEX_INITIALIZER;
static string g_strSockPath;
static volatile bool g_fRpcStop = false;
static volatile bool g_fRpcRunning = false;
static int g_hRpcListen = -1;

static bool FillSockAddr(const string& strPath, struct sockaddr_un& sa)
{
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strPath.size() >= sizeof(sa.sun_path))
        return false;
    memcpy(sa.sun_path, strPath.c_str(), strPath.size() + 1);
    return true;
}

static void RpcHandleClient(int hClient)
{
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(hClient, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    string strLine;
    char ch;
    while (strLine.size() < 65536 && recv(hClient, &ch, 1, 0) == 1 && ch != '\n')
        strLine += ch;

    string strOut;
    bool fContinue = true;
    vector<string> v = Tokenize(strLine);
    if (v.empty())
        strOut = "error: empty command\n";
    else
    {
        char* pbuf = NULL;
        size_t nlen = 0;
        FILE* mem = open_memstream(&pbuf, &nlen);
        if (!mem)
            strOut = "error: out of memory\n";
        else
        {
            pthread_mutex_lock(&g_cmdMutex);
            g_out = mem;
            try { fContinue = DoCommand(v); }
            catch (std::exception& e) { fprintf(g_out, "error: %s\n", e.what()); }
            g_out = stdout;
            pthread_mutex_unlock(&g_cmdMutex);
            fclose(mem);
            if (pbuf)
            {
                strOut.assign(pbuf, nlen);
                free(pbuf);
            }
            if (!fContinue)
                strOut += "stopping\n";
        }
    }
    size_t nSent = 0;
    while (nSent < strOut.size())
    {
        ssize_t n = send(hClient, strOut.data() + nSent, strOut.size() - nSent, 0);
        if (n <= 0)
            break;
        nSent += n;
    }
    if (!fContinue)
        fQuitSignal = 1;
}

static void ThreadRpcServer(void* parg)
{
    try
    {
        while (!g_fRpcStop && !fQuitSignal)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(g_hRpcListen, &fds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 300000;
            if (select(g_hRpcListen + 1, &fds, NULL, NULL, &tv) <= 0)
                continue;
            int hClient = accept(g_hRpcListen, NULL, NULL);
            if (hClient < 0)
                continue;
            RpcHandleClient(hClient);
            close(hClient);
        }
    }
    catch (...) { }
    g_fRpcRunning = false;
}

static bool StartRpcServer(const string& strDir)
{
    g_strSockPath = strDir + "/bitcoin.sock";
    struct sockaddr_un sa;
    if (!FillSockAddr(g_strSockPath, sa))
    {
        fprintf(stderr, "Error: socket path too long: %s\n", g_strSockPath.c_str());
        return false;
    }
    unlink(g_strSockPath.c_str());   // stale socket of a crashed run (we hold the data directory lock)
    g_hRpcListen = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_hRpcListen < 0)
        return false;
    mode_t nOldMask = umask(077);
    int nRet = ::bind(g_hRpcListen, (struct sockaddr*)&sa, sizeof(sa));
    umask(nOldMask);
    if (nRet != 0 || listen(g_hRpcListen, 8) != 0)
    {
        fprintf(stderr, "Error: cannot listen on %s\n", g_strSockPath.c_str());
        close(g_hRpcListen);
        g_hRpcListen = -1;
        return false;
    }
    chmod(g_strSockPath.c_str(), 0600);
    FILE* f = fopen((strDir + "/bitcoin.pid").c_str(), "w");
    if (f) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }
    g_fRpcStop = false;
    g_fRpcRunning = true;
    if (_beginthread(ThreadRpcServer, 0, NULL) == -1)
    {
        g_fRpcRunning = false;
        fprintf(stderr, "Error: cannot start the command socket thread\n");
        return false;
    }
    return true;
}

static void StopRpcServer(const string& strDir)
{
    g_fRpcStop = true;
    for (int i = 0; i < 50 && g_fRpcRunning; i++)
        Sleep(100);
    if (g_hRpcListen >= 0)
    {
        close(g_hRpcListen);
        g_hRpcListen = -1;
        unlink(g_strSockPath.c_str());
        unlink((strDir + "/bitcoin.pid").c_str());
    }
}

// Fork into the background. The parent waits until the child accepts connections
// (or dies) and then exits; only the child returns.
static void Daemonize(const string& strDir)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }
    if (pid > 0)
    {
        struct sockaddr_un sa;
        bool fPath = FillSockAddr(strDir + "/bitcoin.sock", sa);
        for (int i = 0; i < 600; i++)
        {
            int nStatus;
            if (waitpid(pid, &nStatus, WNOHANG) == pid)
            {
                fprintf(stderr, "Error: bitcoin failed to start, see %s/daemon.out\n", strDir.c_str());
                _exit(1);
            }
            if (fPath)
            {
                int h = socket(AF_UNIX, SOCK_STREAM, 0);
                if (h >= 0)
                {
                    int nOk = connect(h, (struct sockaddr*)&sa, sizeof(sa));
                    close(h);
                    if (nOk == 0)
                    {
                        printf_cli("Bitcoin server started (pid %d), datadir %s\n", (int)pid, strDir.c_str());
                        fflush(stdout);
                        _exit(0);
                    }
                }
            }
            Sleep(100);
        }
        printf_cli("Bitcoin server is still starting (pid %d), see %s/daemon.out\n", (int)pid, strDir.c_str());
        fflush(stdout);
        _exit(0);
    }
    setsid();
    signal(SIGHUP, SIG_IGN);
    int fdNull = open("/dev/null", O_RDONLY);
    if (fdNull >= 0) dup2(fdNull, 0);
    int fdOut = open((strDir + "/daemon.out").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fdOut >= 0)
    {
        dup2(fdOut, 1);
        dup2(fdOut, 2);
    }
}

// Client mode: bitcoin [-datadir=DIR] <command> [args]
static int ClientMain(map<string, string>& mapArgs, const vector<string>& vCmd)
{
    string strDir;
    if (mapArgs.count("datadir"))
    {
        char rp[PATH_MAX];
        strDir = realpath(mapArgs["datadir"].c_str(), rp) ? string(rp) : mapArgs["datadir"];
    }
    else if (getenv("HOME"))
        strDir = string(getenv("HOME")) + (mapArgs.count("easy") ? "/.bitcoin-easy" : "/.bitcoin");
    else
    {
        fprintf(stderr, "error: cannot determine the data directory\n");
        return 1;
    }
    string strLine;
    for (size_t i = 0; i < vCmd.size(); i++)
    {
        string strArg = vCmd[i];
        if (i == 1 && vCmd[0] == "backupwallet" && !strArg.empty() && strArg[0] != '/')
        {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)))
                strArg = string(cwd) + "/" + strArg;   // the node runs in another directory
        }
        strLine += (i ? " " : "") + strArg;
    }
    strLine += "\n";

    string strSock = strDir + "/bitcoin.sock";
    struct sockaddr_un sa;
    int h = -1;
    if (!FillSockAddr(strSock, sa) || (h = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
        connect(h, (struct sockaddr*)&sa, sizeof(sa)) != 0)
    {
        fprintf(stderr, "error: cannot connect to %s (is the node running with -daemon or -server?)\n", strSock.c_str());
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    size_t nSent = 0;
    while (nSent < strLine.size())
    {
        ssize_t n = send(h, strLine.data() + nSent, strLine.size() - nSent, 0);
        if (n <= 0)
            break;
        nSent += n;
    }
    string strOut;
    char buf[4096];
    ssize_t n;
    while ((n = recv(h, buf, sizeof(buf), 0)) > 0)
        strOut.append(buf, n);
    close(h);
    fwrite(strOut.data(), 1, strOut.size(), stdout);
    return strOut.compare(0, 6, "error:") == 0 ? 1 : 0;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    umask(077);   // data directory 0700, every file we create 0600 (wallet.dat holds private keys)
    map<string, string> mapArgs;
    vector<string> vClientCmd;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            vClientCmd.push_back(argv[i]);
            continue;
        }
        string s = argv[i];
        while (!s.empty() && s[0] == '-') s = s.substr(1);
        string strVal;
        size_t pos = s.find('=');
        if (pos != string::npos) { strVal = s.substr(pos + 1); s = s.substr(0, pos); }
        mapArgs[s] = strVal;
    }
    if (mapArgs.count("help") || mapArgs.count("h"))
    {
        puts("Usage: bitcoin [-datadir=DIR] [-gen] [-addnode=IP[:PORT]] [-port=N] [-proxy=IP:PORT]\n"
             "               [-debug] [-printtoconsole] [-irc] [-noprompt] [-easy] [-solo] [-stdin]\n"
             "               [-daemon] [-server] [-version]\n"
             "               client: bitcoin [-datadir=DIR] <command> [args]");
        PrintHelp();
        return 0;
    }
    if (mapArgs.count("version"))
    {
        printf_cli("Bitcoin 0.1.0 (cli) %s\n", BUILD_ID);
        return 0;
    }
    if (!vClientCmd.empty())
        return ClientMain(mapArgs, vClientCmd);
    if (mapArgs.count("easy"))
    {
        SelectEasyNetwork();
        if (!mapArgs.count("datadir") && getenv("HOME"))
            mapArgs["datadir"] = string(getenv("HOME")) + "/.bitcoin-easy";
    }
    if (mapArgs.count("datadir"))
    {
        mkdir(mapArgs["datadir"].c_str(), 0700);
        char rp[PATH_MAX];
        strSetDataDir = realpath(mapArgs["datadir"].c_str(), rp) ? string(rp) : mapArgs["datadir"];
    }
    if (mapArgs.count("proxy"))        addrProxy = CAddress(mapArgs["proxy"].c_str());
    if (mapArgs.count("debug"))        fDebug = true;
    if (mapArgs.count("printtoconsole")) fPrintToConsole = true;
    if (mapArgs.count("irc"))          fUseIRC = true;
    if (mapArgs.count("solo"))         fSolo = true;
    if (mapArgs.count("port"))         nListenPort = htons((unsigned short)atoi(mapArgs["port"].c_str()));
    if (mapArgs.count("dropmessages")) nDropMessagesTest = atoi(mapArgs["dropmessages"].c_str());

    string strDir = GetAppDir();
    {
        // tighten a data directory created by an older version (only if it is ours)
        struct stat stDir;
        if (stat(strDir.c_str(), &stDir) == 0 && stDir.st_uid == getuid() && (stDir.st_mode & 077))
            chmod(strDir.c_str(), 0700);
    }
    strDebugLogPath = strDir + "/debug.log";
    if (mapArgs.count("daemon"))
        Daemonize(strDir);

    // single instance per data directory
    int fdLock = open((strDir + "/.lock").c_str(), O_CREAT | O_RDWR, 0666);
    if (fdLock < 0 || flock(fdLock, LOCK_EX | LOCK_NB) != 0)
    {
        fprintf(stderr, "Error: another instance is already using %s\n", strDir.c_str());
        return 1;
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    signal(SIGPIPE, SIG_IGN);

    try
    {
        printf_cli("Bitcoin 0.1.0 (cli) starting, datadir=%s\n", strDir.c_str());
        string strErrors;
        if (!LoadAddresses())   strErrors += "Error loading addr.dat\n";
        if (!LoadBlockIndex())  strErrors += "Error loading blkindex.dat\n";
        if (!LoadWallet())      strErrors += "Error loading wallet.dat\n";
        if (!strErrors.empty()) { fputs(strErrors.c_str(), stderr); return 1; }

        printf_cli("blocks=%d keys=%d wallettx=%d\n", nBestHeight, (int)mapKeys.size(), (int)mapWallet.size());
        ReacceptWalletTransactions();

        if (mapArgs.count("printblocktree")) { PrintBlockTree(); return 0; }
        if (mapArgs.count("gen"))
            fGenerateBitcoins = mapArgs["gen"].empty() ? 1 : atoi(mapArgs["gen"].c_str());

        if (!StartNode(strErrors))
            fprintf(stderr, "%s\n", strErrors.c_str());
        if (fGenerateBitcoins)
            StartMiner();

        if (mapArgs.count("addnode"))
        {
            CAddress addr(mapArgs["addnode"].c_str());
            CNode* pnode = ConnectNode(addr);
            if (pnode) { pnode->fNetworkNode = true; pnode->PushMessage("getaddr"); }
        }
    }
    catch (std::exception& e) { fprintf(stderr, "Fatal: %s\n", e.what()); return 1; }

    bool fServer = mapArgs.count("daemon") || mapArgs.count("server");
    if (fServer && !StartRpcServer(strDir))
    {
        fprintf(stderr, "Error: command socket not available\n");
        if (mapArgs.count("daemon"))
        {
            Shutdown();
            return 1;
        }
    }
    printf_cli("Type 'help' for commands.\n");
    bool fTty = isatty(0);
    bool fInteractive = !mapArgs.count("noprompt") && (fTty || mapArgs.count("stdin"));
    while (!fQuitSignal)
    {
        if (!fInteractive) { Sleep(500); continue; }
        if (fTty) { fputs("bitcoin> ", stdout); fflush(stdout); }
        bool fReady = false;
        while (!fQuitSignal && !fReady)
        {
            fd_set fds; FD_ZERO(&fds); FD_SET(0, &fds);
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 500000;
            fReady = (select(1, &fds, NULL, NULL, &tv) > 0);
        }
        if (fQuitSignal) break;
        string line;
        if (!getline(cin, line)) break;
        vector<string> v = Tokenize(line);
        if (v.empty()) continue;
        bool fContinue = true;
        pthread_mutex_lock(&g_cmdMutex);
        try { fContinue = DoCommand(v); }
        catch (std::exception& e) { printf_cli("error: %s\n", e.what()); }
        if (!fTty) puts("<<END>>");
        fflush(stdout);
        pthread_mutex_unlock(&g_cmdMutex);
        if (!fContinue)
            break;
    }
    StopRpcServer(strDir);
    Shutdown();
    return 0;
}
