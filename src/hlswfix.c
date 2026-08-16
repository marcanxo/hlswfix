/*
 * hlswfix - brings HLSW back to life against modern game servers.
 *
 * HLSW was last built in 2011, and two separate things stop it working today.
 * Neither can be fixed on the server, and neither needs HLSW itself to be
 * altered: this library is injected at startup and redirects a handful of
 * winsock functions in memory, so not one byte of hlsw.exe changes on disk.
 * It is not tied to any particular server or game, and needs no configuration.
 *
 * 1. The A2S_INFO challenge
 *
 * In December 2020 Valve made A2S_INFO require a challenge, so that game
 * servers could no longer be used as UDP reflection amplifiers. The server
 * answers a plain query with a 9 byte S2C_CHALLENGE packet (0x41), and only a
 * repeat of the query carrying those four bytes gets the real 0x49 info reply.
 * HLSW sends the plain query, cannot parse what comes back, and reports every
 * server as timed out. A2S_PLAYER and A2S_RULES always needed a challenge and
 * HLSW performs that handshake correctly, and rcon is TCP and has not changed
 * at all, so this one missing round trip is the whole of it. Source and
 * GoldSrc are affected alike.
 *
 * 2. HLSW's own refresh rate
 *
 * HLSW asks the server it is watching again the instant the previous answer
 * lands: measured at about eighty queries a second for each of info, players
 * and rules. A Source server answers three a second per address and silently
 * drops the rest, so HLSW makes the very server it is watching appear to time
 * out, entirely by itself. Queries are therefore paced; see
 * DEFAULT_QUERY_INTERVAL_MS for the measurements behind the default.
 *
 * How the redirection works
 *
 * The functions themselves are redirected, by writing a jump over the five
 * byte hot patch prologue every one of them begins with. Patching import
 * tables was the first attempt and cannot work here at all, because HLSW
 * resolves winsock at run time and never consults one; it survives only as a
 * fallback for a function that lacks that prologue. See install_detour.
 *
 *   sendto, send            pace the query, and attach an already known
 *                           challenge so that a repeat costs one round trip
 *                           instead of two and the ping stays honest
 *   recvfrom, recv,         spot the 0x41, put the repeated query on the wire
 *   WSARecvFrom, WSARecv    as a side effect, and hand the caller exactly what
 *                           arrived, untouched. Both pairs are needed: HLSW
 *                           uses the plain ones for the burst of probes it
 *                           sends when a server is added, and the WSA ones
 *                           once it settles into monitoring
 *   connect                 optional, diverts rcon for configured servers to a
 *                           local port. Stream sockets only, never the query
 *                           socket
 *   select                  nothing but a control point, see my_select
 *   SetWindowTextW          optional, rewrites the version in the title
 *
 * Configuration is hlswfix.ini next to hlsw.exe. Nothing in it is required.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Room for a full length directory plus one of the file names appended to it,
 * so that none of the paths built below can be cut short. */
#define PATHBUF (MAX_PATH + 32)

/* The query as every browser sends it, without a challenge. 25 bytes. */
static const char A2S_INFO[] =
    "\xFF\xFF\xFF\xFF\x54" "Source Engine Query";
#define A2S_INFO_LEN 25

#define S2C_CHALLENGE   0x41
#define A2S_INFO_REPLY  0x49

#define MAX_ENTRIES   64

/* How often a single server may actually be asked for its info, in
 * milliseconds. HLSW polls the server it is watching as fast as the network
 * answers, which measured at roughly sixty five queries a second: it fires the
 * next A2S_INFO the moment the previous reply lands. Source servers answer at
 * most sv_max_queries_sec, three per second by default, and drop everything
 * beyond that, so HLSW's own refresh rate is what makes the server it is
 * watching go silent. ServerAutoUpdateRate in the registry does not govern
 * this loop; setting it to 1 changed nothing.
 *
 * A query that is held back is simply dropped, with nothing sent in its place.
 * The obvious alternative, answering it from the previous reply, was tried and
 * lies about the ping: HLSW times a query from its own send to the arrival of
 * the answer, and a locally produced answer arrives in well under a
 * millisecond, so every paced server reported 1 to 3 ms while the one being
 * watched, whose queries go out for real, showed its true 26. Dropping leaves
 * the displayed ping as the last genuine measurement, and HLSW does not treat
 * the missing answers as a timeout, because a real one still arrives every
 * interval.
 *
 * One second means three queries a second per server across the three kinds,
 * exactly what a Source server answers without dropping any: measured at that
 * rate, no loss at all over a hundred seconds, and clear losses above it. */
#define DEFAULT_QUERY_INTERVAL_MS 1000

#define MAX_REDIRECTS  8
#define ENTRY_TIMEOUT_MS 120000

/* How long a client challenge request may hold its claim on the next challenge
 * that arrives. Servers answer in milliseconds, so anything beyond this means
 * the answer was lost and the claim has to lapse rather than block the info
 * query for good. */
#define CHALLENGE_CLAIM_MS 3000

/* ------------------------------------------------------------------ state */

