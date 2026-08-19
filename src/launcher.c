/*
 * Launcher that starts HLSW with the shim already inside it.
 *
 * It sits next to HLSW under either of two arrangements, and works out which
 * one it is in by looking at what is on disk:
 *
 *   dropped in   Nothing was renamed. This program is hlswfix.exe and it
 *                starts hlsw.exe. Undoing it means deleting the hlswfix files.
 *   taken over   The original was renamed to hlsw-real.exe and this program
 *                took the name hlsw.exe, so every shortcut and start menu
 *                entry that already exists starts HLSW fixed without knowing
 *                anything about it. install.ps1 sets that up and undoes it.
 *
 * hlsw-real.exe is preferred whenever it exists, because its presence is
 * exactly what tells the second arrangement from the first.
 *
 * It does three things and then gets out of the way:
 *
 *   1. starts the ssh tunnel that carries rcon, if hlswfix.ini asks for one.
 *      The tunnel is put in a job object, so it cannot outlive this process
 *      even if that process is killed rather than closed.
 *   2. starts HLSW suspended and injects hlswfix.dll, which redirects the
 *      winsock functions it needs before HLSW has run a single instruction of
 *      its own.
 *   3. looks for a newer version of the fix, on a thread of its own so that
 *      nothing is held up by it, and offers to install it. See check_update.
 *   4. waits for HLSW to close, then takes the tunnel down again.
 *
 * The working directory matters and is set deliberately: HLSW is an MFC
 * application that looks for cfg\Games.cfg and the rest of its data relative
 * to the current directory rather than to its own location, and reports its
 * configuration as missing when started from anywhere else.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <winver.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>

/* Room for a full length directory plus one of the file names appended to it,
 * so that none of the paths built below can be cut short. */
#define PATHBUF (MAX_PATH + 32)

static char g_log_path[PATHBUF];
static int  g_logging;

/* On unless the ini says otherwise. See check_update for why it is on rather
 * than off, given that this program blocks HLSW's own calls home. */
static int  g_update_check = 1;

/* Shares hlswfix.log with the library. Problems are always recorded, because a
 * tunnel that quietly fails to start looks exactly like rcon being broken and
 * is otherwise invisible. */
