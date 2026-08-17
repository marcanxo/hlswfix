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
 *   3. waits for HLSW to close, then takes the tunnel down again.
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

/* Room for a full length directory plus one of the file names appended to it,
 * so that none of the paths built below can be cut short. */
#define PATHBUF (MAX_PATH + 32)

static char g_log_path[PATHBUF];
static int  g_logging;

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

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR args, int show)
{
    char dir[MAX_PATH], target[PATHBUF], dll[PATHBUF], cmdline[1024], tunnel_cmd[512];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi, tunnel_pi;
    HANDLE job = NULL;
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
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);

    if (have_tunnel) {
        TerminateProcess(tunnel_pi.hProcess, 0);
        CloseHandle(tunnel_pi.hProcess);
    }
    if (job)
        CloseHandle(job);

    return 0;
}
