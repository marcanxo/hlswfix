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
#include <stdlib.h>
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

/* The same answer in the format GoldSrc used before the Source query protocol
 * existed. Some servers still send it, and a few send both for a single query.
 * Measured on one of those: the old answer after 14 ms, the modern one after
 * 15 ms, on every query without exception. */
#define A2S_INFO_REPLY_OLD 0x6D

#define MAX_ENTRIES   64

/* How often a single server may actually be asked for its info, in
 * milliseconds. HLSW polls the server it is watching as fast as the network
 * answers, which measured at sixty five to eighty queries a second depending
 * on the server: it fires the next A2S_INFO the moment the previous reply
 * lands. Source servers answer at
 * most sv_max_queries_sec, three per second by default, and drop everything
 * beyond that, so HLSW's own refresh rate is what makes the server it is
 * watching go silent. ServerAutoUpdateRate in the registry does not govern
 * this loop; setting it to 1 changed nothing.
 *
 * A query that is held back is delayed, not dropped: it is kept and sent when
 * its window opens, see pump_deferred. Dropping it was the first attempt and
 * was wrong, because of the detail above. HLSW asks again the moment an answer
 * arrives, and while it is waiting for one it sends nothing at all for about
 * two seconds. A dropped query therefore does not cost one refresh, it costs
 * that entire deadline, and every server the user was not looking at sat in
 * that state permanently and was painted as timed out. Measured in a rig that
 * asks the way HLSW asks: 4 answers out of 8 with the query dropped, alternating
 * a 2.5 second timeout with an answer that arrives instantly because it is the
 * stale one from the previous window. With the query delayed: 8 out of 8.
 *
 * The cost is the ping HLSW displays. It times a query from its own sendto to
 * the arrival of the answer, so a query delayed by most of a second is reported
 * as most of a second. Nothing avoids both: HLSW starts its own stopwatch and
 * asks again immediately, so the choice is between a wrong number in the ping
 * column and a server shown as unreachable when it is not.
 *
 * Answering the query locally instead, from the previous reply, was also tried.
 * It lies about the ping in the other direction, reporting 1 to 3 ms because
 * that is genuinely how long a locally produced answer takes, and it makes HLSW
 * spin: it asks again the instant it is answered, so an instant answer is an
 * invitation to loop.
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

/* How long a modern info reply counts as proof that a server sends both
 * formats. Short on purpose. A server that stops sending the modern answer,
 * because it was reconfigured or downgraded, has to become fully visible again
 * quickly, and any server being watched at all is asked far more often than
 * this, so the proof is renewed long before it lapses. */
#define BOTH_FORMATS_MS 10000

/* The three kinds of query that are paced, and the longest one of them: an
 * A2S_INFO with a challenge appended comes to 29 bytes. */
#define KIND_INFO   0
#define KIND_PLAYER 1
#define KIND_RULES  2
#define MAX_QUERY   32

/* How often the held back queries are looked at. Small enough that a query
 * goes out close to the moment its window opens, large enough to be free. */
#define PUMP_TICK_MS 25

/* After this many info queries in a row with no answer at all, a server that we
 * had been appending a challenge to is given the benefit of the doubt and asked
 * plainly again. Servers do change their minds: one that answered the challenged
 * form all afternoon stopped answering anything but the bare 25 byte query, and
 * without this the fix would have kept talking to it in a language it had just
 * stopped understanding, forever. */
#define INFO_MISS_LIMIT 3

/* ------------------------------------------------------------------ state */