static void launcher_log(const char *fmt, ...)
{
    FILE *fh;
    va_list ap;
    SYSTEMTIME st;

    if (!g_log_path[0])
        return;
    fh = fopen(g_log_path, "a");
    if (!fh)
        return;
    GetLocalTime(&st);
    fprintf(fh, "%02d:%02d:%02d.%03d  launcher: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(fh, fmt, ap);
    va_end(ap);
    fputc('\n', fh);
    fclose(fh);
}

static void die(const char *what)
{
    char msg[512];

    snprintf(msg, sizeof(msg), "%s\r\n\r\nWindows error %lu.", what, GetLastError());
    MessageBoxA(NULL, msg, "hlswfix", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

/* Directory this executable sits in, with a trailing backslash. */
static void own_dir(char *out, DWORD size)
{
    char *p;

    GetModuleFileNameA(NULL, out, size);
    p = strrchr(out, '\\');
    if (p)
        *(p + 1) = 0;
}

static void trim(char *s)
{
    char *p = s + strlen(s);

    while (p > s && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t'))
        *--p = 0;
}

/* Reads the settings the launcher cares about out of hlswfix.ini. The rest of
 * that file is read by the library itself. The redirect target port is picked
 * up as well, because that is the port the tunnel has to end up on and so the
 * one worth checking afterwards. */
static int read_config(const char *dir, char *out, size_t size, int *tunnel_port)
{
    char path[PATHBUF], line[512], *p;
    FILE *fh;
    int found = 0;
    unsigned int a, b;

    *tunnel_port = 0;
    snprintf(path, sizeof(path), "%shlswfix.ini", dir);
    fh = fopen(path, "r");
    if (!fh)
        return 0;

    while (fgets(line, sizeof(line), fh)) {
        p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';')
            continue;
        trim(p);

        if (sscanf(p, "log = %u", &a) == 1) {
            g_logging = a ? 1 : 0;
        } else if (sscanf(p, "update_check = %u", &a) == 1) {
            g_update_check = a ? 1 : 0;
        } else if (sscanf(p, "rcon_redirect = %*[0-9.] : %u -> %u", &a, &b) == 2) {
            if (!*tunnel_port)
                *tunnel_port = (int)b;
        } else if (strncmp(p, "tunnel_command", 14) == 0 && (p = strchr(p, '=')) != NULL) {
            p++;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p) {
                strncpy(out, p, size - 1);
                out[size - 1] = 0;
                found = 1;
            }
        }
    }
    fclose(fh);
    return found;
}

/* Waits for the tunnel to actually be listening, by trying to connect to it.
 * Started is not the same as ready, and ssh failing after it has been spawned
 * is exactly the case that must not pass unnoticed. */
static int wait_for_port(int port, int timeout_ms)
{
    WSADATA wsa;
    int waited = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 0;

    while (waited < timeout_ms) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a;
        int ok;

        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        ok = connect(s, (struct sockaddr *)&a, sizeof(a)) == 0;
        closesocket(s);
        if (ok) {
            WSACleanup();
            return 1;
        }
        Sleep(250);
        waited += 250;
    }
    WSACleanup();
    return 0;
}

static HANDLE  g_res_update;
static WORD    g_group_lang;

/* The name is copied rather than kept as the pointer the enumeration hands
 * over. For a resource named by string, that pointer is a temporary ANSI
 * conversion that is only valid until the callback returns, and using it
 * afterwards happens to work often enough to look correct. HLSW's icon group
 * does carry a name rather than a number, so this is not theoretical. */
static char    g_group_name[256];
static int     g_group_found;
static int     g_group_is_id;
static WORD    g_group_id;

static LPCSTR group_name(void)
{
    return g_group_is_id ? MAKEINTRESOURCEA(g_group_id) : g_group_name;
}

/* An icon in a PE file is a directory of images. The directory, an entry of
 * type RT_GROUP_ICON, lists the sizes and colour depths available and points
 * at one RT_ICON per image. The shell reads the group with the lowest id and
 * picks whichever size it needs, so that one group and the images it names are
 * everything worth copying. */
#pragma pack(push, 1)
typedef struct {
    WORD reserved, type, count;
} GroupHeader;

typedef struct {
    BYTE  width, height, colours, reserved;
    WORD  planes, bits;
    DWORD bytes;
    WORD  id;
} GroupEntry;
#pragma pack(pop)

static BOOL CALLBACK first_group_language(HMODULE mod, LPCSTR type, LPCSTR name,
                                          WORD lang, LONG_PTR param)
{
    (void)mod; (void)type; (void)name; (void)param;
    g_group_lang = lang;
    return FALSE;
}

/* Enumeration runs in directory order, so the first one is the lowest id and
 * therefore the one the shell would show. Stopping here also keeps the rest of
 * HLSW's icons out: it carries its whole interface in there, and copying all
 * of it produced a resource directory that EndUpdateResource rejected outright
 * with ERROR_INVALID_DATA. */
static BOOL CALLBACK first_group(HMODULE mod, LPCSTR type, LPSTR name, LONG_PTR param)
{
    (void)param;
    if (IS_INTRESOURCE(name)) {
        g_group_is_id = 1;
        g_group_id = (WORD)(ULONG_PTR)name;
    } else {
        g_group_is_id = 0;
        lstrcpynA(g_group_name, name, sizeof(g_group_name));
    }
    g_group_found = 1;
    EnumResourceLanguagesA(mod, type, name, first_group_language, 0);
    return FALSE;
}

static void *resource_bytes(HMODULE mod, LPCSTR type, LPCSTR name, WORD lang, DWORD *size)
{
    HRSRC found = FindResourceExA(mod, type, name, lang);
    HGLOBAL loaded;

    if (!found)
        return NULL;
    *size = SizeofResource(mod, found);
    loaded = LoadResource(mod, found);
    if (!loaded || !*size)
        return NULL;
    return LockResource(loaded);
}

/* Copies the icon out of one executable into another.
 *
 * This exists so that the launcher can wear HLSW's icon: it takes the place of
 * hlsw.exe, so every shortcut and the task bar would otherwise show the blank
 * default instead of the icon that was there before.
 *
 * Deliberately done here and not at build time. HLSW's licence lets its own
 * files be copied and passed on, which is not the same as putting somebody
 * else's artwork inside a program of ours and shipping that. So nothing of
 * theirs is shipped: install.ps1 calls this, and the icon is lifted from the
 * copy of HLSW that is already on the machine.
 *
 * Both the images and the directory that indexes them have to travel, or the
 * shell finds a group pointing at pictures that are not there. */
static int copy_icon(const char *from, const char *to)
{
    HMODULE src;
    GroupHeader *header;
    GroupEntry *entry;
    DWORD size = 0;
    int i, ok;

    src = LoadLibraryExA(from, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!src) {
        launcher_log("icon: cannot read %s, error %lu", from, GetLastError());
        return 0;
    }

    g_group_found = 0;
    EnumResourceNamesA(src, RT_GROUP_ICON, first_group, 0);
    if (!g_group_found) {
        launcher_log("icon: %s carries no icon", from);
        FreeLibrary(src);
        return 0;
    }

    header = (GroupHeader *)resource_bytes(src, RT_GROUP_ICON, group_name(), g_group_lang, &size);
    if (!header || size < sizeof(GroupHeader) + sizeof(GroupEntry)) {
        launcher_log("icon: the icon directory in %s could not be read, error %lu",
                     from, GetLastError());
        FreeLibrary(src);
        return 0;
    }

    g_res_update = BeginUpdateResourceA(to, FALSE);
    if (!g_res_update) {
        launcher_log("icon: cannot open %s for resource update, error %lu", to, GetLastError());
        FreeLibrary(src);
        return 0;
    }

    /* The images first, then the directory that names them, so the file is
     * never left describing pictures it does not contain. */
    entry = (GroupEntry *)(header + 1);
    for (i = 0; i < header->count; i++) {
        DWORD image_size = 0;
        void *image;

        if ((DWORD)((char *)(entry + i + 1) - (char *)header) > size)
            break;
        image = resource_bytes(src, RT_ICON, MAKEINTRESOURCEA(entry[i].id),
                               g_group_lang, &image_size);
        if (image && !UpdateResourceA(g_res_update, RT_ICON,
                                      MAKEINTRESOURCEA(entry[i].id),
                                      g_group_lang, image, image_size))
            launcher_log("icon: image %d could not be written, error %lu",
                         entry[i].id, GetLastError());
    }

    if (!UpdateResourceA(g_res_update, RT_GROUP_ICON, group_name(), g_group_lang, header, size))
        launcher_log("icon: the icon directory could not be written, error %lu", GetLastError());

    ok = EndUpdateResource(g_res_update, FALSE) ? 1 : 0;
    if (!ok)
        launcher_log("icon: writing the icon into %s failed, error %lu", to, GetLastError());
    FreeLibrary(src);
    return ok;
}

/* Picks the program to start, and says why when it cannot.
 *
 *    1  found, path is in out
 *    0  no HLSW next to this program at all
 *   -1  hlsw.exe is this launcher and hlsw-real.exe is gone
 *
 * That last case is worth telling apart, because it is what an HLSW update
 * leaves behind: the update writes its own hlsw.exe over the launcher, the
 * launcher survives only under the name it was installed as, and starting
 * "hlsw.exe" from here would mean starting ourselves without end. */
/* True if the file is another copy of this launcher.
 *
 * Without this the launcher will start whatever sits under the name it expects,
 * and if that is a copy of itself it starts another one, without end. An
 * installer that mistook the launcher for HLSW and moved it over hlsw-real.exe
 * did exactly that, and seven thousand processes later the cause was still not
 * obvious from the outside. Cheap to check, so it is checked. */
static int is_this_launcher(const char *path)
{
    DWORD handle = 0, size;
    void *info;
    char *product = NULL;
    UINT len = 0;
    int ours = 0;

    size = GetFileVersionInfoSizeA(path, &handle);
    if (!size)
        return 0;
    info = malloc(size);
    if (!info)
        return 0;
    if (GetFileVersionInfoA(path, 0, size, info)
        && VerQueryValueA(info, "\\StringFileInfo\\040904B0\\ProductName",
                          (LPVOID *)&product, &len)
        && product && _stricmp(product, "hlswfix") == 0)
        ours = 1;
    free(info);
    return ours;
}

static int find_hlsw(const char *dir, char *out, size_t size)
{
    char self[MAX_PATH];

    /* Preferred whenever it exists: it only exists because the launcher was
     * given the name hlsw.exe and the real program had to move aside. */
    snprintf(out, size, "%shlsw-real.exe", dir);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES && !is_this_launcher(out))
        return 1;

    snprintf(out, size, "%shlsw.exe", dir);
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES)
        return 0;

    /* Either literally this file, or another copy of this program. Both would
     * mean starting ourselves. */
    GetModuleFileNameA(NULL, self, sizeof(self));
    if (_stricmp(self, out) == 0 || is_this_launcher(out))
        return -1;
    return 1;
}