typedef struct {
    int    used;
    SOCKET sock;
    ULONG  ip;                    /* network order */
    USHORT port;                  /* network order */
    UCHAR  challenge[4];
    int    have_challenge;
    int    info_pending;          /* an A2S_INFO of ours is unanswered */
    int    client_wants_challenge;/* HLSW asked for one itself */
    DWORD  challenge_asked_at;    /* so a claim that is never answered expires */
    DWORD  last_use;

    /* Pacing: when each kind of query was last allowed out. */
    DWORD  last_info_sent;
    DWORD  last_aux_sent[2];      /* 0 = A2S_PLAYER, 1 = A2S_RULES */
} Entry;

typedef struct {
    ULONG  from_ip;
    USHORT from_port;
    USHORT to_port;               /* on 127.0.0.1 */
} Redirect;

static Entry           g_entries[MAX_ENTRIES];
static Redirect        g_redirects[MAX_REDIRECTS];
static int             g_redirect_count;
static CRITICAL_SECTION g_lock;
static int             g_logging;
static char            g_log_path[PATHBUF];
static HMODULE         g_self;

static int             g_query_interval = DEFAULT_QUERY_INTERVAL_MS;

/* Version to show in the window title instead of the built in one. Empty
 * leaves the title alone, and then the hook is not even installed. */
static wchar_t         g_title_version[32];
static BOOL (WINAPI *real_SetWindowTextW)(HWND, LPCWSTR);

/* The way back to the original behaviour: normally a trampoline left behind by
 * install_detour, or the address that stood in an import table when only the
 * fallback worked. Any of them can be null when that function could not be
 * redirected, so every call site checks the one it is about to use. */
static int (WSAAPI *real_recvfrom)(SOCKET, char *, int, int, struct sockaddr *, int *);
static int (WSAAPI *real_sendto)(SOCKET, const char *, int, int, const struct sockaddr *, int);
static int (WSAAPI *real_recv)(SOCKET, char *, int, int);
static int (WSAAPI *real_send)(SOCKET, const char *, int, int);
static int (WSAAPI *real_connect)(SOCKET, const struct sockaddr *, int);
static int (WSAAPI *real_select)(int, fd_set *, fd_set *, fd_set *, const struct timeval *);

/* The overlapped capable receive pair. HLSW uses these for its steady state
 * monitoring, and plain recvfrom only for the burst of probes it sends when a
 * server is first added. Hooking only the plain one therefore works for about
 * a minute, until the cached challenge expires and the refresh arrives on a
 * path nothing is watching. */
static int (WSAAPI *real_WSARecvFrom)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                                      struct sockaddr *, LPINT, LPWSAOVERLAPPED,
                                      LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static int (WSAAPI *real_WSARecv)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                                  LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

/* ------------------------------------------------------------------- log */

/* Not called logf, because that is a C standard maths function and gcc has a
 * builtin of that name. */
static void dbg_log(const char *fmt, ...)
{
    FILE *fh;
    va_list ap;
    SYSTEMTIME st;

    if (!g_logging)
        return;
    fh = fopen(g_log_path, "a");
    if (!fh)
        return;
    GetLocalTime(&st);
    /* The thread id matters: whether the socket work happens on the user
     * interface thread or on a worker decides whether this library may ever
     * delay a call, or whether delaying would freeze the window. */
    fprintf(fh, "%02d:%02d:%02d.%03d t%-5lu ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, GetCurrentThreadId());
    va_start(ap, fmt);
    vfprintf(fh, fmt, ap);
    va_end(ap);
    fputc('\n', fh);
    fclose(fh);
}

/* Dumps a packet at log level 2. This exists because guessing at what HLSW
 * puts on the wire is how time gets wasted: the exact bytes settle it. */
static void dump_packet(const char *arrow, const struct sockaddr_in *addr,
                        const char *buf, int len)
{
    char hex[3 * 64 + 1], asc[64 + 1];
    int i, n;

    if (g_logging < 2)
        return;
    n = len < 64 ? len : 64;
    for (i = 0; i < n; i++) {
        sprintf(hex + i * 3, "%02x ", (UCHAR)buf[i]);
        asc[i] = ((UCHAR)buf[i] >= 32 && (UCHAR)buf[i] < 127) ? buf[i] : '.';
    }
    hex[n * 3] = 0;
    asc[n] = 0;
    dbg_log("%s %s:%d  len=%d  %s | %s", arrow, inet_ntoa(addr->sin_addr),
            ntohs(addr->sin_port), len, hex, asc);
}

/* ---------------------------------------------------------------- helpers */

static int is_a2s_info(const char *buf, int len)
{
    return len == A2S_INFO_LEN && memcmp(buf, A2S_INFO, A2S_INFO_LEN) == 0;
}

static int is_connectionless(const char *buf, int len)
{
    return len >= 5 && (UCHAR)buf[0] == 0xFF && (UCHAR)buf[1] == 0xFF
                    && (UCHAR)buf[2] == 0xFF && (UCHAR)buf[3] == 0xFF;
}

/* Find or create the record for one socket and one server address. Entries
 * are per socket on purpose: the server ties a challenge to the address it
 * saw it come from, so two sockets must never share one. */
static Entry *entry_get(SOCKET s, const struct sockaddr_in *addr, int create)
{
    int i, oldest = -1;
    DWORD now = GetTickCount();

    for (i = 0; i < MAX_ENTRIES; i++) {
        if (g_entries[i].used && g_entries[i].sock == s
            && g_entries[i].ip == addr->sin_addr.s_addr
            && g_entries[i].port == addr->sin_port) {
            g_entries[i].last_use = now;
            return &g_entries[i];
        }
    }
    if (!create)
        return NULL;

    for (i = 0; i < MAX_ENTRIES; i++) {
        if (!g_entries[i].used || (now - g_entries[i].last_use) > ENTRY_TIMEOUT_MS) {
            oldest = i;
            break;
        }
        if (oldest < 0 || g_entries[i].last_use < g_entries[oldest].last_use)
            oldest = i;
    }
    memset(&g_entries[oldest], 0, sizeof(Entry));
    g_entries[oldest].used = 1;
    g_entries[oldest].sock = s;
    g_entries[oldest].ip = addr->sin_addr.s_addr;
    g_entries[oldest].port = addr->sin_port;
    g_entries[oldest].last_use = now;
    return &g_entries[oldest];
}

/* ------------------------------------------------------------------ hooks */

/* A server can be reached two ways, and HLSW uses both depending on what it is
 * doing: sendto and recvfrom on a loose socket, or send and recv on one it
 * connected first. Both need identical treatment, so the decisions live in
 * these two functions and the four hooks below differ only in which winsock
 * call they pass through to. */

static int is_udp(SOCKET s)
{
    int type = 0, len = sizeof(type);

    if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *)&type, &len) != 0)
        return 0;
    return type == SOCK_DGRAM;
}

