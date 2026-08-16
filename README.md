# hlswfix

Brings HLSW back to life against modern game servers.

HLSW was last built in 2011 and shows every server as timed out today. This
fixes that. HLSW itself is not modified: nothing on disk changes, the fix is a
small library that is injected at startup and redirects a handful of winsock
functions in memory.

Works for any server HLSW can query, Source and GoldSrc alike. It is not tied
to any particular server, and it needs no configuration.

## Getting HLSW

You need HLSW itself, which is not part of this project. The official site is
effectively gone: hlsw.net and hlsw.org still resolve, both to the same
address, but hlsw.net answers nothing at all and hlsw.org returns 403. There
has been no official download for years, and what turns up in a search engine
instead is mostly download portals that wrap installers in their own.

So the last release is mirrored on the [releases page][releases] of this
repository, unchanged, exactly as installed here. That is allowed: `license.txt`
inside HLSW says so in as many words.

> This program is Copyright (c) 2000-2009 by Stripf Software. It is FREEWARE
> which may be freely copied and distributed onto and through any and all
> computer media.

Verify whatever you download, from here or anywhere else:

| file | SHA-256 | size |
|---|---|---|
| `hlsw_1_4_0_5_setup.exe` | `525b20c89b585704067bb38ae65ff817c2753bd20d680361c840ddce69dc15a7` | 12,034,292 |
| `hlsw.exe` (installed by it) | `ab1ca2abdc25879c7759da8549e386ae7d40fbd4258e12b57a9c820201bf178e` | 20,813,824 |

    certutil -hashfile hlsw_1_4_0_5_setup.exe SHA256

Two things to be honest about. The installer is not code signed, and the
Internet Archive only ever captured the redirect for these files and not the
files themselves, so there is no signature and no archived original to check a
copy against. What the hashes above give you is that everybody gets the same
bytes, and that those bytes are the ones this fix was developed and tested
against. What was checked here: the version resources, the contents of
`license.txt`, the installer's behaviour, and that the result runs.

The installer is also slightly confusing about its own version, and always was.
It is distributed as 1.4.0.5, the about box says 1.4.0.5, and the version
resource in `hlsw.exe` says 1.4.0.4. That is how the developers shipped it.

## Installing

Install HLSW first. Then unpack this archive anywhere and **double click
`install.cmd`**. It finds your HLSW folder in the registry, copies two files
into it, renames `hlsw.exe` to `hlsw-real.exe` and puts the launcher in its
place, so every shortcut, start menu entry and file association that already
exists starts HLSW with the fix in it, without knowing anything about the fix.

**`uninstall.cmd`** puts all of it back exactly as it was.

Run `install.cmd` again after an HLSW update. An update writes its own
`hlsw.exe`, which undoes the arrangement and brings the timeouts back. The
script looks at what `hlsw.exe` actually is rather than assuming, so running it
twice is harmless.

There is no setup program, and that is on purpose. This thing injects a
library into another process, which is exactly what you should want to be able
to read before running. `install.cmd` is six lines and starts `install.ps1`,
which is a text file. Nothing goes into Program Files, the registry or the
start menu.

If you would rather nothing at all be renamed, unpack `hlswfix.exe`,
`hlswfix.dll` and `hlswfix.ini` into the HLSW folder yourself and start
`hlswfix.exe` instead of `hlsw.exe`. It works the same way, only your existing
shortcuts keep starting the unfixed HLSW. Undoing that means deleting three
files.

## What was broken

In December 2020 Valve made `A2S_INFO` require a challenge, so that game
servers could no longer be used as UDP reflection amplifiers. A client sends
the query, the server answers with a 9 byte `S2C_CHALLENGE` packet (`0x41`),
and only a repeat of the query carrying those four challenge bytes gets the
real `0x49` info reply.

HLSW knows nothing about that step. It sends the plain query, cannot parse what
comes back, and shows every server as timed out with no ping. That single
missing round trip is the whole problem. `A2S_PLAYER` and `A2S_RULES` always
needed a challenge and HLSW does that handshake correctly, and rcon is TCP and
has not changed at all.

It cannot be fixed on the server. `sv_enableoldqueries` looks like the switch
for it and is not: measured against a live server, `A2S_INFO` still answers
`0x41` with that cvar set to 1, because queries are answered by the Steam game
server API inside `steamclient.so`, where no engine cvar reaches.

## What it does

| hook | why |
|---|---|
| `sendto`, `send` | Note that an `A2S_INFO` went out, pace it, and attach an already known challenge right away, so repeat queries cost one round trip and the ping HLSW displays stays honest instead of doubling. |
| `recvfrom`, `recv`, `WSARecvFrom`, `WSARecv` | Catch the `0x41` that answers such a query, put the repeat on the wire as a side effect, and hand HLSW exactly what arrived. HLSW never learns anything happened. |
| `connect` | Only used for the optional rcon redirect below. Stream sockets only. |
| `SetWindowTextW` | Only used for the optional version in the title. |

All four receive functions are hooked because HLSW uses all of them: the plain
ones for the burst of probes it sends when a server is added, the `WSA` ones
once it settles into monitoring. Hooking only `recvfrom` worked for about a
minute, until the first challenge rotation.

Challenges that HLSW asked for itself are always passed through untouched.
Losing one of those would cost it the player list, while a challenge the shim
loses only costs an extra round trip.

## Pacing HLSW's queries

HLSW asks the server it is watching again the instant the previous answer
arrives. Measured: around eighty queries a second for each of info, players and
rules, with the rules answer alone coming back as two hundred and fifty
datagrams a second. A Source server answers `sv_max_queries_sec` queries per
address, three a second by default, and silently drops everything beyond that,
so HLSW's own refresh rate is what makes the server it is watching appear to
time out. `ServerAutoUpdateRate` in the registry does not govern that loop.

