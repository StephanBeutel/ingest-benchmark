#include "net-probe.hpp"
#include "plugin-support.h"

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>

// ── Platform-specific socket includes ────────────────────────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using socklen_t = int;
#  define SOCKET_ERRNO  WSAGetLastError()
#  define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#  define SOCK_CLOSE(s)  closesocket(s)
   typedef SOCKET sock_t;
   static const sock_t INVALID_SOCK = INVALID_SOCKET;
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  define SOCKET_ERRNO  errno
#  define WOULD_BLOCK(e) ((e) == EINPROGRESS || (e) == EWOULDBLOCK)
#  define SOCK_CLOSE(s)  ::close(s)
   typedef int sock_t;
   static const sock_t INVALID_SOCK = -1;
#endif

// sys/select.h is already pulled in by sys/socket.h on macOS/Linux.
// On Windows it comes from winsock2.h above. No explicit include needed.

namespace twitch_bench {

using Clock = std::chrono::steady_clock;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void setNonBlocking(sock_t s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

// Returns elapsed milliseconds from `start` to now
static int64_t elapsedMs(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now() - start).count();
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform init/cleanup
// ─────────────────────────────────────────────────────────────────────────────

bool NetProbe::initPlatform()
{
#ifdef _WIN32
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        TLOG_ERROR("WSAStartup failed: %d", rc);
        return false;
    }
#endif
    return true;
}

void NetProbe::cleanupPlatform()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// DNS probe
//
// POSIX getaddrinfo is blocking and has no built-in timeout.  We run it in a
// worker thread and wait up to timeoutMs.  If it doesn't return in time we
// mark the result as failed.  We let the thread detach and finish on its own
// (safe because getaddrinfo is reentrant and has no OBS state).
// ─────────────────────────────────────────────────────────────────────────────

DnsResult NetProbe::probeDns(const std::string &hostname, int timeoutMs)
{
    DnsResult result;

    struct SharedState {
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        DnsResult res;
    };
    auto state = std::make_shared<SharedState>();

    std::thread worker([state, hostname]() {
        struct addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo *ai = nullptr;
        auto t0 = Clock::now();
        int rc  = getaddrinfo(hostname.c_str(), nullptr, &hints, &ai);
        int64_t ms = elapsedMs(t0);

        DnsResult r;
        if (rc == 0 && ai) {
            r.success    = true;
            r.latencyMs  = ms;
            // Convert first address to string
            char buf[INET6_ADDRSTRLEN] = {};
            void *addr = nullptr;
            if (ai->ai_family == AF_INET)
                addr = &reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_addr;
            else if (ai->ai_family == AF_INET6)
                addr = &reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr)->sin6_addr;
            if (addr)
                inet_ntop(ai->ai_family, addr, buf, sizeof(buf));
            r.resolvedIp = buf;
            freeaddrinfo(ai);
        }

        std::lock_guard<std::mutex> lk(state->mtx);
        state->res  = r;
        state->done = true;
        state->cv.notify_one();
    });
    worker.detach();

    std::unique_lock<std::mutex> lk(state->mtx);
    bool finished = state->cv.wait_for(lk,
        std::chrono::milliseconds(timeoutMs),
        [&state] { return state->done; });

    if (finished)
        result = state->res;
    // else: timeout — result keeps defaults (success=false, latencyMs=-1)

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP probe
//
// Creates a non-blocking socket, initiates connect(), then uses select() to
// wait up to timeoutMs for the connection to complete or fail.
// ─────────────────────────────────────────────────────────────────────────────

TcpResult NetProbe::probeTcp(const std::string &hostname, uint16_t port, int timeoutMs)
{
    TcpResult result;

    // 1. Resolve hostname
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    struct addrinfo *ai = nullptr;
    if (getaddrinfo(hostname.c_str(), portStr.c_str(), &hints, &ai) != 0 || !ai)
        return result;  // DNS failed

    // 2. Try each address until one connects
    for (struct addrinfo *p = ai; p != nullptr; p = p->ai_next) {
        sock_t s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCK) continue;

        setNonBlocking(s);

        auto t0 = Clock::now();
        int rc = connect(s, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen));

        bool inProgress = (rc != 0) && WOULD_BLOCK(SOCKET_ERRNO);

        if (rc == 0 || inProgress) {
            // Use select() to wait for completion
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(s, &wfds);

            // Also watch error set (for failed connections on some platforms)
            fd_set efds = wfds;

            int remaining = timeoutMs - (int)elapsedMs(t0);
            if (remaining <= 0) {
                SOCK_CLOSE(s);
                continue;
            }

            struct timeval tv;
            tv.tv_sec  = remaining / 1000;
            tv.tv_usec = (remaining % 1000) * 1000;

            int sel = select((int)s + 1, nullptr, &wfds, &efds, &tv);

            if (sel > 0 && FD_ISSET(s, &wfds)) {
                // Verify no socket-level error
                int optval = 0;
                socklen_t optlen = sizeof(optval);
                getsockopt(s, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char *>(&optval), &optlen);
                if (optval == 0) {
                    result.success   = true;
                    result.latencyMs = elapsedMs(t0);
                    SOCK_CLOSE(s);
                    break;
                }
            }
        }

        SOCK_CLOSE(s);
    }

    freeaddrinfo(ai);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-round TCP probe
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TcpResult> NetProbe::probeTcpMulti(const std::string &hostname,
                                                 uint16_t port,
                                                 int rounds,
                                                 int timeoutMs)
{
    std::vector<TcpResult> results;
    results.reserve(rounds);
    for (int i = 0; i < rounds; ++i)
        results.push_back(probeTcp(hostname, port, timeoutMs));
    return results;
}

} // namespace twitch_bench
