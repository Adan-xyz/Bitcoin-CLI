// Console (CLI) front-end replacing the wxWidgets GUI of Bitcoin 0.1.0
#include "headers.h"
#include <sys/file.h>
#undef printf
#define printf_cli(...) fprintf(stdout, __VA_ARGS__)
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

static void PrintHelp()
{
    puts(
        "Commands:\n"
        "  help                      show this help\n"
        "  getinfo                   node status\n"
        "  getbalance                wallet balance\n"
        "  getnewaddress [label]     create a new receiving address\n"
        "  listaddresses             list wallet addresses\n"
        "  send <address> <amount>   send coins to a bitcoin address\n"
        "  listtransactions [n]      wallet transactions (default 20)\n"
        "  gen [on|off]              start/stop generating coins (mining)\n"
        "  addnode <ip[:port]>       connect to a peer\n"
        "  peers | getpeerinfo       list connected peers\n"
        "  getblockcount             height of the best chain\n"
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
        printf_cli("version       : 0.1.0 (cli)\n");
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
        if (v.size() != 3) { puts("usage: send <address> <amount>"); return true; }
        uint160 hash160;
        if (!AddressToHash160(v[1], hash160)) { puts("error: invalid address"); return true; }
        int64 nValue;
        if (!ParseMoney(v[2].c_str(), nValue) || nValue <= 0) { puts("error: invalid amount"); return true; }
        if (nValue > GetBalance()) { puts("error: insufficient funds"); return true; }
        CWalletTx wtx;
        wtx.mapValue["to"] = v[1];
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
                printf_cli("%s  %+16s  %s  %s\n", DateTimeStr(vSorted[i].first).c_str(),
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
    else
        printf_cli("unknown command '%s' (try: help)\n", cmd.c_str());
    return true;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    map<string, string> mapArgs;
    for (int i = 1; i < argc; i++)
    {
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
             "               [-debug] [-printtoconsole] [-irc] [-noprompt] [-easy] [-solo] [-stdin]");
        PrintHelp();
        return 0;
    }
    if (mapArgs.count("easy"))
    {
        SelectEasyNetwork();
        if (!mapArgs.count("datadir") && getenv("HOME"))
            mapArgs["datadir"] = string(getenv("HOME")) + "/.bitcoin-easy";
    }
    if (mapArgs.count("datadir"))
    {
        mkdir(mapArgs["datadir"].c_str(), 0777);
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
    strDebugLogPath = strDir + "/debug.log";

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
        try { if (!DoCommand(v)) break; }
        catch (std::exception& e) { printf_cli("error: %s\n", e.what()); }
        if (!fTty) puts("<<END>>");
        fflush(stdout);
    }
    Shutdown();
    return 0;
}
