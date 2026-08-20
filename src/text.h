/*
 * The texts hlswfix shows, and the one place they are fetched from.
 *
 * HLSW itself speaks eighteen languages and remembers which one under
 * HKCU\Software\HLSW\Settings\Language, as a full Windows language id: 1031,
 * that is 0x0407, for German as spoken in Germany, and 9 for English. Its own
 * files in cfg\language are named after the two halves of that number, 07-01
 * and 09-00. Only the first half tells the languages apart, so that is the half
 * that is taken. We follow this setting rather than the Windows one, on purpose.
 * Our dialogs appear inside HLSW and belong to its window, so somebody who has
 * set HLSW to German should not be answered in English because Windows happens
 * to be English.
 *
 * The texts live in the executables' own string tables, one per language, which
 * is the mechanism Windows provides for exactly this. No extra file lands in
 * the user's folder, nothing has to be parsed, and the updater carries them
 * along without knowing they exist.
 *
 * Everything goes through text(). That is the point of this header: if the
 * texts ever outgrow a resource table, only this file changes, and not the
 * thirty odd places that show something.
 */

#ifndef HLSWFIX_TEXT_H
#define HLSWFIX_TEXT_H

/* The ids. Kept dense and starting at one, because Windows packs string
 * resources in blocks of sixteen and a gap costs a whole block. */
#define STR_WINDOWS_ERROR        1
#define STR_START_FAILED         2
#define STR_NO_HLSW              3
#define STR_LAUNCHER_IS_HLSW     4
#define STR_INJECT_FAILED        5
/* Far from the rest on purpose: Windows packs string resources sixteen to a
 * block, and this is the only one the library carries, so it gets a block
 * where nothing else has to come along. See the end of strings.rc. */
#define STR_HOOKS_FAILED       100

#define STR_UPDATE_AVAILABLE     7
#define STR_UPDATE_RUNNING       8
#define STR_UPDATE_WHAT_INSTALL  9
#define STR_UPDATE_PAGE_ONLY    10
#define STR_UPDATE_PRIVACY      11
#define STR_BTN_INSTALL         12
#define STR_BTN_PAGE            13
#define STR_BTN_LATER           14

#define STR_DOWNLOAD_FAILED     15
#define STR_DOWNLOAD_FAILED_WHY 16
#define STR_UNPACK_FAILED       17
#define STR_UNPACK_FAILED_WHY   18
#define STR_INSTALL_FAILED      19
#define STR_INSTALL_FAILED_DLL  20
#define STR_INSTALL_FAILED_EXE  21
#define STR_INSTALLED           22
#define STR_INSTALLED_WHY       23
#define STR_INSTALLED_OFFER     24
#define STR_BTN_RESTART         25
#define STR_BTN_RESTART_LATER   26

#define STR_COUNT               26

#ifndef RC_INVOKED

/* Where the tables are read from. The launcher passes its own image, the
 * library passes itself, because each carries its own copy. */
static HMODULE g_text_module;
static WORD    g_text_language;

/* Round robin, because a task dialog wants a heading, a body and three button
 * texts alive at the same time. Eight is comfortably more than the six that
 * are ever needed at once, and a text is only ever used between fetching it
 * and showing the dialog. */
#define TEXT_SLOTS 8
static wchar_t g_text_slot[TEXT_SLOTS][1400];
static int     g_text_next;

/* One string out of one language's table, copied out and terminated.
 *
 * String resources are stored sixteen to a block, each as a length followed by
 * that many characters and no terminator, which is why this cannot simply
 * return a pointer into the resource. */
static const wchar_t *text_from(WORD lang, unsigned id)
{
    HRSRC found;
    HGLOBAL held;
    const WORD *p;
    unsigned i;
    wchar_t *out;
    unsigned len;

    if (!g_text_module)
        return NULL;
    found = FindResourceExW(g_text_module, (LPCWSTR)RT_STRING,
                            MAKEINTRESOURCEW(id / 16 + 1), lang);
    if (!found)
        return NULL;
    held = LoadResource(g_text_module, found);
    if (!held)
        return NULL;
    p = (const WORD *)LockResource(held);
    if (!p)
        return NULL;

    for (i = 0; i < (id & 15); i++)
        p += 1 + *p;
    len = *p;
    if (!len)
        return NULL;

    out = g_text_slot[g_text_next];
    g_text_next = (g_text_next + 1) % TEXT_SLOTS;
    if (len > (sizeof(g_text_slot[0]) / sizeof(wchar_t)) - 1)
        len = (sizeof(g_text_slot[0]) / sizeof(wchar_t)) - 1;
    memcpy(out, p + 1, len * sizeof(wchar_t));
    out[len] = 0;
    return out;
}

/* The text for an id, in the language that was chosen, falling back to English
 * for anything a translation has not caught up with yet, and to an empty
 * string rather than a null pointer so that no caller can be caught out. */
static const wchar_t *text(unsigned id)
{
    const wchar_t *s = NULL;

    if (g_text_language)
        s = text_from(g_text_language, id);
    if (!s)
        s = text_from(MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL), id);
    return s ? s : L"";
}

/* Which language HLSW is set to, or the one Windows is in, or English.
 *
 * A language is only chosen if there is really a table for it, so a half added
 * translation cannot leave someone staring at empty dialogs; the fallback in
 * text() then never has to do the work alone.
 *
 * The caller says which id to look for, because the two binaries do not carry
 * the same texts: the library holds only the one message it can ever show.
 * Probing for something that is not there would have left it speaking English
 * whatever HLSW was set to, and nothing would have looked wrong. */
static void text_init(HMODULE module, unsigned probe)
{
    HKEY key;
    DWORD value = 0, size = sizeof(value), type = 0;
    WORD want = 0;

    g_text_module = module;
    g_text_language = 0;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\HLSW\\Settings", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, "Language", NULL, &type, (LPBYTE)&value, &size)
                == ERROR_SUCCESS && type == REG_DWORD)
            /* The low ten bits, which is the primary language: the value is
             * a full language id and 0x0407 and 0x0807 are both German. */
            want = (WORD)(value & 0x3FF);
        RegCloseKey(key);
    }
    if (!want)
        want = (WORD)PRIMARYLANGID(GetUserDefaultUILanguage());

    /* The tables are filed under the primary language with no sublanguage,
     * because that is all HLSW records and all we need to tell them apart. */
    if (want && text_from(MAKELANGID(want, SUBLANG_NEUTRAL), probe))
        g_text_language = MAKELANGID(want, SUBLANG_NEUTRAL);
}

#endif /* RC_INVOKED */
#endif /* HLSWFIX_TEXT_H */