static int inject(HANDLE proc, const char *dll_path)
{
    SIZE_T len = strlen(dll_path) + 1;
    void *mem;
    HANDLE thread;
    DWORD result = 0;
    FARPROC load_library;

    mem = VirtualAllocEx(proc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem)
        return 0;
    if (!WriteProcessMemory(proc, mem, dll_path, len, NULL))
        return 0;

    /* Both this program and HLSW are 32 bit, so kernel32 sits at the same
     * address in both and this address is valid over there as well. */
    load_library = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    thread = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)(void *)load_library,
                                mem, 0, NULL);
    if (!thread)
        return 0;

    if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0) {
        /* Still running after fifteen seconds. Its exit code would read as
         * STILL_ACTIVE, which is not zero and would have been taken for the
         * module handle and reported as success. The page it is reading the
         * path out of stays allocated for the same reason: freeing it now
         * would pull the argument out from under a thread that is still using
         * it, and a leaked page is the cheaper of the two. */
        CloseHandle(thread);
        return 0;
    }

    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    VirtualFreeEx(proc, mem, 0, MEM_RELEASE);

    /* The thread returns the module handle, so zero means LoadLibrary failed. */
    return result != 0;
}

/* ----------------------------------------------------------------- update */

/* Looking for a new version of ourselves, and installing it when asked to.
 *
 * This program blocks HLSW's own calls home, so checking in with a server of
 * ours needs answering for rather than hiding. The difference is not that ours
 * is better meant, it is what the two actually do. HLSW sends a packet to
 * s9b.hlsw.org every five seconds for as long as it runs, gets nothing back,
 * cannot be told to stop, and the domain it aims at can change hands. This
 * asks GitHub once per start, for one file, from the same address the user
 * downloaded this from, sends nothing about them or their machine, is one line
 * in hlswfix.ini away from being off, and is written up in the README next to
 * the paragraph about blocking HLSW's.
 *
 * It is on by default, and that is a deliberate answer to the obvious
 * objection. An update check nobody switches on tells nobody anything, and the
 * one number that would have given it away, the version in the title bar,
 * comes out of this program itself and is therefore always right about the
 * version that is running and never about the version that exists.
 *
 * The check runs on a thread, so a slow or dead network cannot keep HLSW
 * waiting. Installing happens while HLSW runs and takes effect at the next
 * start: a file that is open can still be renamed out of the way on Windows,
 * only not deleted, which is exactly enough.
 *
 * hlswfix.ini is never touched. It is the one file a user edits, the installer
 * has always left it alone, and an updater that overwrote it would undo every
 * setting on the machine it was meant to help. */

#define UPDATE_URL   L"https://api.github.com/repos/marcanxo/hlswfix/releases/latest"
#define RELEASE_PAGE "https://github.com/marcanxo/hlswfix/releases/latest"
#define MAX_BODY     (8 * 1024 * 1024)

/* Fetches one address over https and returns the body, which the caller frees.
 * Anything but a plain 200 is treated as nothing to report, because there is
 * nothing sensible to do about it either way. */