/* The address a connected datagram socket is talking to. Deliberately refuses
 * stream sockets: rcon runs over one of those, and its payload must never be
 * mistaken for a query. */
static int udp_peer_of(SOCKET s, struct sockaddr_in *peer)
{
    int len = sizeof(*peer);

    if (!is_udp(s))
        return 0;
    if (getpeername(s, (struct sockaddr *)peer, &len) != 0)
        return 0;
    return peer->sin_family == AF_INET;
}

#define SEND_AS_IS     0
#define SEND_CHALLENGE 1
#define SEND_SUPPRESS  2

/* Decides what to do with a query HLSW wants to send.
 *
 * can_suppress is false on connected sockets. Holding a query back there would
 * strand the reply: the wake up packet comes from loopback and a connected
 * socket accepts nothing but its peer, so HLSW would never be prompted to come
 * and collect it. */
static int outgoing(SOCKET s, const struct sockaddr_in *peer,
                    const char *buf, int len, char *query, int can_suppress)
{
    Entry *e;
    int have = 0;

    if (is_a2s_info(buf, len)) {
        DWORD now = GetTickCount();
        int suppress = 0;

        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 1);

        if (can_suppress && g_query_interval > 0
            && (now - e->last_info_sent) < (DWORD)g_query_interval) {
            suppress = 1;
        } else {
            e->last_info_sent = now;
            e->info_pending = 1;
            have = e->have_challenge;
            if (have) {
                memcpy(query, buf, A2S_INFO_LEN);
                memcpy(query + A2S_INFO_LEN, e->challenge, 4);
            }
        }
        LeaveCriticalSection(&g_lock);

        if (suppress)
            return SEND_SUPPRESS;
        return have ? SEND_CHALLENGE : SEND_AS_IS;
    }

    /* Players and rules get the same pacing. Without it they alone still came
     * to eighty queries a second each, and the rules answer arrives split
     * across a dozen datagrams, so it was by far the largest share of the
     * traffic. */
    if (is_connectionless(buf, len)
        && ((UCHAR)buf[4] == 0x55 || (UCHAR)buf[4] == 0x56)) {
        int idx = ((UCHAR)buf[4] == 0x55) ? 0 : 1;
        DWORD now = GetTickCount();
        int suppress = 0;
        int seeks_challenge = (len >= 9 && (UCHAR)buf[5] == 0xFF && (UCHAR)buf[6] == 0xFF
                                        && (UCHAR)buf[7] == 0xFF && (UCHAR)buf[8] == 0xFF);

        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 1);
        if (can_suppress && g_query_interval > 0
            && (now - e->last_aux_sent[idx]) < (DWORD)g_query_interval) {
            suppress = 1;
        } else {
            e->last_aux_sent[idx] = now;
            if (seeks_challenge) {
                e->client_wants_challenge = 1;
                e->challenge_asked_at = now;
            }
        }
        LeaveCriticalSection(&g_lock);
        return suppress ? SEND_SUPPRESS : SEND_AS_IS;
    }

    /* Only a query that actually asks for a challenge earns one: A2S_PLAYER and
     * A2S_RULES carrying the placeholder -1, or the bare challenge request.
     *
     * The same test on the packet type alone is wrong, and was the bug that
     * made this work for about a minute and then never again. Once HLSW has a
     * challenge it sends A2S_PLAYER with the real value and gets player data
     * back, not a challenge. Marking that as "the client is waiting for a
     * challenge" left the claim standing forever, and from then on every
     * challenge meant for A2S_INFO was handed to HLSW instead of being used,
     * so the info query could never complete again. */
    if (is_connectionless(buf, len)
        && (((UCHAR)buf[4] == 0x55 || (UCHAR)buf[4] == 0x56)
            ? (len >= 9 && (UCHAR)buf[5] == 0xFF && (UCHAR)buf[6] == 0xFF
                         && (UCHAR)buf[7] == 0xFF && (UCHAR)buf[8] == 0xFF)
            : (UCHAR)buf[4] == 0x57)) {
        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 1);
        e->client_wants_challenge = 1;
        e->challenge_asked_at = GetTickCount();
        LeaveCriticalSection(&g_lock);
    }
    return SEND_AS_IS;
}

