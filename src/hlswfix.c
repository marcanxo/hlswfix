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
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "text.h"

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

/* How many queries of one kind may go out back to back after a quiet spell,
 * before the rate above applies again.
 *
 * A rate limit without an allowance was wrong for this program. Clicking a
 * server in the list makes HLSW fire everything it knows at it at once, twenty
 * packets inside five milliseconds, and a limit of one per second turns most of
 * that into refusals. HLSW then waits out its deadline and the server you just
 * clicked is the one showing a timeout. Measured at 17:24:20 in a packet log:
 * the burst went out, the four queries behind it were refused, and nothing
 * further happened for 1.7 seconds.
 *
 * Six, because three was measured to be too few. Selecting a server also starts
 * a challenge exchange per query kind, and HLSW answers every challenge that
 * arrives with a fresh query straight away: counted at 18:44:49 in a packet
 * log, six attempts at A2S_PLAYER inside 250 milliseconds, of which three got
 * through and three were refused.
 *
 * The average over any longer stretch is unchanged at one per interval, which
 * is the number the server at the other end actually cares about. The allowance
 * only lets them cluster, and it cannot accumulate beyond this: an idle spell
 * of an hour still buys six, not three thousand. */
#define QUERY_BURST 6

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
    DWORD  next_ok[3];            /* earliest each kind may go out again */
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
 * 1, the default, reports that the send failed. HLSW then knows nothing is on
 * its way, so it never sits waiting for an answer that cannot come, and
 * whatever it sends next goes out at once and is timed honestly. That last part
 * is the reason it won: the ping column shows the real round trip, 11 to 30 ms
 * against these servers.
 *
 * 0 reports it as sent and really sends it a fraction of a second later. HLSW
 * starts its stopwatch at the report, so the delay lands in the ping, which
 * then reads about one interval for every server.
 *
 * Both were measured against seven servers over an evening. Refusing costs two
 * things. HLSW writes every refused send into its status bar as
 *
 *   ERROR in CHLSWSocket::SendTo: (10035) A non-blocking socket operation
 *   could not be completed immediately
 *
 * which fills the footer of a program that is working perfectly, and selecting
 * a server in the list can briefly flash a timeout for it. Nobody could have
 * known either in advance: HLSW's source is lost, and the only way to learn how
 * it reacts to a refused send was to refuse one and watch.
 *
 * Refusing is the default anyway, because the ping is what a server browser is
 * read for and a wrong one is a wrong answer, while a noisy footer is only
 * untidy. Delaying is one line away for anyone who weighs that differently. */
static int             g_refuse_held = 1;

/* On by default. A handful of servers answer one A2S_INFO twice, once in the
 * old GoldSrc format and once in the modern one. HLSW understands both and
 * shows whichever landed last, and the two disagree on exactly the fields it
 * reads from: the old answer carries no application id and reports protocol 47
 * where the modern one reports 48. The result is a game icon and a version
 * string that flip back and forth for as long as the server is selected. */
static int             g_hide_duplicate_info = 1;

/* On by default. Every link in HLSW's interface points at hlsw.org or
 * hlsw.net, and not one of them leads anywhere any more. See rewrite_dead_link
 * for what happens to them instead, and why deleting them would be the wrong
 * answer. */
static int             g_fix_links = 1;

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

static HINSTANCE (WINAPI *real_ShellExecuteW)(HWND, LPCWSTR, LPCWSTR, LPCWSTR,
                                              LPCWSTR, INT);
static BOOL (WINAPI *real_ShellExecuteExW)(SHELLEXECUTEINFOW *);

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

/* The pacing itself, as a bucket that refills rather than a plain gap between
 * two sends. next_ok is the time this kind of query may next go out; a query is
 * let through while it is no more than a burst ahead of that, and each one that
 * goes out pushes next_ok one interval further into the future.
 *
 * Written with signed differences on purpose. GetTickCount wraps every 49.7
 * days, and comparing the values themselves rather than their difference is a
 * bug that waits a month and a half to appear. Entries start with next_ok set
 * to the current time, so the two are always close together and the difference
 * is meaningful. */
static int may_send_now(const Entry *e, int kind, DWORD now)
{
    LONG ahead = (LONG)(now - e->next_ok[kind]);

    return ahead + (LONG)((QUERY_BURST - 1) * g_query_interval) >= 0;
}