typedef struct {
    int    used;
    SOCKET sock;
    ULONG  ip;                    /* network order */
    USHORT port;                  /* network order */
    UCHAR  challenge[4];
    int    have_challenge;
    int    info_needs_challenge;  /* proven, by this server refusing without one */
    int    info_misses;           /* info queries in a row that went unanswered */
    int    info_pending;          /* an A2S_INFO of ours is unanswered */
    int    client_wants_challenge;/* HLSW asked for one itself */
    DWORD  challenge_asked_at;    /* so a claim that is never answered expires */
    DWORD  last_modern_info;      /* when this server last answered 0x49 */
    DWORD  last_use;

    /* Pacing, one slot per kind of query. A query that arrives too soon is not
     * thrown away but kept here and sent when its window opens, which is the
     * whole difference between pacing HLSW and blinding it. */
    DWORD  last_sent[3];          /* when each kind last went out for real */
    char   pending[3][MAX_QUERY]; /* the query waiting for its window */
    int    pending_len[3];
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

/* On by default. HLSW sends a ten byte packet to s9b.hlsw.org every five
 * seconds, forever, and gets nothing back; those domains are still registered
 * and can change hands at any time, so a program from 2011 keeps reporting to
 * whoever holds them now. Nothing HLSW does for you needs them. */
static int             g_block_home = 1;

/* On by default. The login screen wants an account on servers that stopped
 * answering years ago, and it is the first thing a fresh install shows. */
static int             g_skip_login = 1;

/* What HLSW is told about a query the pacing is holding back.
 *
 * 0, the default, reports it as sent and really sends it a fraction of a second
 * later. HLSW starts its stopwatch at the report, so the delay lands in the
 * ping it displays.
 *
 * 1 reports that the send failed and does not keep the query. HLSW then knows
 * nothing is on its way, so it cannot be waiting for an answer that will not
 * come, and whatever it sends next goes out at once and is timed honestly. It
 * is off by default because nobody knows how HLSW reacts to a refused send: it
 * might retry sensibly, it might retry in a tight loop, it might mark the
 * server as failed. The only way to find out is to try it, which is what the
 * switch is for. */
static int             g_refuse_held;

/* On by default. A handful of servers answer one A2S_INFO twice, once in the
 * old GoldSrc format and once in the modern one. HLSW understands both and
 * shows whichever landed last, and the two disagree on exactly the fields it
 * reads from: the old answer carries no application id and reports protocol 47
 * where the modern one reports 48. The result is a game icon and a version
 * string that flip back and forth for as long as the server is selected. */
static int             g_hide_duplicate_info = 1;

#define MAX_HOME_IPS 8
static ULONG           g_home_ips[MAX_HOME_IPS];
static int             g_home_ip_count;

/* Defined further down, next to the rest of the calling home business, but
 * needed by the send hook above it. */
static int is_home_address(const struct sockaddr_in *a);

/* Version to show in the window title instead of the built in one. Seeded from
 * this library's own version resource, so it is correct after an update with
 * nobody having to maintain it. hlswfix.ini can override it, and an override
 * with nothing after the equals sign leaves HLSW's own title alone, in which
 * case the hook is not even installed. */
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
static struct hostent *(WSAAPI *real_gethostbyname)(const char *);
static int (WSAAPI *real_getaddrinfo)(const char *, const char *,
                                      const struct addrinfo *, struct addrinfo **);
static HANDLE (WSAAPI *real_WSAAsyncGetHostByName)(HWND, unsigned int, const char *,
                                                   char *, int);

/* The overlapped capable receive pair, hooked for completeness rather than
 * because HLSW needs it. An earlier note here claimed HLSW settles into these
 * once it is monitoring; a 28 minute packet log says otherwise. Every one of
 * the 55,000 lines in it is sendto or recvfrom, on a single thread, and there
 * is not one WSARecvFrom, WSARecv, send, recv or connect among them. They stay
 * hooked because another build or another path might use them and the cost is
 * nothing, but nothing here depends on them. */
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
 * can_suppress is false on connected sockets, and stays that way. The old
 * reason given here, that a held query would strand a wake up packet from
 * loopback, belonged to an abandoned design and no longer means anything now
 * that a held query is really sent a moment later. The honest reason is that
 * HLSW never uses a connected socket for queries, so this path is untested in
 * practice and is left alone rather than paced on a guess. */
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
            && (now - e->last_sent[KIND_INFO]) < (DWORD)g_query_interval) {
            /* Kept, not discarded. HLSW does not ask again while it believes an
             * answer is on its way, so a query thrown away here costs it a
             * whole deadline of silence rather than one refresh. */
            suppress = 1;
            /* Not kept when the send is being refused: HLSW will send its own
             * again, and holding a copy too would put the query out twice. */
            if (!g_refuse_held) {
                memcpy(e->pending[KIND_INFO], buf, A2S_INFO_LEN);
                e->pending_len[KIND_INFO] = A2S_INFO_LEN;
                if (e->have_challenge && e->info_needs_challenge) {
                    memcpy(e->pending[KIND_INFO] + A2S_INFO_LEN, e->challenge, 4);
                    e->pending_len[KIND_INFO] = A2S_INFO_LEN + 4;
                }
            }
        } else {
            /* Nothing came back for the last one. Enough of those in a row and
             * the assumption that this server wants a challenge on its info
             * query is dropped, so the next one is asked plainly. */
            if (e->info_pending && ++e->info_misses >= INFO_MISS_LIMIT) {
                if (e->info_needs_challenge)
                    dbg_log("%s stopped answering the challenged info query, asking plainly again",
                            inet_ntoa(peer->sin_addr));
                e->info_needs_challenge = 0;
                e->have_challenge = 0;
                e->info_misses = 0;
            }
            e->last_sent[KIND_INFO] = now;
            e->info_pending = 1;
            e->pending_len[KIND_INFO] = 0;
            /* Only a server that has actually refused a plain A2S_INFO gets one
             * appended. Sending it to a server that never asked is how a server
             * that was perfectly happy goes silent: it is four bytes of rubbish
             * on the end of a query it already understood. */
            have = e->have_challenge && e->info_needs_challenge;
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
        int idx = ((UCHAR)buf[4] == 0x55) ? KIND_PLAYER : KIND_RULES;
        DWORD now = GetTickCount();
        int suppress = 0;
        int seeks_challenge = (len >= 9 && (UCHAR)buf[5] == 0xFF && (UCHAR)buf[6] == 0xFF
                                        && (UCHAR)buf[7] == 0xFF && (UCHAR)buf[8] == 0xFF);

        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 1);
        if (can_suppress && g_query_interval > 0
            && (now - e->last_sent[idx]) < (DWORD)g_query_interval
            && len <= MAX_QUERY) {
            /* Held the same way, and the claim on the next challenge is staked
             * when it actually goes out rather than now, so a claim cannot
             * stand for a query that is still sitting here. */
            suppress = 1;
            if (!g_refuse_held) {
                memcpy(e->pending[idx], buf, len);
                e->pending_len[idx] = len;
            }
        } else {
            e->last_sent[idx] = now;
            e->pending_len[idx] = 0;
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

/* Puts the held back queries on the wire once their window has opened.
 *
 * This exists because of how HLSW polls. It sends the next query the instant
 * the previous answer arrives, and while it believes a query is outstanding it
 * sends nothing at all and waits about two seconds for an answer. So a query
 * that is quietly dropped does not cost one refresh, it costs that whole
 * deadline, and every server the user is not currently looking at sat
 * permanently in that state and was painted as timed out. Measured before this
 * existed: one real query every 2.04 s per server, with an answer HLSW was
 * waiting for outstanding for 2.03 s of it.
 *
 * The queries are therefore delayed rather than discarded. The rate on the wire
 * is exactly the configured budget either way, but HLSW always gets its answer.
 * The price is the ping it displays: it starts its own stopwatch when it calls
 * sendto, so a query delayed by most of a second is reported as most of a
 * second. There is no arrangement that avoids both, because HLSW times from its
 * own call and asks again immediately, and a wrong number in the ping column is
 * a smaller lie than a server shown as unreachable when it is not. */
static void pump_deferred(void)
{
    struct {
        SOCKET s;
        struct sockaddr_in to;
        char buf[MAX_QUERY];
        int len;
        int claims_challenge;
    } ready[8];
    int n = 0, i, k;
    DWORD now;

    if (!real_sendto || g_query_interval <= 0)
        return;

    /* Collected under the lock, sent outside it: sendto on a socket another
     * thread is reading is fine, holding the lock across it is not. At most a
     * handful per tick, and the tick is short, so nothing waits long and no
     * entry can starve. */
    now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < MAX_ENTRIES && n < 8; i++) {
        Entry *e = &g_entries[i];

        if (!e->used)
            continue;
        for (k = 0; k < 3 && n < 8; k++) {
            const char *q = e->pending[k];
            int qlen = e->pending_len[k];

            if (qlen <= 0 || (now - e->last_sent[k]) < (DWORD)g_query_interval)
                continue;

            memset(&ready[n].to, 0, sizeof(ready[n].to));
            ready[n].to.sin_family = AF_INET;
            ready[n].to.sin_addr.s_addr = e->ip;
            ready[n].to.sin_port = e->port;
            ready[n].s = e->sock;
            memcpy(ready[n].buf, q, qlen);
            ready[n].len = qlen;
            /* Read back off the stored bytes rather than remembered: a players
             * or rules query carrying the -1 placeholder is asking for a
             * challenge, and only now is it really asking. */
            ready[n].claims_challenge = (k != KIND_INFO && qlen >= 9
                                         && (UCHAR)q[5] == 0xFF && (UCHAR)q[6] == 0xFF
                                         && (UCHAR)q[7] == 0xFF && (UCHAR)q[8] == 0xFF);

            e->pending_len[k] = 0;
            e->last_sent[k] = now;
            if (k == KIND_INFO)
                e->info_pending = 1;
            if (ready[n].claims_challenge) {
                e->client_wants_challenge = 1;
                e->challenge_asked_at = now;
            }
            n++;
        }
    }
    LeaveCriticalSection(&g_lock);

    for (i = 0; i < n; i++) {
        dump_packet("LATE   ->", &ready[i].to, ready[i].buf, ready[i].len);
        real_sendto(ready[i].s, ready[i].buf, ready[i].len, 0,
                    (struct sockaddr *)&ready[i].to, sizeof(struct sockaddr_in));
    }
}