/* Returns 1 when what was just read is the challenge answering our own query.
 * The caller then has to repeat `query` and read again. */
static int incoming(SOCKET s, const struct sockaddr_in *peer,
                    const char *buf, int rc, char *query)
{
    Entry *e;
    int repeat = 0;

    if (!(is_connectionless(buf, rc) && (UCHAR)buf[4] == S2C_CHALLENGE)) {
        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 0);
        if (e) {
            /* Any reply that is not a challenge settles what was pending. */
            e->client_wants_challenge = 0;
            if ((UCHAR)buf[4] == A2S_INFO_REPLY)
                e->info_pending = 0;
        }
        LeaveCriticalSection(&g_lock);
        return 0;
    }

    EnterCriticalSection(&g_lock);
    e = entry_get(s, peer, 1);
    memcpy(e->challenge, buf + 5, 4);
    e->have_challenge = 1;
    if (e->client_wants_challenge
        && (GetTickCount() - e->challenge_asked_at) < CHALLENGE_CLAIM_MS) {
        /* HLSW asked for this one. Losing it would cost it the player list, so
         * the client is always served first. The age check makes sure a claim
         * whose answer never arrived cannot keep doing that indefinitely. */
        e->client_wants_challenge = 0;
    } else if (e->info_pending) {
        e->info_pending = 0;
        repeat = 1;
        memcpy(query, A2S_INFO, A2S_INFO_LEN);
        memcpy(query + A2S_INFO_LEN, e->challenge, 4);
    }
    LeaveCriticalSection(&g_lock);
    return repeat;
}

static int WSAAPI my_sendto(SOCKET s, const char *buf, int len, int flags,
                            const struct sockaddr *to, int tolen)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)to;
    char query[A2S_INFO_LEN + 4];
    int rc;

    if (!to || tolen < (int)sizeof(struct sockaddr_in) || to->sa_family != AF_INET)
        return real_sendto(s, buf, len, flags, to, tolen);

    dump_packet("SENDTO ->", sin, buf, len);

    switch (outgoing(s, sin, buf, len, query, 1)) {
    case SEND_SUPPRESS:
        /* Held back. The caller is told it went out, and gets the previous
         * answer instead of a fresh one. */
        return len;
    case SEND_CHALLENGE:
        rc = real_sendto(s, query, sizeof(query), flags, to, tolen);
        /* Report the length the caller handed us, not the longer one that
         * actually went out, so its own accounting stays consistent. */
        return rc == (int)sizeof(query) ? len : rc;
    default:
        return real_sendto(s, buf, len, flags, to, tolen);
    }
}

static int WSAAPI my_send(SOCKET s, const char *buf, int len, int flags)
{
    struct sockaddr_in peer;
    char query[A2S_INFO_LEN + 4];
    int rc;

    if (!udp_peer_of(s, &peer))
        return real_send(s, buf, len, flags);

    dump_packet("SEND ->", &peer, buf, len);

    if (outgoing(s, &peer, buf, len, query, 0) != SEND_CHALLENGE)
        return real_send(s, buf, len, flags);

    rc = real_send(s, query, sizeof(query), flags);
    return rc == (int)sizeof(query) ? len : rc;
}

/* The receive hooks never swallow a packet, never wait, and never invent an
 * error. They hand the caller exactly what arrived and, on the side, put the
 * repeated query on the wire; its answer is then read by the caller's next
 * ordinary read.
 *
 * The obvious alternative, swallowing the challenge and blocking until the
 * real answer arrives, is what broke this. It looks tidier, because HLSW then
 * never sees the extra packet at all, but it has to report something when the
 * wait runs out, and WSAEWOULDBLOCK on a blocking socket is not an answer any
 * caller expects. HLSW's receive loop gave up for good on the first one, while
 * its send timer carried on. The symptom was both monitored servers falling
 * silent in the same instant, which no server-side explanation fits.
 *
 * Each of them checks the send function it is about to use. The hooks are
 * installed one by one and independently, so a receive hook can be live while
 * the send it needs for the repeat was never redirected, and the check at the
 * end of DllMain does not help: it reports whether any usable pair exists at
 * all, which it still does when the other pair is intact. */
static int WSAAPI my_recvfrom(SOCKET s, char *buf, int len, int flags,
                              struct sockaddr *from, int *fromlen)
{
    struct sockaddr_in *sin;
    char query[A2S_INFO_LEN + 4];
    int rc = real_recvfrom(s, buf, len, flags, from, fromlen);