`query_interval_ms`, one second by default, holds each kind of query to one per
interval per server, which comes to the three a second a server will answer
without dropping any. In practice this discards about ninety four per cent of
what HLSW tries to send, and the traffic to one server fell from 222 packets a
second to under 3.

A held back query is dropped, with nothing sent in its place. Two other designs
were tried first and both were worse:

*Delaying the call* is not available at all. HLSW does its socket work on the
thread that owns its window, so any wait inside a hook freezes the interface.

*Answering the query locally*, from the previous reply, works but lies about
the ping. HLSW times a query from its own send to the arrival of the answer, so
a locally produced answer reports the time it took to produce it: every paced
server showed 1 to 3 ms, while the one being watched, whose queries go out for
real, showed its true 26. Dropping instead leaves the displayed ping as the
last genuine measurement, and HLSW does not read the missing answers as a
timeout, because a real one still arrives every interval.

## Settings

Everything in `hlswfix.ini` is optional, including the file itself. The actual
fix needs no configuration. See the comments in it for the rest.

`title_version` rewrites the version HLSW shows in its title bar. HLSW builds
that string from its own version resource, and this changes only what is
displayed, so the program's files stay exactly as they were shipped and the
change travels with this library rather than with a modified executable.

`rcon_redirect` sends the rcon connection for one server to a local port
instead of straight at it, for when the rcon port is firewalled off and you
reach it through an ssh tunnel. `tunnel_command` starts and stops that tunnel
along with HLSW. Queries are never diverted, so HLSW still shows and queries
the server's real address. Worth doing even where the port is open, because
Source rcon sends its password in clear text.

## Antivirus, and reading the code

The launcher starts HLSW suspended, writes a path into it and calls
`LoadLibraryA` over there through `CreateRemoteThread`. That is the textbook
shape of dll injection, it is exactly what antivirus software is trained to
notice, and there is no way to do this job without it. The executables are also
not code signed, so SmartScreen will call the publisher unknown.

Nothing here is hidden from you. It is about 1500 lines of C in [src](src),
it builds in a few seconds, and building it yourself is the shortest way to be
sure of what you are running:

    build.cmd

The build needs a 32 bit compiler, because `hlsw.exe` is PE32: a 64 bit library
cannot be loaded into it, and a 64 bit launcher would look up `LoadLibraryA` at
an address that does not exist over there. A portable MinGW-w64 i686 toolchain
unpacks anywhere and needs no installation. Point `MINGW` at its `bin` folder,
or put `gcc` on the PATH. Output lands in `build\`.

## HLSW talks to servers that are no longer its own

This has nothing to do with the fix, but you should know. From HLSW's own
`license.txt`:

> HLSW transfers data to central servers owned by Stripf Software to enable
> several functionalities in HLSW like version check, console log, location
> detection, login, buddy list, etc.

Those domains are still registered and still point at a host, they just do not
serve anything useful any more. Whoever holds them can change that at any time,
and a 2011 program will keep talking to them. Leave the login and buddy list
features alone. This project does not touch that traffic in either direction.

## How the functions are redirected, and why not the import tables

The obvious approach is to patch import tables, and it does not work here.
HLSW resolves the winsock functions at run time and calls them through its own
pointers, so its import table is never consulted. This is easy to misdiagnose,
because a hook installed in an import table that nobody reads behaves exactly
like a hook that failed to install. What settled it was reading the patched
entries back out of the running process: they held the hook addresses, `select`
among them, and not one was ever called while packets were plainly going out on
the wire.

So the functions themselves are redirected. Each one begins with the Microsoft
hot patch prologue, `mov edi,edi; push ebp; mov ebp,esp`, which is exactly the
five bytes a `jmp rel32` needs, so the jump can be written over it without ever
cutting an instruction in half. The displaced bytes plus a jump back form a
trampoline that the hook calls to do the original work. That catches every
caller, whatever route it took to the address.

Import patching is kept only as a fallback, for a function that does not carry
that prologue. Nothing currently needs it.

## Three more traps

**HLSW needs the right working directory.** It is an MFC application that looks
for `cfg\Games.cfg` and the rest of its data relative to the current directory,
not relative to its own location. Started from anywhere else it reports its
configuration as missing, which looks like a broken installation and is not.
The launcher sets it.

**Redirect the winsock functions, not their callers, and never both ways
round.** `WSOCK32.DLL` has its own `recv` and `recvfrom` and forwards `send`,
`sendto` and `connect` straight into `WS2_32.DLL`. A hook must therefore call
its trampoline, never the original entry point, or it reaches its own
replacement and recurses until the stack runs out.

**WOW64 redirects a 32 bit process away from System32.** The Windows ssh client
lives in `C:\Windows\System32\OpenSSH` and exists only as 64 bit, so a 32 bit
process asking for it is sent to `SysWOW64\OpenSSH`, which does not exist.
Spawning `ssh` failed with "file not found" although it was plainly in the PATH.
The launcher turns redirection off around that one call.

Several of these failed silently. Anything the launcher cannot do is written to
`hlswfix.log` whatever the `log` setting says, and `log = 2` dumps every packet
as hex, which is the tool that ended the guessing.

## Still gone for good

The web lists and `GamersSearch` point at hlsw.net, so they stay empty and
servers have to be entered by hand. Everything else works, including the large
SourceMod admin interface in `cfg\rcon_sourcemod.cfg`.

## License

This project is [MIT licensed](LICENSE).

It is not connected with HLSW or its authors, and is not endorsed by them.
It contains no HLSW code, and changes no HLSW file on disk. HLSW is
Copyright (c) 2000-2009 Stripf Software and was released as freeware.

[releases]: https://github.com/marcanxo/hlswfix/releases