/* A thread of our own, and not a matter of taste. HLSW does its socket work on
 * one thread with a blocking recvfrom and never calls select at all, which the
 * packet log settles: the select hook has been in place all along and has never
 * once fired. So there is no call of HLSW's left to hang this on at the one
 * moment it matters, namely when HLSW is sitting in recvfrom waiting for the
 * answer to a query that is still held here.
 *
 * It runs for as long as the process does. Nothing unloads this library, the
 * launcher injects it and never frees it, so there is no teardown to get wrong.
 * The stop flag is there anyway, so that the loop has a real exit rather than
 * one the compiler has to be told to ignore. */
static volatile LONG g_pump_stop;

static DWORD WINAPI pump_thread(LPVOID unused)
{
    (void)unused;

    while (!g_pump_stop) {
        Sleep(PUMP_TICK_MS);
        pump_deferred();
    }
    return 0;
}

static LONG g_pump_started;

/* Started on the first query rather than in DllMain, because creating a thread
 * under the loader lock is a well known way to deadlock. */
static void start_pump(void)
{
    HANDLE t;

    if (g_query_interval <= 0)
        return;
    if (InterlockedCompareExchange(&g_pump_started, 1, 0) != 0)
        return;

    t = CreateThread(NULL, 0, pump_thread, NULL, 0, NULL);
    if (t) {
        CloseHandle(t);
        dbg_log("pacing thread started, tick %d ms", PUMP_TICK_MS);
    } else {
        /* Nothing would ever send the held queries, so give the pacing up
         * rather than leave HLSW waiting for answers that cannot come. */
        g_query_interval = 0;
        dbg_log("could not start the pacing thread, error %lu, pacing switched off",
                GetLastError());
    }
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
            /* The connectionless test is not decoration. A split answer starts
             * FE FF FF FF, so its fifth byte is part of a request id and not a
             * packet type at all: counted in one 28 minute log, 54 fragments
             * carried 49 there by chance. Without this they would each be taken
             * for an info reply, clearing info_pending so that a needed repeat
             * is skipped, and stamping last_modern_info so that a server which
             * only ever speaks the old format would have its one real answer
             * hidden for the next ten seconds. */
            if (is_connectionless(buf, rc) && (UCHAR)buf[4] == A2S_INFO_REPLY) {
                e->info_pending = 0;
                e->info_misses = 0;
                /* Noted so that a duplicate in the old format can be
                 * recognised as redundant rather than guessed at. */
                e->last_modern_info = GetTickCount();
            } else if (is_connectionless(buf, rc)
                       && (UCHAR)buf[4] == A2S_INFO_REPLY_OLD) {
                /* The old format answers the same query, so it settles it too,
                 * and a server that speaks only that one must never be counted
                 * as not answering. */
                e->info_pending = 0;
                e->info_misses = 0;
            }
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
        /* This is the proof, and the only one there is: an A2S_INFO of ours
         * came back as a challenge, so this server really does demand one.
         * Until that has happened the query is sent plainly, because a server
         * that never asked for a challenge may well treat four extra bytes as
         * rubbish and answer nothing at all. */
        e->info_needs_challenge = 1;
        e->info_pending = 0;
        e->info_misses = 0;
        repeat = 1;
        memcpy(query, A2S_INFO, A2S_INFO_LEN);
        memcpy(query + A2S_INFO_LEN, e->challenge, 4);
    }
    LeaveCriticalSection(&g_lock);
    return repeat;
}