    if (rc < 0 || !from || from->sa_family != AF_INET)
        return rc;

    sin = (struct sockaddr_in *)from;
    dump_packet("RECVFROM <-", sin, buf, rc);

    if (rc >= 9 && real_sendto && incoming(s, sin, buf, rc, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(sin->sin_addr));
        real_sendto(s, query, sizeof(query), 0, from, sizeof(struct sockaddr_in));
    }
    return rc;
}

static int WSAAPI my_recv(SOCKET s, char *buf, int len, int flags)
{
    struct sockaddr_in peer;
    char query[A2S_INFO_LEN + 4];
    int rc;

    if (!udp_peer_of(s, &peer))
        return real_recv(s, buf, len, flags);

    rc = real_recv(s, buf, len, flags);
    if (rc < 0)
        return rc;

    dump_packet("RECV <-", &peer, buf, rc);

    if (rc >= 9 && real_send && incoming(s, &peer, buf, rc, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(peer.sin_addr));
        real_send(s, query, sizeof(query), 0);
    }
    return rc;
}

static int WSAAPI my_WSARecvFrom(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD recvd,
                                 LPDWORD flags, struct sockaddr *from, LPINT fromlen,
                                 LPWSAOVERLAPPED ov,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    char query[A2S_INFO_LEN + 4];
    struct sockaddr_in *sin;
    int rc;

    /* Overlapped calls complete later and somewhere else entirely, so there is
     * nothing to inspect here and nothing safe to do. */
    if (ov || routine || !bufs || count < 1 || !recvd || !from || !fromlen)
        return real_WSARecvFrom(s, bufs, count, recvd, flags, from, fromlen, ov, routine);

    rc = real_WSARecvFrom(s, bufs, count, recvd, flags, from, fromlen, ov, routine);
    if (rc != 0 || from->sa_family != AF_INET)
        return rc;

    sin = (struct sockaddr_in *)from;
    dump_packet("WSARECVFROM <-", sin, bufs[0].buf, (int)*recvd);

    if ((int)*recvd >= 9 && real_sendto
        && incoming(s, sin, bufs[0].buf, (int)*recvd, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(sin->sin_addr));
        real_sendto(s, query, sizeof(query), 0, from, sizeof(struct sockaddr_in));
    }
    return rc;
}

static int WSAAPI my_WSARecv(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD recvd,
                             LPDWORD flags, LPWSAOVERLAPPED ov,
                             LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    struct sockaddr_in peer;
    char query[A2S_INFO_LEN + 4];
    int rc;

    if (ov || routine || !bufs || count < 1 || !recvd || !udp_peer_of(s, &peer))
        return real_WSARecv(s, bufs, count, recvd, flags, ov, routine);

    rc = real_WSARecv(s, bufs, count, recvd, flags, ov, routine);
    if (rc != 0)
        return rc;

    dump_packet("WSARECV <-", &peer, bufs[0].buf, (int)*recvd);

    if ((int)*recvd >= 9 && real_send
        && incoming(s, &peer, bufs[0].buf, (int)*recvd, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(peer.sin_addr));
        real_send(s, query, sizeof(query), 0);
    }
    return rc;
}

/* Rewrites the version in the window title, which HLSW builds from its own
 * version resource. Done here rather than by editing that resource, so the
 * program's own files stay exactly as they were shipped and the change travels
 * with this library instead of with a modified executable. */
static BOOL WINAPI my_SetWindowTextW(HWND hwnd, LPCWSTR text)
{
    wchar_t buf[512];
    const wchar_t *rest;

    if (!text || wcsncmp(text, L"HLSW v", 6) != 0)
        return real_SetWindowTextW(hwnd, text);

    rest = text + 6;
    while (*rest && ((*rest >= L'0' && *rest <= L'9') || *rest == L'.'))
        rest++;

    buf[0] = 0;
    wcsncat(buf, L"HLSW v", 500);
    wcsncat(buf, g_title_version, 500 - wcslen(buf));
    wcsncat(buf, rest, 500 - wcslen(buf));
    return real_SetWindowTextW(hwnd, buf);
}

/* Purely a control point. If this never fires, the import patching is not
 * reaching the code that actually runs, and no amount of work on the query
 * logic will help. HLSW cannot poll its sockets without calling this. */
static int WSAAPI my_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex,
                            const struct timeval *tv)
{
    static LONG seen = 0;
    LONG n = InterlockedIncrement(&seen);

    if (n <= 3)
        dbg_log("select called (%ld)", n);
    return real_select(nfds, rd, wr, ex, tv);
}