static char *https_get(const wchar_t *url, DWORD *out_len)
{
    URL_COMPONENTS uc;
    wchar_t host[256], path[1024], extra[512], full[1536];
    HINTERNET ses = NULL, con = NULL, req = NULL;
    char *body = NULL;
    DWORD cap = 0, total = 0, status = 0, slen = sizeof(status);

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof(host) / sizeof(host[0]);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof(path) / sizeof(path[0]);
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = sizeof(extra) / sizeof(extra[0]);

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return NULL;
    _snwprintf(full, sizeof(full) / sizeof(full[0]) - 1, L"%s%s", path, extra);
    full[sizeof(full) / sizeof(full[0]) - 1] = 0;

    /* The agent string is what GitHub sees. It names the program and nothing
     * else: no version, no machine, no account, nothing that would tell two
     * users apart. */
    ses = WinHttpOpen(L"hlswfix", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
        return NULL;
    WinHttpSetTimeouts(ses, 5000, 5000, 15000, 30000);

    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con)
        req = WinHttpOpenRequest(con, L"GET", full, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req) {
        WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json",
                                 (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(req, NULL)
            && WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                   WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen,
                                   WINHTTP_NO_HEADER_INDEX)
            && status == 200) {
            for (;;) {
                DWORD ready = 0, got = 0;

                if (!WinHttpQueryDataAvailable(req, &ready) || !ready)
                    break;
                if (total + ready + 1 > cap) {
                    char *bigger;

                    cap = total + ready + 1024;
                    if (cap > MAX_BODY) {
                        free(body);
                        body = NULL;
                        total = 0;
                        break;
                    }
                    bigger = (char *)realloc(body, cap);
                    if (!bigger) {
                        free(body);
                        body = NULL;
                        total = 0;
                        break;
                    }
                    body = bigger;
                }
                if (!WinHttpReadData(req, body + total, ready, &got) || !got)
                    break;
                total += got;
            }
            if (body)
                body[total] = 0;
        } else if (g_logging) {
            launcher_log("update: %S answered %lu", host, status);
        }
        WinHttpCloseHandle(req);
    }
    if (con)
        WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);

    if (!body || !total) {
        free(body);
        return NULL;
    }
    *out_len = total;
    return body;
}

/* Just enough JSON to read four flat string fields out of a release. A real
 * parser would be larger than everything else here put together, and there is
 * nothing in these fields that needs one. */
static int json_string(const char *json, const char *key, char *out, size_t cap)
{
    char pattern[64];
    const char *p;
    size_t n = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p)
        return 0;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != ':')
        return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != '"')
        return 0;
    p++;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return n > 0;
}

/* Where in the release the asset with this file name begins. The fields that
 * matter come after the name in what GitHub sends, so reading forward from
 * here finds this asset's own and not the next one's. */
static const char *json_asset(const char *json, const char *filename)
{
    const char *p = json;

    while ((p = strstr(p, "\"name\"")) != NULL) {
        char got[128];

        if (json_string(p, "name", got, sizeof(got)) && strcmp(got, filename) == 0)
            return p;
        p += 6;
    }
    return NULL;
}

/* Address and expected hash of one file in the release.
 *
 * The digest is checked to be this asset's and not the following one's:
 * download address and digest both come after the name, so a release where one
 * asset carries no digest would otherwise silently borrow the next asset's,
 * and a wrong hash that matches something is worse than none at all. */
static int release_asset(const char *json, const char *filename,
                         wchar_t *url, size_t url_cap, char *sha, size_t sha_cap)
{
    const char *a, *dl, *dg;
    char narrow[512], digest[128];

    a = json_asset(json, filename);
    if (!a)
        return 0;
    dl = strstr(a, "\"browser_download_url\"");
    dg = strstr(a, "\"digest\"");
    if (!dl || !dg || dg > dl)
        return 0;
    if (!json_string(dl, "browser_download_url", narrow, sizeof(narrow)))
        return 0;
    if (!json_string(dg, "digest", digest, sizeof(digest)))
        return 0;
    if (strncmp(digest, "sha256:", 7) != 0)
        return 0;
    if (strlen(digest + 7) != 64 || sha_cap < 65)
        return 0;

    strcpy(sha, digest + 7);
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, url, (int)url_cap) == 0)
        return 0;
    return 1;
}