/* Blanks out the second, redundant answer that a few servers send for a single
 * A2S_INFO: the old GoldSrc format alongside the modern one, one millisecond
 * apart. HLSW reads both and displays whichever arrived last, so the game icon
 * and the version string flip back and forth several times a second for as long
 * as such a server is selected.
 *
 * Nothing about the delivery changes. The packet is still handed to HLSW, same
 * length, same sender, same instant; only the type byte becomes one the query
 * protocol does not use, so HLSW still recognises a query protocol packet and
 * then finds nothing to do with it. Leaving the four header bytes alone is the
 * safer half of that choice: a packet that is no longer connectionless might
 * reach code meant for a game connection, while an unknown type inside a
 * connectionless packet can only be ignored, since that is what arrives from
 * the open internet all day.
 *
 * That indirection is the whole point. Swallowing the packet and returning the
 * next one instead would mean waiting for a packet that might never arrive, and
 * blocking inside a receive hook is exactly what once broke HLSW's receive
 * loop, so this does not go near it.
 *
 * It only ever fires with proof in hand: the same server, on the same socket,
 * must have answered in the modern format within the last few seconds. A server
 * that speaks nothing but the old format is therefore never touched, and one
 * that stops sending the modern answer is fully visible again within
 * BOTH_FORMATS_MS. The cost when the modern answer is the one that gets lost on
 * the way is a single missed refresh, which is what a lost packet costs anyway.
 *
 * Called after incoming(), deliberately, so the challenge logic keeps seeing
 * the bytes exactly as they came off the wire. */