static int WSAAPI my_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)name;
    int i;

    if (!name || namelen < (int)sizeof(struct sockaddr_in) || name->sa_family != AF_INET)
        return real_connect(s, name, namelen);

    dump_packet(is_udp(s) ? "CONNECT udp" : "CONNECT tcp", sin, "", 0);

    /* Stream sockets only. HLSW connects its query socket as well, and sending
     * that one down the rcon tunnel would break exactly the thing this library
     * exists to fix. */
    if (is_udp(s))
        return real_connect(s, name, namelen);

    for (i = 0; i < g_redirect_count; i++) {
        if (sin->sin_addr.s_addr == g_redirects[i].from_ip
            && sin->sin_port == g_redirects[i].from_port) {
            struct sockaddr_in local;

            memset(&local, 0, sizeof(local));
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons(g_redirects[i].to_port);
            dbg_log("rcon to %s:%d redirected to 127.0.0.1:%d",
                 inet_ntoa(sin->sin_addr), ntohs(sin->sin_port), g_redirects[i].to_port);
            return real_connect(s, (const struct sockaddr *)&local, sizeof(local));
        }
    }
    return real_connect(s, name, namelen);
}

/* --------------------------------------------------------- import patching */

/* Replaces one entry in one module's import table and returns what stood
 * there, which is the real function.
 *
 * Matching is done three ways because different modules import differently:
 * hlsw.exe imports winsock by ordinal, MFC imports it by name, and a module
 * with a bound import table gives neither, so the resolved address is the last
 * resort. */
static void *patch_import_in(HMODULE module, const char *dll, WORD ordinal,
                             const char *func, void *replacement)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    DWORD rva;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    void *want = NULL;
    HMODULE mod;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return NULL;

    mod = GetModuleHandleA(dll);
    if (mod)
        want = (void *)GetProcAddress(mod, func);

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); imp->Name; imp++) {
        IMAGE_THUNK_DATA *oft, *ft;
        int i;

        if (_stricmp((const char *)(base + imp->Name), dll) != 0)
            continue;

        oft = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk) : NULL;
        ft = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);

        for (i = 0; ft[i].u1.Function; i++) {
            int match = 0;

            if (oft && oft[i].u1.Ordinal) {
                if (IMAGE_SNAP_BY_ORDINAL32(oft[i].u1.Ordinal)) {
                    if (IMAGE_ORDINAL32(oft[i].u1.Ordinal) == ordinal)
                        match = 1;
                } else {
                    IMAGE_IMPORT_BY_NAME *n =
                        (IMAGE_IMPORT_BY_NAME *)(base + oft[i].u1.AddressOfData);
                    if (strcmp((const char *)n->Name, func) == 0)
                        match = 1;
                }
            }
            if (!match && want && (void *)ft[i].u1.Function == want)
                match = 1;

            if (match) {
                void *old = (void *)ft[i].u1.Function;
                DWORD prot;

                /* Never chain onto ourselves, that would recurse forever. */
                if (old == replacement)
                    return NULL;
                if (!VirtualProtect(&ft[i].u1.Function, sizeof(void *), PAGE_READWRITE, &prot))
                    return NULL;
                ft[i].u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&ft[i].u1.Function, sizeof(void *), prot, &prot);
                return old;
            }
        }
    }
    return NULL;
}

/* Patches the import table of every module in the process, not just the
 * executable, and is only the fallback for a function that cannot be
 * redirected directly. See install_detour for why that is the weaker route.
 *
 * It reaches every module because import tables are per module, so a hook
 * placed only in hlsw.exe sits there fully installed and is never called if
 * the caller lives elsewhere. That was a real dead end here, though not for
 * the reason first assumed: the suspicion was MFC's CAsyncSocket calling
 * winsock through mfc90u.dll, and that turned out to be wrong. mfc90u.dll
 * imports no winsock at all. HLSW resolves the functions at run time and never
 * consults an import table, which is why patching them cannot work for it and
 * the detours exist.
 *
 * Both winsock libraries are tried, because different modules import from
 * either one. */

/* The winsock libraries themselves must never be patched, only their users.
 * WSOCK32 carries its own recv and recvfrom that call through to WS2_32, so
 * patching inside it would make a hook reach its own replacement and recurse
 * until the stack runs out. The address of recvfrom really does land in a
 * different module than the address of sendto, which is what gives this away. */
static int is_winsock_module(const char *name)
{
    return _stricmp(name, "WSOCK32.dll") == 0
        || _stricmp(name, "WS2_32.dll") == 0
        || _stricmp(name, "MSWSOCK.dll") == 0;
}

/* Reports which winsock functions a module imports, and how. A hook that is
 * installed and never called looks identical to one that failed to install,
 * and the only way to tell them apart is to know which module the caller lives
 * in and under what name or ordinal it asks for the function. */
static void log_winsock_imports(HMODULE module, const char *modname)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    DWORD rva;
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); imp->Name; imp++) {
        const char *dll = (const char *)(base + imp->Name);
        IMAGE_THUNK_DATA *oft, *ft;
        char line[512];
        int i, n = 0;

        if (_stricmp(dll, "WSOCK32.dll") != 0 && _stricmp(dll, "WS2_32.dll") != 0
            && _stricmp(dll, "MSWSOCK.dll") != 0)
            continue;

        oft = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk) : NULL;
        ft = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        line[0] = 0;

        for (i = 0; ft[i].u1.Function && n < 400; i++) {
            char item[64];

            if (oft && oft[i].u1.Ordinal) {
                if (IMAGE_SNAP_BY_ORDINAL32(oft[i].u1.Ordinal))
                    sprintf(item, "#%lu ", (unsigned long)IMAGE_ORDINAL32(oft[i].u1.Ordinal));
                else {
                    IMAGE_IMPORT_BY_NAME *nm =
                        (IMAGE_IMPORT_BY_NAME *)(base + oft[i].u1.AddressOfData);
                    sprintf(item, "%.40s ", (const char *)nm->Name);
                }
            } else {
                sprintf(item, "@%p ", (void *)ft[i].u1.Function);
            }
            n += (int)strlen(item);
            strncat(line, item, sizeof(line) - strlen(line) - 1);
        }
        dbg_log("  %s imports from %s: %s", modname, dll, line);
    }
}