static void note_sent(Entry *e, int kind, DWORD now)
{
    DWORD base = ((LONG)(now - e->next_ok[kind]) > 0) ? now : e->next_ok[kind];

    e->next_ok[kind] = base + (DWORD)g_query_interval;
}

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
    /* Not left at zero. The pacing compares against these, and a zero here on a
     * machine that has been up for weeks is a tick count far in the past, which
     * the signed difference would read as a nonsense value. */
    g_entries[oldest].next_ok[0] = now;
    g_entries[oldest].next_ok[1] = now;
    g_entries[oldest].next_ok[2] = now;
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

    /* Nothing here belongs to an address that is not a single real server.
     * HLSW's LAN search broadcasts to 255.255.255.255 on port 0, which Windows
     * rejects anyway, and letting that into the table would mean the pacing
     * thread patiently retrying an impossible address once a second forever. */
    if (peer->sin_port == 0 || peer->sin_addr.s_addr == INADDR_BROADCAST
        || peer->sin_addr.s_addr == INADDR_ANY)
        return SEND_AS_IS;

    if (is_a2s_info(buf, len)) {
        DWORD now = GetTickCount();
        int suppress = 0;

        EnterCriticalSection(&g_lock);
        e = entry_get(s, peer, 1);

        if (can_suppress && g_query_interval > 0
            && !may_send_now(e, KIND_INFO, now)) {
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
            note_sent(e, KIND_INFO, now);
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
            && !may_send_now(e, idx, now)) {
            /* Held the same way, and the claim on the next challenge is staked
             * when it actually goes out rather than now, so a claim cannot
             * stand for a query that is still sitting here.
             *
             * The length test guards the copy, not the decision. Having it in
             * the condition meant an oversized query skipped the pacing
             * altogether and reset the clock while it was at it. */
            suppress = 1;
            if (!g_refuse_held && len <= MAX_QUERY) {
                memcpy(e->pending[idx], buf, len);
                e->pending_len[idx] = len;
            }
        } else {
            note_sent(e, idx, now);
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

            if (qlen <= 0 || !may_send_now(e, k, now))
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
            note_sent(e, k, now);
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

    /* Nothing is ever stored when held queries are refused instead of delayed,
     * so the thread would wake forty times a second to find an empty table. */
    if (g_query_interval <= 0 || g_refuse_held)
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
        /* HLSW asked for this one. It gets it either way, because the packet is
         * handed over untouched; this only records that the claim is settled. */
        e->client_wants_challenge = 0;
    }

    /* Deliberately not an else. The two are not rivals: the challenge value
     * belongs to the address, not to one query, so the same packet can settle
     * HLSW's claim and carry our repeated info query. Written as an else it
     * meant that whenever HLSW happened to be waiting for a challenge at the
     * same moment, our info query silently lost its round. */
    if (e->info_pending) {
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

    /* Port zero is not a destination. HLSW's LAN search broadcasts to
     * 255.255.255.255:0, Windows turns it down as it must, and HLSW writes
     * "Addr invalid" into its status bar, which is the first thing a new user
     * sees. The cause is removed rather than the message hidden: a datagram to
     * port zero can never arrive, so not attempting it loses nothing at all and
     * spares everyone the report of a failure that was certain. */
    if (sin->sin_port == 0) {
        dump_packet("DROPPED ->", sin, buf, len);
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
    int rc, first;

    /* Overlapped calls complete later and somewhere else entirely, so there is
     * nothing to inspect here and nothing safe to do. */
    if (ov || routine || !bufs || count < 1 || !recvd || !from || !fromlen)
        return real_WSARecvFrom(s, bufs, count, recvd, flags, from, fromlen, ov, routine);

    rc = real_WSARecvFrom(s, bufs, count, recvd, flags, from, fromlen, ov, routine);
    if (rc != 0 || from->sa_family != AF_INET)
        return rc;

    sin = (struct sockaddr_in *)from;
    /* recvd is the total across every buffer handed in, while only the first is
     * being looked at, so the smaller of the two is the length that is really
     * there to read. Getting this wrong would read, and in one place write,
     * past the end of a caller's buffer. */
    first = (int)*recvd < (int)bufs[0].len ? (int)*recvd : (int)bufs[0].len;
    dump_packet("WSARECVFROM <-", sin, bufs[0].buf, first);

    if (first >= 9 && real_sendto
        && incoming(s, sin, bufs[0].buf, first, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(sin->sin_addr));
        real_sendto(s, query, sizeof(query), 0, from, sizeof(struct sockaddr_in));
    }
    hide_duplicate_info(s, sin, bufs[0].buf, first);
    return rc;
}

static int WSAAPI my_WSARecv(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD recvd,
                             LPDWORD flags, LPWSAOVERLAPPED ov,
                             LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
{
    struct sockaddr_in peer;
    char query[A2S_INFO_LEN + 4];
    int rc, first;

    if (ov || routine || !bufs || count < 1 || !recvd || !udp_peer_of(s, &peer))
        return real_WSARecv(s, bufs, count, recvd, flags, ov, routine);

    rc = real_WSARecv(s, bufs, count, recvd, flags, ov, routine);
    if (rc != 0)
        return rc;

    first = (int)*recvd < (int)bufs[0].len ? (int)*recvd : (int)bufs[0].len;
    dump_packet("WSARECV <-", &peer, bufs[0].buf, first);

    if (first >= 9 && real_send
        && incoming(s, &peer, bufs[0].buf, first, query)) {
        dbg_log("challenge from %s, repeating A2S_INFO", inet_ntoa(peer.sin_addr));
        real_send(s, query, sizeof(query), 0);
    }
    hide_duplicate_info(s, &peer, bufs[0].buf, first);
    return rc;
}

/* ------------------------------------------------------------- dead links */

/* Where a link in HLSW's interface goes was decided when hlsw.org was still
 * somebody's website. Measured on 2026-08-19, none of it answers any more:
 * wiki.hlsw.org serves the bare Apache default page and 404s every article,
 * the homepage, the registration form and the Sentinel lookup all answer 403,
 * and nothing under hlsw.net answers at all. That is 180 wiki links in
 * cfg\Games.cfg and cfg\AddOns.cfg alone, one per game and per server addon,
 * plus a handful built into hlsw.exe itself.
 *
 * Removing them is the obvious answer and the wrong one, because most of what
 * they point at still exists. The wiki moved from hlsw.org to hlsw.net before
 * it went dark, and the Internet Archive kept the .net copy: of those 180
 * pages, 2 are archived under the .org name and 136 under the .net one.
 * Rewriting the host before handing the address to the archive is the whole
 * difference between a dead link and a working one.
 *
 * The player context menu holds a second case worth repairing rather than
 * burying. "Steam Community" goes to www.hlsw.org/steamprofile/<id>/, which
 * was HLSW's own redirect to Steam, and the account id is right there in the
 * address. That click can go to Steam directly.
 *
 * Everything else is handed to the archive under its own address and shows
 * either the page as it was or the archive's own "not archived" notice. Both
 * are more use than a server that refuses.
 *
 * Nothing here is a guess about what a link means. Only the address is read. */

#define ARCHIVE_PREFIX L"https://web.archive.org/web/2011/"
#define STEAM_PROFILE  L"https://steamcommunity.com/profiles/"
#define WIKI_HOST      L"http://wiki.hlsw.net"

/* Tail match on the host, so s9b, wiki, sentinel and every other name the
 * developers ever used is caught without listing them. The dot is required, or
 * a host like notahlsw.org would match too. The same rule as is_home_domain,
 * on wide characters and on a length rather than a terminator, because the
 * host sits in the middle of the address. */
static int is_home_host_w(const wchar_t *host, size_t len)
{
    static const wchar_t *domains[2] = { L"hlsw.net", L"hlsw.org" };
    int i;

    for (i = 0; i < 2; i++) {
        size_t d = wcslen(domains[i]);

        if (len < d)
            continue;
        if (_wcsnicmp(host + len - d, domains[i], (int)d) != 0)
            continue;
        if (len == d || host[len - d - 1] == L'.')
            return 1;
    }
    return 0;
}

/* Appends and stops at the end of the buffer, so a long address is cut short
 * rather than written past. */
static void append_w(wchar_t *out, size_t cap, const wchar_t *add)
{
    size_t have = wcslen(out);

    if (have + 1 >= cap)
        return;
    lstrcpynW(out + have, add, (int)(cap - have));
}

/* Case insensitive search, which the standard library has no wide version of. */
static const wchar_t *wcsstr_i(const wchar_t *hay, const wchar_t *needle)
{
    size_t n = wcslen(needle);

    if (!n)
        return hay;
    for (; *hay; hay++)
        if (_wcsnicmp(hay, needle, (int)n) == 0)
            return hay;
    return NULL;
}

static void u64_to_wide(unsigned long long v, wchar_t *out)
{
    wchar_t tmp[24];
    int i = 0, j = 0;

    if (!v) {
        out[0] = L'0';
        out[1] = 0;
        return;
    }
    while (v && i < 23) {
        tmp[i++] = (wchar_t)(L'0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = 0;
}

/* STEAM_X:Y:Z is how every tool of that era wrote an account: the number split
 * in two, with Y as its lowest bit. What a Steam profile address wants is the
 * 64 bit form, 76561197960265728 + Z * 2 + Y.
 *
 * Written out rather than handed to printf, so that the digits do not depend
 * on which runtime the build lands on. */
static int steam_id64_w(const wchar_t *gid, size_t len, wchar_t *out, size_t cap)
{
    unsigned long long value[3] = { 0, 0, 0 };
    unsigned long long y, z;
    size_t i = 0;
    int fields = 0;

    if (cap < 24)
        return 0;
    if (len > 6 && _wcsnicmp(gid, L"STEAM_", 6) == 0) {
        gid += 6;
        len -= 6;
    }

    while (i < len && fields < 3) {
        if (gid[i] < L'0' || gid[i] > L'9')
            return 0;
        while (i < len && gid[i] >= L'0' && gid[i] <= L'9') {
            value[fields] = value[fields] * 10 + (unsigned)(gid[i] - L'0');
            if (value[fields] > 0xFFFFFFFFULL)
                return 0;
            i++;
        }
        fields++;
        if (i < len && gid[i] == L':')
            i++;
        else
            break;
    }
    if (i != len)
        return 0;

    if (fields == 3) {
        y = value[1];
        z = value[2];
    } else if (fields == 2) {
        y = value[0];
        z = value[1];
    } else {
        return 0;
    }
    if (y > 1)
        return 0;

    u64_to_wide(76561197960265728ULL + z * 2 + y, out);
    return 1;
}

/* True for an address that looks up one particular server or player.
 *
 * These are the ones the archive can never help with, and sending them there
 * would be worse than leaving them alone: a crawler in 2011 had no reason to
 * fetch the page for this server or that account, so the archive answers every
 * one of them with its own "not archived" notice. A link that plainly fails is
 * more honest than one that leads to a page explaining that it has nothing.
 *
 * So these keep the address HLSW gave them and fail the way they already did.
 * The one exception is above this: the Steam profile redirect, which is also a
 * lookup for one player, but has somewhere real to go instead. */
static int is_per_entity_lookup(const wchar_t *url)
{
    static const wchar_t *paths[4] = {
        L"/gameserver/",   /* www.hlsw.org, one server, with the register form */
        L"/profile/",      /* www.hlsw.org, one account */
        L"/player/",       /* sentinel.hlsw.org, one account */
        L"/server/"        /* sentinel.hlsw.org, one server */
    };
    int i;

    for (i = 0; i < 4; i++)
        if (wcsstr_i(url, paths[i]))
            return 1;
    return 0;
}

/* Fills out with where the address should go instead, and returns whether it
 * did. Anything that is not an http address on one of the two dead domains is
 * left completely alone. */
static int rewrite_dead_link(const wchar_t *url, wchar_t *out, size_t cap)
{
    const wchar_t *host, *rest, *id;
    size_t host_len;

    if (!g_fix_links || !url || cap < 64)
        return 0;

    while (*url == L' ' || *url == L'\t')
        url++;

    if (_wcsnicmp(url, L"http://", 7) == 0)
        host = url + 7;
    else if (_wcsnicmp(url, L"https://", 8) == 0)
        host = url + 8;
    else
        return 0;

    rest = host;
    while (*rest && *rest != L'/' && *rest != L':' && *rest != L'?')
        rest++;
    host_len = (size_t)(rest - host);
    if (!is_home_host_w(host, host_len))
        return 0;

    out[0] = 0;

    /* The wiki, moved to the name it died under and then handed to the
     * archive. Anything after the host travels unchanged, including the
     * index.php?title= form a few of the links use. */
    if (host_len > 5 && _wcsnicmp(host, L"wiki.", 5) == 0) {
        append_w(out, cap, ARCHIVE_PREFIX);
        append_w(out, cap, WIKI_HOST);
        append_w(out, cap, *rest ? rest : L"/");
        return 1;
    }

    /* HLSW's own redirect to a Steam profile, with the account id lifted out
     * of the middle of it. */
    id = wcsstr_i(url, L"/steamprofile/");
    if (id) {
        const wchar_t *end;
        wchar_t id64[24];

        id += 14;
        end = id;
        while (*end && *end != L'/' && *end != L'?')
            end++;
        if (end > id && steam_id64_w(id, (size_t)(end - id), id64, 24)) {
            append_w(out, cap, STEAM_PROFILE);
            append_w(out, cap, id64);
            return 1;
        }
        out[0] = 0;
    }

    if (is_per_entity_lookup(url))
        return 0;

    append_w(out, cap, ARCHIVE_PREFIX);
    append_w(out, cap, url);
    return 1;
}

/* The same rewriting, but for a line of text that has an address somewhere in
 * it rather than being nothing but one.
 *
 * This exists because what HLSW puts in its status bar when the mouse passes
 * over a link was never established, only assumed: it may be the bare address,
 * or the address with something in front of it. Rather than find out and then
 * depend on the answer, every address in the line is found and replaced where
 * it stands, and everything around it travels unchanged. Then it does not
 * matter which of the two it turns out to be.
 *
 * An address ends at whitespace or at one of the characters that cannot appear
 * in one, which is where the surrounding text starts again. */
static int rewrite_text_links(const wchar_t *in, wchar_t *out, size_t cap)
{
    size_t at = 0;
    int changed = 0;

    if (!g_fix_links || !in || cap < 64)
        return 0;

    out[0] = 0;
    while (*in) {
        const wchar_t *url = NULL;

        if (_wcsnicmp(in, L"http://", 7) == 0)
            url = in;
        else if (_wcsnicmp(in, L"https://", 8) == 0)
            url = in;

        if (url) {
            wchar_t one[1024], fixed[1024];
            const wchar_t *end = url;
            size_t n;

            while (*end && *end > L' ' && *end != L'"' && *end != L'<' && *end != L'>')
                end++;
            n = (size_t)(end - url);
            if (n < sizeof(one) / sizeof(one[0])) {
                lstrcpynW(one, url, (int)n + 1);
                if (rewrite_dead_link(one, fixed, 1024)) {
                    append_w(out, cap, fixed);
                    at = wcslen(out);
                    in = end;
                    changed = 1;
                    continue;
                }
            }
            /* Not one of ours, or too long to hold: copy it across as it is,
             * in one go, so the scan does not start again in the middle of it
             * and find something that only looks like an address. */
            if (at + n + 1 < cap) {
                lstrcpynW(out + at, url, (int)n + 1);
                at += n;
                out[at] = 0;
            }
            in = end;
            continue;
        }

        if (at + 2 < cap) {
            out[at++] = *in;
            out[at] = 0;
        }
        in++;
    }
    return changed;
}

/* Wide text in a log file that is written as plain bytes. On a conversion that
 * does not fit, WideCharToMultiByte writes nothing and leaves the buffer as it
 * found it, so the terminator is set by hand either way. */
static void log_link(const char *what, const wchar_t *from, const wchar_t *to)
{
    char a[600], b[600];

    if (g_logging < 1)
        return;
    if (WideCharToMultiByte(CP_ACP, 0, from, -1, a, sizeof(a), NULL, NULL) == 0)
        a[0] = 0;
    if (WideCharToMultiByte(CP_ACP, 0, to, -1, b, sizeof(b), NULL, NULL) == 0)
        b[0] = 0;
    dbg_log("%s %s -> %s", what, a, b);
}

static HINSTANCE WINAPI my_ShellExecuteW(HWND hwnd, LPCWSTR verb, LPCWSTR file,
                                         LPCWSTR params, LPCWSTR dir, INT show)
{
    wchar_t fixed[1024];

    if (rewrite_dead_link(file, fixed, 1024)) {
        log_link("opening", file, fixed);
        return real_ShellExecuteW(hwnd, verb, fixed, params, dir, show);
    }
    return real_ShellExecuteW(hwnd, verb, file, params, dir, show);
}

/* The structure is copied rather than edited, because it belongs to the caller
 * and the address in it may well be a string constant in a read only section.
 * What the call reports back is copied into the original, since that is where
 * the caller looks for it. */
static BOOL WINAPI my_ShellExecuteExW(SHELLEXECUTEINFOW *info)
{
    wchar_t fixed[1024];
    SHELLEXECUTEINFOW copy;
    BOOL ok;

    if (!info || !rewrite_dead_link(info->lpFile, fixed, 1024))
        return real_ShellExecuteExW(info);

    log_link("opening", info->lpFile, fixed);
    copy = *info;
    copy.lpFile = fixed;
    ok = real_ShellExecuteExW(&copy);
    info->hInstApp = copy.hInstApp;
    info->hProcess = copy.hProcess;
    return ok;
}

/* Rewrites the version in the window title, which HLSW builds from its own
 * version resource. Done here rather than by editing that resource, so the
 * program's own files stay exactly as they were shipped and the change travels
 * with this library instead of with a modified executable. */
static BOOL WINAPI my_SetWindowTextW(HWND hwnd, LPCWSTR text)
{
    wchar_t buf[512];
    const wchar_t *rest;

    /* Sweep up after ourselves. Refusing a held query makes HLSW write a line
     * into its status bar, and it turned out those lines travel through this
     * very call, so the noise we cause can be caught where we cause it.
     * Measured over a minute and a half: 443 of the 469 error lines in the
     * footer were this one.
     *
     * Deliberately as narrow as it can be made. Only the exact message our own
     * refusals produce is swallowed, only while refusing is switched on, and
     * every other error HLSW has to report, including a genuine 10035 from
     * anything but SendTo, goes through untouched. Hiding a program's errors is
     * otherwise the wrong thing to do; this hides our own noise, not its news.
     * At log level 2 the swallowed lines are written to the log, so nothing
     * disappears without trace. */
    if (g_refuse_held && text
        && wcsstr(text, L"CHLSWSocket::SendTo") && wcsstr(text, L"(10035)")) {
        if (g_logging >= 2) {
            char shown[200];
            int n = (int)wcslen(text);

            if (n > 180)
                n = 180;
            n = WideCharToMultiByte(CP_ACP, 0, text, n, shown, sizeof(shown) - 1,
                                    NULL, NULL);
            shown[n > 0 ? n : 0] = 0;
            dbg_log("swallowed our own noise: %s", shown);
        }
        return TRUE;
    }

    /* The same link shown rather than followed. HLSW writes the address of
     * whatever the mouse is over into its status bar, and without this the
     * footer would go on advertising a host that has not answered in years
     * while the click quietly went somewhere that works. */
    if (g_fix_links && text) {
        wchar_t fixed[1024];

        if (rewrite_text_links(text, fixed, 1024)) {
            /* Only when it is not the line that was logged last.
             *
             * Measured, not guessed: HLSW rewrites its status bar while the
             * mouse merely rests on a link, dozens of times a second with the
             * same text every time. One session of hovering produced 884 of
             * 1160 lines in the log, all identical, and everything worth
             * reading was buried under them. The rewriting itself still runs
             * every time, because HLSW is told the text every time; it is only
             * the record of it that is worth keeping once.
             *
             * The comparison is against the last line written and nothing
             * more, so a link visited, left and visited again is recorded
             * again. Only HLSW's own interface thread writes here. */
            static wchar_t last[1024];

            if (wcscmp(last, text) != 0) {
                lstrcpynW(last, text, 1024);
                log_link("status bar", text, fixed);
            }
            return real_SetWindowTextW(hwnd, fixed);
        }
        /* Named a dead host and was left alone anyway. That should not happen,
         * and if it ever does this is the line that says so instead of the
         * feature simply appearing not to work. */
        if (g_logging >= 2 && (wcsstr_i(text, L"hlsw.org") || wcsstr_i(text, L"hlsw.net")))
            log_link("status bar, not rewritten", text, L"(unchanged)");
    }

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

/* HLSW looks for updates of itself at every start, at servers that stopped
 * answering years ago. block_home_calls already refuses the connection, but
 * refusing a call is not as tidy as never placing it: the attempt still costs
 * a name lookup and a socket, and it fills the log with something nobody can
 * act on.
 *
 * This is HLSW's own setting, so it is switched off in HLSW's own settings,
 * exactly the way the login screen is. Turning it back on inside HLSW lasts
 * until the next start, because this runs at every start. block_home_calls = 0
 * leaves it alone. */
static void turn_off_hlsw_update_check(void)
{
    HKEY key;
    DWORD off = 0;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\HLSW\\Settings", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExA(key, "AutoUpdateCheck", 0, REG_DWORD, (const BYTE *)&off, sizeof(off));
    RegCloseKey(key);
    dbg_log("HLSW's own update check switched off");
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

/* The same fallback for a function that is not winsock. Kept apart from
 * patch_everywhere rather than folded into it, because that one tries both
 * winsock libraries in turn and would need a special case for every other
 * library added to it.
 *
 * Worth knowing about its reach here: HLSW is started suspended and injected
 * into before its own imports are resolved, so an entry patched in hlsw.exe
 * can still be written over by the loader afterwards. That makes this a
 * genuinely weaker route for the shell functions than for anything else, and
 * it is only ever reached when the detour itself could not be placed. */
static void *patch_import_everywhere(const char *dll, const char *func, void *replacement)
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

            /* Never inside the library that owns the function, or the hook
             * would reach its own replacement and recurse. */
            if (me.hModule == g_self || _stricmp(me.szModule, dll) == 0)
                continue;
            old = patch_import_in(me.hModule, dll, 0, func, replacement);
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

/* Two entries per function, because a name can live in both winsock
 * libraries with a different implementation behind each. Sized with room to
 * spare: at sixteen this table was two short of holding the shell entry points
 * as well, and running out of it is the kind of failure that shows up as a
 * feature quietly not working. */
#define MAX_DETOURS 32
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

    if (!t || already_detoured(t))
        return NULL;
    if (g_detour_count >= MAX_DETOURS) {
        dbg_log("no room left to redirect %p, raise MAX_DETOURS", target);
        return NULL;
    }
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
    char text[32];
    HRSRC found;
    HGLOBAL held;
    void *info;
    VS_FIXEDFILEINFO *fixed = NULL;
    UINT len = 0;

    /* Read out of the image already mapped into this process, not through
     * GetFileVersionInfo, which would open and map the file all over again.
     * This runs from DllMain with the loader lock held, and asking the loader
     * to do more work from in there is exactly the rule the pacing thread is
     * started late to obey. FindResource and LockResource only walk memory that
     * is mapped already and touch the loader not at all. */
    found = FindResourceA(self, MAKEINTRESOURCEA(1), (LPCSTR)RT_VERSION);
    if (!found)
        return;
    held = LoadResource(self, found);
    if (!held)
        return;
    info = LockResource(held);
    if (!info)
        return;

    if (VerQueryValueA(info, "\\", (LPVOID *)&fixed, &len)
        && fixed && len >= sizeof(*fixed)) {
        snprintf(text, sizeof(text), "%u.%u.%u.%u",
                 (unsigned)HIWORD(fixed->dwFileVersionMS),
                 (unsigned)LOWORD(fixed->dwFileVersionMS),
                 (unsigned)HIWORD(fixed->dwFileVersionLS),
                 (unsigned)LOWORD(fixed->dwFileVersionLS));
        MultiByteToWideChar(CP_ACP, 0, text, -1, g_title_version,
                            sizeof(g_title_version) / sizeof(wchar_t));
    }
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
        } else if (sscanf(p, "fix_dead_links = %u", &from_port) == 1) {
            g_fix_links = from_port ? 1 : 0;
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

/* Set when there is no way left to repeat a query, so that the message about
 * it can be shown from somewhere it is allowed to be shown from. */
static volatile LONG g_hooks_failed;

/* Everything that does not have to happen under the loader lock, moved out of
 * it.
 *
 * DllMain runs with that lock held, and what may be done in there is narrow:
 * no LoadLibrary, and nothing that creates a window or pumps messages, because
 * a window can send a message to another thread which then wants the loader,
 * and both sides wait for each other. This library broke that twice. It put up
 * a message box when the redirection failed, which is exactly the
 * window-creating case, and it took a Toolhelp snapshot to write the module
 * list into the log, which walks the very list the lock is there to protect.
 *
 * Neither has ever gone wrong here, and that is the point of fixing it anyway:
 * it works until it meets a machine with something else injected into the same
 * process, an anti-virus or an overlay, and then it looks like HLSW simply not
 * starting, with nothing anywhere to find.
 *
 * This thread is created as the last thing DllMain does and cannot run before
 * the loader has finished, because starting a thread goes through the loader
 * as well and waits for the same lock. So by the time any of this runs, the
 * lock is gone.
 *
 * What stays behind in DllMain stays for a reason. The detours have to be in
 * place before HLSW runs its first instruction, and they are plain memory
 * writes, which is allowed. The settings have to be read before it is known
 * which detours to place at all. The two registry values have to be written
 * before HLSW reads them, which is moments later. */
static DWORD WINAPI after_attach(LPVOID unused)
{
    (void)unused;

    /* Here rather than in DllMain: it is only ever needed for a message, and
     * every message is shown from here now. */
    text_init(g_self, STR_HOOKS_FAILED);

    log_modules();

    if (g_hooks_failed)
        MessageBoxW(NULL, text(STR_HOOKS_FAILED), L"hlswfix", MB_OK | MB_ICONWARNING);
    return 0;
}

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
        turn_off_hlsw_update_check();
    }

    if (g_skip_login)
        turn_off_login_screen();

    /* Links into a world that is gone, sent to the Internet Archive instead,
     * or straight to Steam where the address carries an account id. Both entry
     * points are taken because a program of this age may use either, and
     * because the one HLSW uses cannot be told from the outside. */
    if (g_fix_links) {
        real_ShellExecuteW = detour_in("SHELL32.dll", "ShellExecuteW",
                                       (void *)my_ShellExecuteW);
        if (!real_ShellExecuteW)
            real_ShellExecuteW = patch_import_everywhere("SHELL32.dll", "ShellExecuteW",
                                                         (void *)my_ShellExecuteW);

        real_ShellExecuteExW = detour_in("SHELL32.dll", "ShellExecuteExW",
                                         (void *)my_ShellExecuteExW);
        if (!real_ShellExecuteExW)
            real_ShellExecuteExW = patch_import_everywhere("SHELL32.dll", "ShellExecuteExW",
                                                           (void *)my_ShellExecuteExW);

        if (!real_ShellExecuteW && !real_ShellExecuteExW)
            dbg_log("dead links stay dead: SHELL32 is not loaded here, or neither "
                    "of its two entry points could be redirected");
        else
            dbg_log("dead links redirected: ShellExecuteW=%p ShellExecuteExW=%p",
                    (void *)real_ShellExecuteW, (void *)real_ShellExecuteExW);
    }

    /* Three unrelated jobs share this hook: rewriting the version in the
     * title, swallowing the status bar lines our own refusals cause, and
     * rewriting the address the status bar shows for a link. Any one of them
     * is enough to install it, which is why the condition is not simply
     * whether a title version is set. */
    if (g_title_version[0] || g_refuse_held || g_fix_links) {
        char shown[32];

        real_SetWindowTextW = detour_in("USER32.dll", "SetWindowTextW",
                                        (void *)my_SetWindowTextW);
        if (g_title_version[0]) {
            WideCharToMultiByte(CP_ACP, 0, g_title_version, -1, shown,
                                sizeof(shown), NULL, NULL);
            dbg_log("title bar will read HLSW v%s", shown);
        } else {
            dbg_log("title bar left as HLSW built it");
        }
    } else {
        dbg_log("title bar left as HLSW built it, window text not hooked");
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
    if (!((real_recvfrom && real_sendto) || (real_recv && real_send)))
        g_hooks_failed = 1;

    /* Last, and deliberately last: everything above had to be done here,
     * everything in there did not. See after_attach. */
    {
        HANDLE thread = CreateThread(NULL, 0, after_attach, NULL, 0, NULL);

        if (thread)
            CloseHandle(thread);
        else if (g_hooks_failed)
            /* Nowhere left to say it from, so say it here after all. Breaking
             * the rule beats a redirection that silently did not happen. */
            MessageBoxA(NULL, "hlswfix could not redirect the winsock functions "
                              "it needs.\r\nHLSW will run, but servers will show "
                              "as timed out.", "hlswfix", MB_OK | MB_ICONWARNING);
    }
    return TRUE;
}