static void hide_duplicate_info(SOCKET s, const struct sockaddr_in *peer,
                                char *buf, int rc)
{
    Entry *e;
    int hide = 0;

    /* 20 bytes is far below the 73 such an answer measured, and well above
     * anything that could be a short packet of some other kind. */
    if (!g_hide_duplicate_info || rc < 20 || !is_connectionless(buf, rc)
        || (UCHAR)buf[4] != A2S_INFO_REPLY_OLD)
        return;

    EnterCriticalSection(&g_lock);
    e = entry_get(s, peer, 0);
    if (e && e->last_modern_info
        && (GetTickCount() - e->last_modern_info) < BOTH_FORMATS_MS)
        hide = 1;
    LeaveCriticalSection(&g_lock);

    if (!hide)
        return;

    buf[4] = 0;
    /* Only at the packet level, because this fires on every refresh of such a
     * server and would otherwise bury everything else in the log. */
    if (g_logging >= 2)
        dbg_log("%s answered twice, the old format copy was hidden",
                inet_ntoa(peer->sin_addr));
}

static int WSAAPI my_sendto(SOCKET s, const char *buf, int len, int flags,
                            const struct sockaddr *to, int tolen)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)to;
    char query[A2S_INFO_LEN + 4];
    int rc;

    if (!to || tolen < (int)sizeof(struct sockaddr_in) || to->sa_family != AF_INET)
        return real_sendto(s, buf, len, flags, to, tolen);

    /* Reported as sent. Telling HLSW the send failed would only make it retry
     * and log errors at us; as far as it is concerned the packet went out and
     * nobody answered, which is what has been happening for years anyway. */
    if (g_block_home && is_home_address(sin)) {
        dump_packet("BLOCKED ->", sin, buf, len);
        return len;
    }

    /* Logged after the decision, not before, and labelled with what actually
     * happened. Dumping the packet on the way in makes a query that was held
     * back look exactly like one that went out, which turns the packet log
     * into a source of wrong conclusions in the one situation it exists for. */
    start_pump();

    switch (outgoing(s, sin, buf, len, query, 1)) {
    case SEND_SUPPRESS:
        if (g_refuse_held) {
            /* Refused rather than delayed. WSAEWOULDBLOCK is the one error that
             * says exactly this: nothing was sent, ask again in a moment. */
            dump_packet("REFUSE ->", sin, buf, len);
            WSASetLastError(WSAEWOULDBLOCK);
            return SOCKET_ERROR;
        }
        /* Not sent yet. The caller is told it went out, which becomes true a
         * fraction of a second later when the pacing thread sends it. */
        dump_packet("DEFER  ->", sin, buf, len);
        return len;
    case SEND_CHALLENGE:
        dump_packet("SENDTO ->", sin, query, sizeof(query));
        rc = real_sendto(s, query, sizeof(query), flags, to, tolen);
        /* Report the length the caller handed us, not the longer one that
         * actually went out, so its own accounting stays consistent. */
        return rc == (int)sizeof(query) ? len : rc;
    default:
        dump_packet("SENDTO ->", sin, buf, len);
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

    if (outgoing(s, &peer, buf, len, query, 0) != SEND_CHALLENGE) {
        dump_packet("SEND   ->", &peer, buf, len);
        return real_send(s, buf, len, flags);
    }

    dump_packet("SEND   ->", &peer, query, sizeof(query));
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
    hide_duplicate_info(s, sin, buf, rc);
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
    hide_duplicate_info(s, &peer, buf, rc);
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
    hide_duplicate_info(s, sin, bufs[0].buf, (int)*recvd);
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
    hide_duplicate_info(s, &peer, bufs[0].buf, (int)*recvd);
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

/* Kept as a control point, but it turned out to prove the opposite of what it
 * was put here for. The assumption was that HLSW cannot poll its sockets
 * without calling select. It never calls it: across a 28 minute log this hook
 * did not fire once. HLSW blocks in recvfrom instead, which is why the held
 * back queries are sent from a thread of our own and not from here. */
static int WSAAPI my_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex,
                            const struct timeval *tv)
{
    static LONG seen = 0;
    LONG n = InterlockedIncrement(&seen);

    if (n <= 3)
        dbg_log("select called (%ld)", n);
    return real_select(nfds, rd, wr, ex, tv);
}