/* Lists what is actually loaded, and who talks to winsock. */
static void log_modules(void)
{
    HANDLE snap;
    MODULEENTRY32 me;

    if (g_logging < 1)
        return;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        dbg_log("module snapshot failed, error %lu", GetLastError());
        return;
    }
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            log_winsock_imports(me.hModule, me.szModule);
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
}

static void *patch_everywhere(WORD ordinal, const char *func, void *replacement)
{
    HANDLE snap;
    MODULEENTRY32 me;
    void *real = NULL;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return NULL;

    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            void *old;

            if (me.hModule == g_self || is_winsock_module(me.szModule))
                continue;
            old = patch_import_in(me.hModule, "WSOCK32.dll", ordinal, func, replacement);
            if (!old)
                old = patch_import_in(me.hModule, "WS2_32.dll", ordinal, func, replacement);
            if (old) {
                if (!real)
                    real = old;
                if (g_logging)
                    dbg_log("  patched %s in %s", func, me.szModule);
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return real;
}

/* ---------------------------------------------------------------- detours */

#define MAX_DETOURS 16
static void *g_detoured[MAX_DETOURS];
static int   g_detour_count;

static int already_detoured(void *fn)
{
    int i;

    for (i = 0; i < g_detour_count; i++)
        if (g_detoured[i] == fn)
            return 1;
    return 0;
}

/* Redirects a function by overwriting its first five bytes with a jump, and
 * returns a trampoline that still does the original work.
 *
 * This is what patching import tables could not do, however correctly it was
 * done. HLSW reaches winsock through pointers resolved at run time, so its
 * import table is simply never consulted: the patched entries were verified to
 * be live in memory and not one of them was ever called, while the traffic was
 * plainly going out. Overwriting the function itself catches every caller no
 * matter how it found the address.
 *
 * Only the Microsoft hot patch prologue is accepted. "mov edi,edi; push ebp;
 * mov ebp,esp" is exactly five bytes, exactly what a jmp rel32 needs, so no
 * instruction can be cut in half. Every function needed here begins with it.
 * Anything that does not is left alone rather than guessed at. */
static void *install_detour(void *target, void *replacement)
{
    BYTE *t = (BYTE *)target;
    BYTE *tramp;
    DWORD prot;

    if (!t || already_detoured(t) || g_detour_count >= MAX_DETOURS)
        return NULL;
    if (!(t[0] == 0x8B && t[1] == 0xFF && t[2] == 0x55 && t[3] == 0x8B && t[4] == 0xEC))
        return NULL;

    tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp)
        return NULL;

    /* The five displaced bytes, then a jump back to the rest of the function. */
    memcpy(tramp, t, 5);
    tramp[5] = 0xE9;
    *(DWORD *)(tramp + 6) = (DWORD)(ULONG_PTR)(t + 5) - (DWORD)(ULONG_PTR)(tramp + 10);

    if (!VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &prot)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return NULL;
    }
    t[0] = 0xE9;
    *(DWORD *)(t + 1) = (DWORD)(ULONG_PTR)replacement - (DWORD)(ULONG_PTR)(t + 5);
    VirtualProtect(t, 5, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), t, 5);

    g_detoured[g_detour_count++] = t;
    return tramp;
}

/* Both winsock libraries carry these names. WSOCK32 forwards several of them
 * straight into WS2_32, so the second attempt often sees an address that has
 * already been redirected and skips it. Where WSOCK32 has its own
 * implementation, as it does for recv and recvfrom, both get redirected. */
static void *detour_in(const char *dll, const char *func, void *replacement)
{
    HMODULE m = GetModuleHandleA(dll);
    void *fn = m ? (void *)GetProcAddress(m, func) : NULL;
    void *tramp = fn ? install_detour(fn, replacement) : NULL;

    if (tramp && g_logging)
        dbg_log("  detoured %s in %-11s entry %p", func, dll, fn);
    return tramp;
}

static void *detour_api(const char *func, void *replacement)
{
    static const char *dlls[2] = { "WS2_32.dll", "WSOCK32.dll" };
    void *real = NULL;
    int i;

    for (i = 0; i < 2; i++) {
        void *tramp = detour_in(dlls[i], func, replacement);

        if (tramp && !real)
            real = tramp;
    }
    return real;
}

/* ------------------------------------------------------------------ config */

