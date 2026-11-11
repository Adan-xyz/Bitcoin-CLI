// POSIX compatibility layer for the original Win32 Bitcoin 0.1.0 sources
#ifndef BITCOIN_COMPAT_H
#define BITCOIN_COMPAT_H

#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
struct ThreadExit {};
#define CATCH_FORCED_UNWIND catch (ThreadExit&) { throw; }

// ---- types
typedef int SOCKET;
typedef unsigned long u_long_compat;
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)
#define closesocket(s)  close(s)
#define WSAGetLastError() errno
#define WSAEWOULDBLOCK  EWOULDBLOCK
#define WSAEMSGSIZE     EMSGSIZE
#define WSAEINTR        EINTR
#define WSAEINPROGRESS  EINPROGRESS
#define WSAEADDRINUSE   EADDRINUSE
#define NO_ERROR        0
#define MAKEWORD(a,b)   ((a)|((b)<<8))
struct WSADATA { int dummy; };
inline int WSAStartup(int, WSADATA*) { signal(SIGPIPE, SIG_IGN); return 0; }
inline int WSACleanup() { return 0; }
#ifndef FIONBIO_COMPAT
#define FIONBIO_COMPAT 1
#endif
inline int ioctlsocket_nb(SOCKET s, int nb)
{
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return -1;
    fl = (nb ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
    return fcntl(s, F_SETFL, fl);
}
#define ioctlsocket(s, cmd, pn) ioctlsocket_nb((s), (int)*(pn))

// ---- misc
#define __forceinline   inline
#define _UI64_MAX       ULLONG_MAX
#define _I64_MAX        LLONG_MAX
#define _vsnprintf      vsnprintf
#define stricmp         strcasecmp
#define _mkdir(p)       mkdir((p), 0777)
inline char* strlwr(char* s) { for (char* p = s; *p; p++) *p = tolower((unsigned char)*p); return s; }
#define ExitProcess(n)  exit(n)

inline void Sleep(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

union LARGE_INTEGER { long long QuadPart; };
inline int QueryPerformanceCounter(LARGE_INTEGER* p)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    p->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}

// ---- threads
inline int _beginthread(void (*fn)(void*), unsigned int, void* arg)
{
    struct Ctx { void (*fn)(void*); void* arg; };
    struct Run {
        static void* go(void* p) {
            Ctx c = *(Ctx*)p; delete (Ctx*)p;
            try { c.fn(c.arg); } catch (ThreadExit&) { }
            return NULL;
        }
    };
    Ctx* pc = new Ctx; pc->fn = fn; pc->arg = arg;
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    int ret = pthread_create(&t, &attr, Run::go, pc);
    pthread_attr_destroy(&attr);
    if (ret != 0) { delete pc; return -1; }
    return 1;
}
inline void _endthread() { throw ThreadExit(); }
#define GetCurrentThread()              0
#define SetThreadPriority(h, p)         ((void)0)
#define THREAD_PRIORITY_LOWEST          0
#define THREAD_PRIORITY_BELOW_NORMAL    0
#define THREAD_PRIORITY_NORMAL          0

// ---- critical section (recursive mutex)
typedef pthread_mutex_t CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION* p)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(p, &a);
    pthread_mutexattr_destroy(&a);
}
inline void DeleteCriticalSection(CRITICAL_SECTION* p) { pthread_mutex_destroy(p); }
inline void EnterCriticalSection(CRITICAL_SECTION* p)  { pthread_mutex_lock(p); }
inline void LeaveCriticalSection(CRITICAL_SECTION* p)  { pthread_mutex_unlock(p); }
inline bool TryEnterCriticalSection(CRITICAL_SECTION* p) { return pthread_mutex_trylock(p) == 0; }

#endif