static int sha256_hex(const void *data, DWORD len, char *hex, size_t cap)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    UCHAR digest[32];
    int ok = 0, i;

    if (cap < 65)
        return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
        return 0;
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        if (BCryptHashData(h, (PUCHAR)data, len, 0) == 0
            && BCryptFinishHash(h, digest, sizeof(digest), 0) == 0) {
            for (i = 0; i < 32; i++)
                sprintf(hex + i * 2, "%02x", digest[i]);
            hex[64] = 0;
            ok = 1;
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

/* Four parts, with a leading v allowed because that is how the tags are
 * written. Missing parts count as zero, so 1.7 and 1.7.0.0 compare equal. */
static int parse_version(const char *s, int v[4])
{
    int i;

    if (*s == 'v' || *s == 'V')
        s++;
    for (i = 0; i < 4; i++)
        v[i] = 0;
    for (i = 0; i < 4 && *s; i++) {
        if (*s < '0' || *s > '9')
            return 0;
        while (*s >= '0' && *s <= '9')
            v[i] = v[i] * 10 + (*s++ - '0');
        if (*s == '.')
            s++;
        else
            break;
    }
    return 1;
}

static int version_is_newer(const int a[4], const int b[4])
{
    int i;

    for (i = 0; i < 4; i++) {
        if (a[i] != b[i])
            return a[i] > b[i];
    }
    return 0;
}

/* The version this launcher was built as, out of its own resource, which is
 * the one place the number is written down. */
static int own_version(int v[4], char *text, size_t cap)
{
    char path[MAX_PATH];
    DWORD handle = 0, size;
    void *info;
    VS_FIXEDFILEINFO *fixed = NULL;
    UINT len = 0;
    int ok = 0;

    if (!GetModuleFileNameA(NULL, path, sizeof(path)))
        return 0;
    size = GetFileVersionInfoSizeA(path, &handle);
    if (!size)
        return 0;
    info = malloc(size);
    if (!info)
        return 0;
    if (GetFileVersionInfoA(path, 0, size, info)
        && VerQueryValueA(info, "\\", (LPVOID *)&fixed, &len)
        && fixed && len >= sizeof(*fixed)) {
        v[0] = HIWORD(fixed->dwFileVersionMS);
        v[1] = LOWORD(fixed->dwFileVersionMS);
        v[2] = HIWORD(fixed->dwFileVersionLS);
        v[3] = LOWORD(fixed->dwFileVersionLS);
        snprintf(text, cap, "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
        ok = 1;
    }
    free(info);
    return ok;
}

/* Puts new contents in place of a file that may well be in use.
 *
 * Renaming a running executable or a loaded library is allowed on Windows;
 * deleting one is not. So the old file is moved aside rather than removed, the
 * new one is written under the name everything already points at, and the file
 * that was moved aside is deleted at the next start, by which time nothing
 * holds it any more. Any failure puts the old name back before returning. */
static int swap_in(const char *path, const void *data, DWORD len)
{
    char aside[PATHBUF + 8];
    HANDLE fh;
    DWORD written = 0;

    snprintf(aside, sizeof(aside), "%s.old", path);
    DeleteFileA(aside);
    if (!MoveFileA(path, aside)) {
        launcher_log("update: %s could not be moved aside, error %lu", path, GetLastError());
        return 0;
    }

    fh = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        launcher_log("update: %s could not be written, error %lu", path, GetLastError());
        MoveFileA(aside, path);
        return 0;
    }
    if (!WriteFile(fh, data, len, &written, NULL) || written != len) {
        launcher_log("update: %s was written short, error %lu", path, GetLastError());
        CloseHandle(fh);
        DeleteFileA(path);
        MoveFileA(aside, path);
        return 0;
    }
    CloseHandle(fh);
    return 1;
}

static void swap_back(const char *path)
{
    char aside[PATHBUF + 8];

    snprintf(aside, sizeof(aside), "%s.old", path);
    DeleteFileA(path);
    MoveFileA(aside, path);
}

/* The copies left behind by the last update, gone now that nothing holds
 * them. Failing here is of no consequence and is not reported. */
static void clean_up_after_update(const char *dir, const char *self)
{
    char path[PATHBUF + 8];

    snprintf(path, sizeof(path), "%shlswfix.dll.old", dir);
    DeleteFileA(path);
    snprintf(path, sizeof(path), "%s.old", self);
    DeleteFileA(path);
}

static HANDLE g_hlsw_process;
static DWORD  g_hlsw_pid;
static HWND   g_found_window;

static BOOL CALLBACK find_main_window(HWND hwnd, LPARAM pid)
{
    DWORD owner = 0;

    GetWindowThreadProcessId(hwnd, &owner);
    /* Top level, visible, and belonging to HLSW. GW_OWNER rules out the splash
     * and any dialog of its own that may be up at the time. */
    if (owner != (DWORD)pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    g_found_window = hwnd;
    return FALSE;
}

/* HLSW's main window, waited for rather than guessed at.
 *
 * Without an owner the question is a window of its own, and HLSW's main window
 * appears a moment later and is drawn straight over it. That happened in
 * testing: the dialog was up before HLSW and then simply gone from view.
 *
 * A fixed pause before asking would be the obvious repair and the wrong one.
 * It is a guess either way: too short on a machine that is busy or cold, and
 * wasted time on one that is not. Waiting for the window itself is the same
 * thing said exactly, and it also gives the dialog somewhere to belong, so it
 * can no longer end up behind the program it is talking about.
 *
 * Gives up quietly after fifteen seconds, or as soon as HLSW is gone. Then the
 * question is asked without an owner, which is still better than not at all. */
static HWND hlsw_window(void)
{
    int waited;

    if (!g_hlsw_process)
        return NULL;

    WaitForInputIdle(g_hlsw_process, 15000);
    for (waited = 0; waited < 15000; waited += 200) {
        g_found_window = NULL;
        EnumWindows(find_main_window, (LPARAM)g_hlsw_pid);
        if (g_found_window)
            return g_found_window;
        /* Doubles as the pause and as the check that there is still something
         * to wait for. */
        if (WaitForSingleObject(g_hlsw_process, 200) == WAIT_OBJECT_0)
            return NULL;
    }
    return NULL;
}

typedef HRESULT (WINAPI *TaskDialogIndirect_t)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

/* Being owned by HLSW keeps the dialog above HLSW. This asks for it to be in
 * front of everything else as well, once, at the moment it appears. Windows is
 * entitled to refuse that, and if it does the dialog is still where it should
 * be, just not on top of another program. */
static HRESULT CALLBACK dialog_created(HWND hwnd, UINT msg, WPARAM w, LPARAM l, LONG_PTR ref)
{
    (void)w; (void)l; (void)ref;
    if (msg == TDN_CREATED)
        SetForegroundWindow(hwnd);
    return S_OK;
}

/* Switches on version 6 of the common controls for as long as a dialog is up.
 *
 * The usual way to ask for version 6 is the application manifest, and that
 * route is closed here: MinGW's linker writes its own manifest into every
 * executable it builds and refuses to have a second one beside it, so the
 * build ended with ".rsrc merge failure: multiple non-default manifests" and
 * both landed in the file. Ours therefore sits under resource id 2 and is
 * activated by hand, which is the documented way round and has the pleasant
 * side effect that nothing else about the program changes.
 *
 * Everything here is allowed to fail. Without the context comctl32 is version
 * 5, TaskDialogIndirect is not in it, and the caller falls back to a message
 * box, which is the same thing that would happen on a system old enough not to
 * have the function at all. */
static HANDLE begin_v6(ULONG_PTR *cookie)
{
    ACTCTXW ctx;
    wchar_t self[MAX_PATH];
    HANDLE h;

    *cookie = 0;
    if (!GetModuleFileNameW(NULL, self, MAX_PATH))
        return NULL;

    memset(&ctx, 0, sizeof(ctx));
    ctx.cbSize = sizeof(ctx);
    ctx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ctx.lpSource = self;
    ctx.lpResourceName = MAKEINTRESOURCEW(2);

    h = CreateActCtxW(&ctx);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    if (!ActivateActCtx(h, cookie)) {
        ReleaseActCtx(h);
        return NULL;
    }
    return h;
}

static void end_v6(HANDLE h, ULONG_PTR cookie)
{
    if (!h)
        return;
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(h);
}

#define ANSWER_INSTALL 101
#define ANSWER_PAGE    102
#define ANSWER_LATER   103

/* Asks, in the shape the question deserves: what is new, what it will do, and
 * three plain ways out. Falls back to a message box where the task dialog is
 * not available, which needs comctl32 version 6 and therefore the manifest. */
static int ask(HWND parent, const wchar_t *heading, const wchar_t *detail,
               int offer_install, int icon_is_warning)
{
    TASKDIALOGCONFIG cfg;
    TASKDIALOG_BUTTON buttons[3];
    TaskDialogIndirect_t task_dialog;
    HMODULE comctl;
    HANDLE ctx;
    ULONG_PTR cookie;
    int pressed = ANSWER_LATER, n = 0;

    ctx = begin_v6(&cookie);
    comctl = LoadLibraryA("comctl32.dll");
    task_dialog = comctl ? (TaskDialogIndirect_t)(void *)
                  GetProcAddress(comctl, "TaskDialogIndirect") : NULL;

    if (!task_dialog) {
        int r = MessageBoxW(parent, detail, L"hlswfix",
                            (offer_install ? MB_YESNOCANCEL : MB_OKCANCEL) | MB_ICONINFORMATION);

        if (comctl)
            FreeLibrary(comctl);
        end_v6(ctx, cookie);
        if (offer_install)
            return r == IDYES ? ANSWER_INSTALL : (r == IDNO ? ANSWER_PAGE : ANSWER_LATER);
        return r == IDOK ? ANSWER_PAGE : ANSWER_LATER;
    }

    if (offer_install) {
        buttons[n].nButtonID = ANSWER_INSTALL;
        buttons[n].pszButtonText = L"Install it now\n"
                                   L"Downloads and puts it in place. HLSW keeps running, "
                                   L"and the new version starts with it next time.";
        n++;
    }
    buttons[n].nButtonID = ANSWER_PAGE;
    buttons[n].pszButtonText = L"Open the release page\n"
                               L"See what changed and download it yourself.";
    n++;
    buttons[n].nButtonID = ANSWER_LATER;
    buttons[n].pszButtonText = L"Not now";
    n++;

    memset(&cfg, 0, sizeof(cfg));
    cfg.cbSize = sizeof(cfg);
    cfg.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION
                | (parent ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
    cfg.hwndParent = parent;
    cfg.pfCallback = dialog_created;
    cfg.pszWindowTitle = L"hlswfix";
    cfg.pszMainIcon = icon_is_warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    cfg.pszMainInstruction = heading;
    cfg.pszContent = detail;
    cfg.cButtons = (UINT)n;
    cfg.pButtons = buttons;
    cfg.nDefaultButton = offer_install ? ANSWER_INSTALL : ANSWER_PAGE;

    if (task_dialog(&cfg, &pressed, NULL, NULL) != S_OK)
        pressed = ANSWER_LATER;

    if (comctl)
        FreeLibrary(comctl);
    end_v6(ctx, cookie);
    return pressed;
}

static void tell(HWND parent, const wchar_t *heading, const wchar_t *detail, int warning)
{
    TASKDIALOGCONFIG cfg;
    TaskDialogIndirect_t task_dialog;
    HMODULE comctl;
    HANDLE ctx;
    ULONG_PTR cookie;

    ctx = begin_v6(&cookie);
    comctl = LoadLibraryA("comctl32.dll");
    task_dialog = comctl ? (TaskDialogIndirect_t)(void *)
                  GetProcAddress(comctl, "TaskDialogIndirect") : NULL;
    if (!task_dialog) {
        MessageBoxW(parent, detail, L"hlswfix",
                    MB_OK | (warning ? MB_ICONWARNING : MB_ICONINFORMATION));
    } else {
        memset(&cfg, 0, sizeof(cfg));
        cfg.cbSize = sizeof(cfg);
        cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION
                    | (parent ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
        cfg.hwndParent = parent;
        cfg.pfCallback = dialog_created;
        cfg.pszWindowTitle = L"hlswfix";
        cfg.pszMainIcon = warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
        cfg.pszMainInstruction = heading;
        cfg.pszContent = detail;
        cfg.dwCommonButtons = TDCBF_OK_BUTTON;
        task_dialog(&cfg, NULL, NULL, NULL);
    }
    if (comctl)
        FreeLibrary(comctl);
    end_v6(ctx, cookie);
}

/* Downloads one file and hands it back only if it is byte for byte what the
 * release says it should be.
 *
 * Worth being honest about what this does and does not prove. It catches a
 * download that arrived damaged or was interfered with on the way. It cannot
 * catch a release that was published from an account that is no longer in the
 * right hands, because the file and the hash come from the same place. The
 * README says so in as many words. */
static void *fetch_verified(const wchar_t *url, const char *want_sha, DWORD *len)
{
    char got[65];
    void *data = https_get(url, len);

    if (!data)
        return NULL;
    if (!sha256_hex(data, *len, got, sizeof(got)) || _stricmp(got, want_sha) != 0) {
        launcher_log("update: the download did not match its published hash, "
                     "expected %s got %s", want_sha, got);
        free(data);
        return NULL;
    }
    return data;
}

static char g_dir[MAX_PATH];
static char g_self[PATHBUF];
static char g_hlsw_real[PATHBUF];

/* Runs on its own thread, so that a slow network, a proxy that never answers
 * or a GitHub outage cannot keep HLSW from starting. */
static DWORD WINAPI check_update(LPVOID unused)
{
    char have_text[32], tag[64], sha_dll[65], sha_exe[65];
    char *json;
    wchar_t url_dll[512], url_exe[512], heading[128], detail[1024];
    int have[4], want[4];
    DWORD len = 0, dll_len = 0, exe_len = 0;
    int can_install, answer;
    void *dll_data = NULL, *exe_data = NULL;
    char dll_path[PATHBUF];
    HWND parent;

    (void)unused;

    if (!own_version(have, have_text, sizeof(have_text)))
        return 0;

    json = https_get(UPDATE_URL, &len);
    if (!json) {
        if (g_logging)
            launcher_log("update: no answer from GitHub, nothing to do");
        return 0;
    }

    if (!json_string(json, "tag_name", tag, sizeof(tag))
        || !parse_version(tag, want)) {
        launcher_log("update: the latest release carries no version we can read");
        free(json);
        return 0;
    }

    if (!version_is_newer(want, have)) {
        if (g_logging)
            launcher_log("update: %s is the latest, nothing to do", have_text);
        free(json);
        return 0;
    }

    /* A release that ships only the archive can still be pointed at, it just
     * cannot be installed from here. Both files have to be there and both have
     * to carry a hash, or the offer is not made at all. */
    can_install = release_asset(json, "hlswfix.dll", url_dll,
                                sizeof(url_dll) / sizeof(url_dll[0]), sha_dll, sizeof(sha_dll))
               && release_asset(json, "hlswfix.exe", url_exe,
                                sizeof(url_exe) / sizeof(url_exe[0]), sha_exe, sizeof(sha_exe));
    free(json);

    launcher_log("update: %s is available, running %s%s", tag, have_text,
                 can_install ? "" : " (that release carries no loose files, "
                                    "so only the page is offered)");

    _snwprintf(heading, 127, L"hlswfix %S is available", tag);
    heading[127] = 0;
    _snwprintf(detail, 1023,
               L"This HLSW is running hlswfix %S.\n\n"
               L"%s"
               L"Your hlswfix.ini is never touched, so every setting in it stays as it is.\n\n"
               L"This check runs once when HLSW starts, asks GitHub for one file, and sends "
               L"nothing about you or this machine. Put update_check = 0 in hlswfix.ini to "
               L"switch it off.",
               have_text,
               can_install
                   ? L"Installing replaces two files, hlswfix.dll and the launcher. HLSW can "
                     L"stay open; the new version takes over the next time it starts.\n\n"
                   : L"That release does not carry the two files on their own, so it has to be "
                     L"downloaded from the page.\n\n");
    detail[1023] = 0;

    /* Only now, once there is something to say. Waiting for HLSW's window
     * before there is a question would hold this thread up for nothing. */
    parent = hlsw_window();
    answer = ask(parent, heading, detail, can_install, 0);

    if (answer == ANSWER_PAGE) {
        ShellExecuteA(NULL, "open", RELEASE_PAGE, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    if (answer != ANSWER_INSTALL)
        return 0;

    dll_data = fetch_verified(url_dll, sha_dll, &dll_len);
    if (dll_data)
        exe_data = fetch_verified(url_exe, sha_exe, &exe_len);

    if (!dll_data || !exe_data) {
        free(dll_data);
        free(exe_data);
        if (ask(parent, L"The update could not be downloaded",
                L"Either the download did not finish, or what arrived did not match the "
                L"hash published with the release. Nothing on this machine was changed.\n\n"
                L"The release page has the same files to download by hand.",
                0, 1) == ANSWER_PAGE)
            ShellExecuteA(NULL, "open", RELEASE_PAGE, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }

    snprintf(dll_path, sizeof(dll_path), "%shlswfix.dll", g_dir);

    if (!swap_in(dll_path, dll_data, dll_len)) {
        free(dll_data);
        free(exe_data);
        tell(parent, L"The update could not be installed",
             L"hlswfix.dll could not be replaced. Nothing was changed. The most likely "
             L"reason is that the HLSW folder is not writable by this account.", 1);
        return 0;
    }
    if (!swap_in(g_self, exe_data, exe_len)) {
        swap_back(dll_path);
        free(dll_data);
        free(exe_data);
        tell(parent, L"The update could not be installed",
             L"The launcher could not be replaced, so hlswfix.dll was put back as it was "
             L"and nothing has changed. The most likely reason is that the HLSW folder is "
             L"not writable by this account.", 1);
        return 0;
    }

    free(dll_data);
    free(exe_data);

    /* The launcher wears HLSW's icon, which is stamped into it at install time
     * out of the user's own copy of HLSW rather than shipped with us. A fresh
     * one off GitHub has the plain icon, so it is stamped again here. The file
     * just written is not the one that is running, so it can be opened for
     * writing; the running image lives under the .old name now. */
    if (g_hlsw_real[0] && GetFileAttributesA(g_hlsw_real) != INVALID_FILE_ATTRIBUTES)
        copy_icon(g_hlsw_real, g_self);

    launcher_log("update: %s installed", tag);
    tell(parent, L"Update installed",
         L"Close HLSW and start it again to run the new version. Nothing else is needed, "
         L"and hlswfix.ini was left exactly as it was.", 0);
    return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR args, int show)
{
    char dir[MAX_PATH], target[PATHBUF], dll[PATHBUF], cmdline[1024], tunnel_cmd[512];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi, tunnel_pi;
    HANDLE job = NULL, updater = NULL;
    int have_tunnel, tunnel_port = 0, started = 0;

    (void)inst; (void)prev; (void)show;

    /* Called by install.ps1, never by a user. Does its one job and leaves. */
    if (__argc == 4 && strcmp(__argv[1], "--copy-icon") == 0) {
        own_dir(dir, sizeof(dir));
        snprintf(g_log_path, sizeof(g_log_path), "%shlswfix.log", dir);
        return copy_icon(__argv[2], __argv[3]) ? 0 : 1;
    }

    own_dir(dir, sizeof(dir));
    snprintf(dll, sizeof(dll), "%shlswfix.dll", dir);
    snprintf(g_log_path, sizeof(g_log_path), "%shlswfix.log", dir);

    /* Whatever the last update moved aside is free now. */
    GetModuleFileNameA(NULL, g_self, sizeof(g_self));
    lstrcpynA(g_dir, dir, sizeof(g_dir));
    clean_up_after_update(dir, g_self);

    switch (find_hlsw(dir, target, sizeof(target))) {
    case 0:
        MessageBoxA(NULL,
                    "HLSW was not found next to this program.\r\n\r\n"
                    "hlswfix.exe, hlswfix.dll and hlswfix.ini belong in the "
                    "folder HLSW is installed in, the one that has hlsw.exe "
                    "in it.",
                    "hlswfix", MB_OK | MB_ICONERROR);
        return 1;
    case -1:
        MessageBoxA(NULL,
                    "hlsw.exe is this launcher, and hlsw-real.exe is missing.\r\n\r\n"
                    "An HLSW update has most likely overwritten the launcher. "
                    "Run install.ps1 again to put things back, or reinstall "
                    "HLSW and then run it.",
                    "hlswfix", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* The tunnel first, so it is already up by the time anyone clicks rcon. */
    have_tunnel = read_config(dir, tunnel_cmd, sizeof(tunnel_cmd), &tunnel_port);

    /* find_hlsw has just told us which arrangement this is, and target is the
     * real HLSW either way, which is where the launcher's icon comes from if
     * it ever has to be stamped again. */
    lstrcpynA(g_hlsw_real, target, sizeof(g_hlsw_real));
    if (have_tunnel) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&tunnel_pi, 0, sizeof(tunnel_pi));

        job = CreateJobObjectA(NULL, NULL);
        if (job) {
            memset(&limits, 0, sizeof(limits));
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits));
        }
        /* This launcher is 32 bit, and a 32 bit process reaching into
         * C:\Windows\System32 is silently redirected to SysWOW64, where 64 bit
         * only tools do not exist. The Windows ssh client lives in
         * System32\OpenSSH and has no 32 bit counterpart, so spawning it from
         * here failed with "file not found" even though it sits in the PATH.
         * Redirection is switched off for the moment of the call and straight
         * back on afterwards. */
        {
            HMODULE k32 = GetModuleHandleA("kernel32.dll");
            BOOL (WINAPI *disable_redir)(PVOID *) =
                (BOOL (WINAPI *)(PVOID *))(void *)GetProcAddress(k32, "Wow64DisableWow64FsRedirection");
            BOOL (WINAPI *revert_redir)(PVOID) =
                (BOOL (WINAPI *)(PVOID))(void *)GetProcAddress(k32, "Wow64RevertWow64FsRedirection");
            PVOID redir_state = NULL;
            int redir_off = 0;

            if (disable_redir && disable_redir(&redir_state))
                redir_off = 1;

            started = CreateProcessA(NULL, tunnel_cmd, NULL, NULL, FALSE,
                                     CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                     NULL, dir, &si, &tunnel_pi);

            if (redir_off && revert_redir)
                revert_redir(redir_state);
        }

        if (started) {
            if (job)
                AssignProcessToJobObject(job, tunnel_pi.hProcess);
            ResumeThread(tunnel_pi.hThread);
            CloseHandle(tunnel_pi.hThread);
            if (g_logging)
                launcher_log("rcon tunnel started: %s", tunnel_cmd);
            if (tunnel_port && !wait_for_port(tunnel_port, 10000))
                launcher_log("the rcon tunnel never started listening on 127.0.0.1:%d, "
                             "so rcon will not work. Command was: %s", tunnel_port, tunnel_cmd);
            else if (g_logging)
                launcher_log("rcon tunnel listening on 127.0.0.1:%d", tunnel_port);
        } else {
            launcher_log("the rcon tunnel could not be started (Windows error %lu). "
                         "Command was: %s", GetLastError(), tunnel_cmd);
            have_tunnel = 0;
        }
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    /* Pass on whatever arguments we were given, so nothing is lost by sitting
     * in front of the real program. */
    if (args && *args)
        snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", target, args);
    else
        snprintf(cmdline, sizeof(cmdline), "\"%s\"", target);

    if (!CreateProcessA(target, cmdline, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, dir, &si, &pi))
        die("HLSW could not be started.");

    if (!inject(pi.hProcess, dll)) {
        /* Better a working HLSW that shows timeouts than none at all, so the
         * process is resumed either way and the user is simply told. */
        MessageBoxA(NULL,
                    "hlswfix.dll could not be loaded into HLSW.\r\n\r\n"
                    "HLSW starts anyway, but servers will show as timed out.",
                    "hlswfix", MB_OK | MB_ICONWARNING);
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    /* Now, and not earlier: the check ends in a question, and a question needs
     * HLSW's window to belong to or it disappears behind it. Started here it
     * still holds nothing up, because the asking waits and the starting does
     * not. */
    if (g_update_check) {
        g_hlsw_process = pi.hProcess;
        g_hlsw_pid = pi.dwProcessId;
        updater = CreateThread(NULL, 0, check_update, NULL, 0, NULL);
        if (!updater && g_logging)
            launcher_log("update: the check could not be started, error %lu", GetLastError());
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    if (have_tunnel) {
        TerminateProcess(tunnel_pi.hProcess, 0);
        CloseHandle(tunnel_pi.hProcess);
    }
    if (job)
        CloseHandle(job);

    /* HLSW is gone, but the update question may still be on the screen, or a
     * download may be halfway through. Leaving now would close the one and
     * abandon the other, so this process stays for as long as its own dialog
     * does. The tunnel is already down by this point, so nothing is being held
     * open on its account. */
    if (updater) {
        WaitForSingleObject(updater, INFINITE);
        CloseHandle(updater);
    }

    /* Closed last of all, because the update thread holds it too. */
    CloseHandle(pi.hProcess);

    return 0;
}