/* HLSW keeps the master server address it last resolved under
 * HKCU\Software\HLSW\Master Server, so refusing to resolve the name would on
 * its own change nothing: it never has to ask again. Measured on this machine,
 * IP2 held 62.75.203.63, which is s9b.hlsw.org, and a ten byte packet went
 * there every five seconds. What is cached is therefore read here and refused
 * at the socket as well. */
static void load_home_addresses(void)
{
    HKEY key;
    int i;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\HLSW\\Master Server",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return;

    for (i = 0; i < MAX_HOME_IPS; i++) {
        char name[16], value[64];
        DWORD type = 0, size = sizeof(value) - 1;
        ULONG addr;

        if (i == 0)
            strcpy(name, "IP");
        else
            snprintf(name, sizeof(name), "IP%d", i + 1);

        memset(value, 0, sizeof(value));
        if (RegQueryValueExA(key, name, NULL, &type, (BYTE *)value, &size) != ERROR_SUCCESS)
            continue;
        if (type != REG_SZ)
            continue;

        addr = inet_addr(value);
        if (addr != INADDR_NONE && g_home_ip_count < MAX_HOME_IPS) {
            g_home_ips[g_home_ip_count++] = addr;
            dbg_log("master server %s cached in the registry as %s, blocked", name, value);
        }
    }
    RegCloseKey(key);
}

static int is_home_address(const struct sockaddr_in *a)
{
    int i;

    for (i = 0; i < g_home_ip_count; i++)
        if (a->sin_addr.s_addr == g_home_ips[i])
            return 1;
    return 0;
}

/* The login screen is HLSW's own setting, so it is switched off in HLSW's own
 * settings rather than by intercepting the window. It can be turned back on
 * inside HLSW, but this runs at every start, so the lasting way back is
 * skip_login_screen = 0 in hlswfix.ini. */