static void trim(char *s)
{
    char *p = s + strlen(s);
    while (p > s && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t'))
        *--p = 0;
}

static void load_config(HMODULE self)
{
    char path[MAX_PATH], line[512], *p;
    FILE *fh;
    DWORD n;

    n = GetModuleFileNameA(self, path, MAX_PATH);
    if (!n)
        return;
    p = strrchr(path, '\\');
    if (!p)
        return;
    *(p + 1) = 0;
    snprintf(g_log_path, sizeof(g_log_path), "%shlswfix.log", path);
    strncat(path, "hlswfix.ini", sizeof(path) - strlen(path) - 1);

    fh = fopen(path, "r");
    if (!fh)
        return;

    while (fgets(line, sizeof(line), fh)) {
        char ip[64];
        unsigned int from_port, to_port;

        p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == 0)
            continue;
        trim(p);

        if (sscanf(p, "rcon_redirect = %63[0-9.] : %u -> %u",
                   ip, &from_port, &to_port) == 3
            && g_redirect_count < MAX_REDIRECTS) {
            g_redirects[g_redirect_count].from_ip = inet_addr(ip);
            g_redirects[g_redirect_count].from_port = htons((USHORT)from_port);
            g_redirects[g_redirect_count].to_port = (USHORT)to_port;
            g_redirect_count++;
        } else if (strncmp(p, "title_version", 13) == 0 && strchr(p, '=')) {
            char *v = strchr(p, '=') + 1;

            while (*v == ' ' || *v == '\t')
                v++;
            if (*v)
                MultiByteToWideChar(CP_ACP, 0, v, -1, g_title_version,
                                    sizeof(g_title_version) / sizeof(wchar_t));
        } else if (sscanf(p, "query_interval_ms = %u", &from_port) == 1) {
            g_query_interval = (int)from_port;
        } else if (sscanf(p, "log = %u", &from_port) == 1) {
            /* Kept as a level, not squeezed into a flag. Clamping this to 0 or
             * 1 silently disabled the packet dump at level 2 and turned the
             * one instrument that could show what HLSW puts on the wire into
             * something that always reported nothing at all. */
            g_logging = (int)from_port;
        }
    }
    fclose(fh);
}

/* ------------------------------------------------------------------ attach */

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_lock);
    g_self = (HMODULE)inst;
    load_config(inst);

    log_modules();

    /* Redirect the functions themselves. Both the connected and the
     * unconnected pair are taken, because HLSW uses both depending on what it
     * is doing.
     *
     * Patching import tables is kept only as a fallback, for the case where a
     * function does not begin with the hot patch prologue and so cannot be
     * redirected safely. It is a genuinely weaker mechanism here: it reaches
     * only callers that go through an import table, and HLSW does not.
     * Winsock ordinals: connect 4, recv 16, recvfrom 17, send 19, sendto 20,
     * select 18. */
    real_recvfrom = detour_api("recvfrom", (void *)my_recvfrom);
    if (!real_recvfrom)
        real_recvfrom = patch_everywhere(17, "recvfrom", (void *)my_recvfrom);

    real_sendto = detour_api("sendto", (void *)my_sendto);
    if (!real_sendto)
        real_sendto = patch_everywhere(20, "sendto", (void *)my_sendto);

    real_recv = detour_api("recv", (void *)my_recv);
    if (!real_recv)
        real_recv = patch_everywhere(16, "recv", (void *)my_recv);

    real_send = detour_api("send", (void *)my_send);
    if (!real_send)
        real_send = patch_everywhere(19, "send", (void *)my_send);

    real_connect = detour_api("connect", (void *)my_connect);
    if (!real_connect)
        real_connect = patch_everywhere(4, "connect", (void *)my_connect);

    /* The receive pair HLSW actually uses once it is monitoring rather than
     * probing. Without these the cached challenge can never be refreshed, so
     * everything works until the first one expires and then never again. */
    real_WSARecvFrom = detour_api("WSARecvFrom", (void *)my_WSARecvFrom);
    real_WSARecv     = detour_api("WSARecv",     (void *)my_WSARecv);

    /* Kept as a control point: if select never fires, nothing is reaching the
     * hooks and there is no point looking at the query logic. */
    real_select = detour_api("select", (void *)my_select);

    /* Cosmetic, and only when asked for. */
    if (g_title_version[0])
        real_SetWindowTextW = detour_in("USER32.dll", "SetWindowTextW",
                                        (void *)my_SetWindowTextW);

    dbg_log("attached: recvfrom=%p sendto=%p recv=%p send=%p connect=%p, %d rcon redirect(s)",
         (void *)real_recvfrom, (void *)real_sendto, (void *)real_recv,
         (void *)real_send, (void *)real_connect, g_redirect_count);

    /* True when neither pair came out complete, so there is no route left by
     * which a query can be repeated and the fix cannot work at all.
     *
     * This says nothing about safety. An incomplete pair is harmless here only
     * because every hook checks the function it forwards to; the check that
     * keeps a missing one from being called through is at the call site, not
     * in this condition. */
    if (!((real_recvfrom && real_sendto) || (real_recv && real_send))) {
        MessageBoxA(NULL,
                    "hlswfix could not redirect the winsock functions it needs.\r\n"
                    "HLSW will run, but servers will show as timed out.",
                    "hlswfix", MB_OK | MB_ICONWARNING);
    }
    return TRUE;
}
