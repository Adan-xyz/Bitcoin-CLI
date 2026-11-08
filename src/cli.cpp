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

static vector<string> Tokenize(const string& line)
{
    vector<string> v;
    istringstream iss(line);
    string tok;
    while (iss >> tok) v.push_back(tok);
    return v;
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
        "  peers                     list connected peers\n"
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
    else if (cmd == "peers")
    {
        CRITICAL_BLOCK(cs_vNodes)
        {
            foreach(CNode* pnode, vNodes)
                printf_cli("%s  %s\n", pnode->addr.ToString().c_str(), pnode->fInbound ? "inbound" : "outbound");
            if (vNodes.empty()) puts("no peers");
        }
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
             "               [-debug] [-printtoconsole] [-irc] [-noprompt] [-easy] [-stdin]");
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