static void turn_off_login_screen(void)
{
    HKEY key;
    DWORD off = 0;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\HLSW\\Management", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExA(key, "LoginOnStartup", 0, REG_DWORD, (const BYTE *)&off, sizeof(off));
    RegSetValueExA(key, "AutoLogin", 0, REG_DWORD, (const BYTE *)&off, sizeof(off));
    RegCloseKey(key);
    dbg_log("login on startup switched off");
}

/* True for hlsw.net, hlsw.org and anything underneath them. Matched on the
 * tail, so s9b.hlsw.org is caught without having to know every host name the
 * developers ever used. The dot before the suffix is required, or a domain
 * like notahlsw.org would match as well. */
static int is_home_domain(const char *name)
{
    static const char *domains[] = { "hlsw.net", "hlsw.org" };
    size_t n, d;
    int i;

    if (!name)
        return 0;
    n = strlen(name);
    for (i = 0; i < 2; i++) {
        d = strlen(domains[i]);
        if (n < d)
            continue;
        if (_stricmp(name + n - d, domains[i]) != 0)
            continue;
        if (n == d || name[n - d - 1] == '.')
            return 1;
    }
    return 0;
}

/* The three ways a program of this age asks for an address. Refusing here
 * rather than at the socket means HLSW never learns where to send, and it
 * fails exactly as it would with no network, which is a state it was built to
 * survive. */
static struct hostent *WSAAPI my_gethostbyname(const char *name)
{
    if (g_block_home && is_home_domain(name)) {
        dbg_log("blocked lookup of %s", name);
        WSASetLastError(WSAHOST_NOT_FOUND);
        return NULL;
    }
    return real_gethostbyname(name);
}

static int WSAAPI my_getaddrinfo(const char *node, const char *service,
                                 const struct addrinfo *hints, struct addrinfo **res)
{
    if (g_block_home && is_home_domain(node)) {
        dbg_log("blocked lookup of %s", node);
        return WSAHOST_NOT_FOUND;
    }
    return real_getaddrinfo(node, service, hints, res);
}

static HANDLE WSAAPI my_WSAAsyncGetHostByName(HWND hwnd, unsigned int msg,
                                              const char *name, char *buf, int buflen)
{
    if (g_block_home && is_home_domain(name)) {
        dbg_log("blocked lookup of %s", name);
        WSASetLastError(WSAHOST_NOT_FOUND);
        return NULL;
    }
    return real_WSAAsyncGetHostByName(hwnd, msg, name, buf, buflen);
}

static int WSAAPI my_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)name;
    int i;

    if (!name || namelen < (int)sizeof(struct sockaddr_in) || name->sa_family != AF_INET)
        return real_connect(s, name, namelen);

    dump_packet(is_udp(s) ? "CONNECT udp" : "CONNECT tcp", sin, "", 0);

    if (g_block_home && is_home_address(sin)) {
        dbg_log("blocked connection to %s:%d", inet_ntoa(sin->sin_addr),
                ntohs(sin->sin_port));
        WSASetLastError(WSAECONNREFUSED);
        return SOCKET_ERROR;
    }

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

/* Reads the version this library was built as out of its own resource, so the
 * number in the title bar follows the build instead of a line in a file.
 *
 * That line used to carry it, and it was the wrong place. The installer never
 * overwrites hlswfix.ini, on purpose, because it is the one file the user is
 * meant to edit; so after an update the title still announced the version
 * before it, and the number had to be remembered by hand at every release. Now
 * there is one place it is written down, the version resource, and everything
 * else follows from it.
 *
 * The fixed block is read rather than the string block: it needs no language
 * key to be present, and it cannot disagree with the numbers Explorer shows. */
static void load_own_version(HMODULE self)
{
    char path[MAX_PATH], text[32];
    void *info;
    DWORD size, handle = 0;
    VS_FIXEDFILEINFO *fixed = NULL;
    UINT len = 0;

    if (!GetModuleFileNameA(self, path, MAX_PATH))
        return;
    size = GetFileVersionInfoSizeA(path, &handle);
    if (!size)
        return;
    info = malloc(size);
    if (!info)
        return;

    if (GetFileVersionInfoA(path, 0, size, info)
        && VerQueryValueA(info, "\\", (LPVOID *)&fixed, &len)
        && fixed && len >= sizeof(*fixed)) {
        snprintf(text, sizeof(text), "%u.%u.%u.%u",
                 (unsigned)HIWORD(fixed->dwFileVersionMS),
                 (unsigned)LOWORD(fixed->dwFileVersionMS),
                 (unsigned)HIWORD(fixed->dwFileVersionLS),
                 (unsigned)LOWORD(fixed->dwFileVersionLS));
        MultiByteToWideChar(CP_ACP, 0, text, -1, g_title_version,
                            sizeof(g_title_version) / sizeof(wchar_t));
    }
    free(info);
}

static void load_config(HMODULE self)
{
    char path[MAX_PATH], line[512], *p;
    FILE *fh;
    DWORD n;
    int first_line = 1;

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
        /* A Windows editor saving this as UTF-8 puts a byte order mark in front
         * of the first line. Without skipping it the first setting in the file
         * is silently ignored, which is a miserable thing to debug: the file
         * looks exactly right and one line of it does nothing. */
        if (first_line) {
            first_line = 0;
            if ((UCHAR)p[0] == 0xEF && (UCHAR)p[1] == 0xBB && (UCHAR)p[2] == 0xBF)
                p += 3;
        }
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
            /* Length checked first: given a target too small for the whole
             * string, MultiByteToWideChar fails and writes nothing, which
             * would leave this buffer without a terminator for everything
             * afterwards to run past. A byte count that fits is a wide
             * character count that fits. */
            if (*v && strlen(v) < sizeof(g_title_version) / sizeof(wchar_t))
                MultiByteToWideChar(CP_ACP, 0, v, -1, g_title_version,
                                    sizeof(g_title_version) / sizeof(wchar_t));
            else if (!*v)
                /* The line is there with nothing after it, which is how you ask
                 * for HLSW's own version back. Leaving it out entirely means
                 * something different: then the build's own version is shown. */
                g_title_version[0] = 0;
        } else if (sscanf(p, "query_interval_ms = %u", &from_port) == 1) {
            g_query_interval = (int)from_port;
        } else if (sscanf(p, "block_home_calls = %u", &from_port) == 1) {
            g_block_home = from_port ? 1 : 0;
        } else if (sscanf(p, "skip_login_screen = %u", &from_port) == 1) {
            g_skip_login = from_port ? 1 : 0;
        } else if (sscanf(p, "hide_duplicate_info = %u", &from_port) == 1) {
            g_hide_duplicate_info = from_port ? 1 : 0;
        } else if (sscanf(p, "refuse_held_queries = %u", &from_port) == 1) {
            g_refuse_held = from_port ? 1 : 0;
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
    /* Own version first, so that a title_version line in the file overrides it
     * rather than the other way round. */
    load_own_version(inst);
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

    /* Name resolution, so that HLSW's calls home never get an address. All
     * three are hooked because a program of this age may use any of them, and
     * only the ones that took are then in force. The addresses it has already
     * cached are handled at the socket, see load_home_addresses. */
    if (g_block_home) {
        real_gethostbyname = detour_api("gethostbyname", (void *)my_gethostbyname);
        real_getaddrinfo   = detour_api("getaddrinfo",   (void *)my_getaddrinfo);
        real_WSAAsyncGetHostByName = detour_api("WSAAsyncGetHostByName",
                                                (void *)my_WSAAsyncGetHostByName);
        load_home_addresses();
    }

    if (g_skip_login)
        turn_off_login_screen();

    /* Cosmetic, and only when asked for. */
    if (g_title_version[0]) {
        char shown[32];

        real_SetWindowTextW = detour_in("USER32.dll", "SetWindowTextW",
                                        (void *)my_SetWindowTextW);
        WideCharToMultiByte(CP_ACP, 0, g_title_version, -1, shown, sizeof(shown),
                            NULL, NULL);
        dbg_log("title bar will read HLSW v%s", shown);
    } else {
        dbg_log("title bar left as HLSW built it");
    }

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
