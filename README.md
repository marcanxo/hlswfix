**English** · [Deutsch](README.de.md)

# hlswfix

Brings HLSW back to life against modern game servers.

HLSW was last built in 2011 and shows every server as timed out today. This
fixes that, for any server HLSW can query, and needs no configuration.

Not one byte inside HLSW is altered. The fix is a small library that is loaded
into HLSW at startup and corrects what it sends on the network, in memory. The
installer does rename `hlsw.exe` and put files next to it, and that is the
whole of what it touches.

## Install

1. Install HLSW first. If you do not have it, see [Getting HLSW](#getting-hlsw)
   below.
2. Download `hlswfix-1.6.1.zip` from the [releases page][releases]. Not the
   green **Code** button: that gives you the sources without the built files.
3. Unpack it anywhere and close HLSW if it is running.
4. Double click **`install.cmd`**.

That is all. Start HLSW as you always did, add a server by its address, and it
should show a ping instead of a timeout.

HLSW usually sits under `C:\Program Files (x86)`, so Windows will ask for
administrator rights. If it does not ask and the script says it cannot write,
right click `install.cmd` and choose **Run as administrator**. It checks before
it changes anything, so a refused install leaves nothing half done.

**`uninstall.cmd`** puts HLSW back: `hlsw-real.exe` becomes `hlsw.exe` again and
the `hlswfix` files are deleted. Your `hlswfix.ini` is left alone in case you
edited it. By hand it is the same three steps: delete the `hlswfix.*` files,
delete `hlsw.exe` if it is the small one, rename `hlsw-real.exe` back.

Three details worth knowing. While HLSW runs you will now see two processes,
`hlsw.exe` (the launcher, waiting) and `hlsw-real.exe` (HLSW itself). The built
in server lists stay empty, because they pointed at hlsw.net; servers have to
be added by hand. And the login screen is switched off and HLSW stops reporting
to its old home servers, both of which you can turn back on, see
[Settings](#settings).

The launcher takes HLSW's icon during the install, so your shortcuts look the
way they always did. That icon is lifted from the copy of HLSW on your machine
and is not shipped with this.

<details>
<summary>Without renaming anything</summary>

If you would rather nothing at all be renamed, unpack `hlswfix.exe`,
`hlswfix.dll` and `hlswfix.ini` into the HLSW folder yourself and start
`hlswfix.exe` instead of `hlsw.exe`. Same fix, no administrator rights, and
undoing it means deleting those three files plus `hlswfix.log` if one was
written. The only difference is that your existing shortcuts still start the
unfixed HLSW.

This is also the way out if your machine forbids PowerShell scripts.
</details>

<details>
<summary>After reinstalling or updating HLSW</summary>

Run `install.cmd` again. A fresh HLSW writes its own `hlsw.exe` and undoes the
arrangement, and the timeouts come back. The script looks at what `hlsw.exe`
actually is rather than assuming, so running it twice is harmless.
</details>

## If it does not work

**HLSW does not start at all any more.** Most likely your antivirus quarantined
a file. After the install `hlsw.exe` is the launcher, so if that gets taken away
there is nothing left to click. Nothing is lost: `hlsw-real.exe` is your
untouched HLSW. Run `uninstall.cmd`, or restore the file and exclude the HLSW
folder in your scanner, then install again.

**"hlswfix.dll could not be loaded into HLSW".** Same cause almost every time.
Check your antivirus history first. HLSW still starts, it just shows timeouts.

**Servers still time out.** Set `log = 1` in `hlswfix.ini`, start HLSW, and open
`hlswfix.log` next to `hlsw.exe`. A line beginning `attached:` means the fix is
loaded and the trouble is elsewhere. No log file at all means HLSW was started
without the launcher, so check that `hlsw.exe` really is the small one.

**The script cannot find HLSW.** It reads the folder from HLSW's own uninstall
entry, which a copied or moved installation does not have. Tell it where:
`install.cmd -Dir "D:\Games\HLSW"`.

Everything below this line is background. You do not need any of it to use the
fix.

## Getting HLSW

Already have it? Skip this. Any 1.4 installation works, and the checksums below
matter only if you are fetching a copy.

The official site is effectively gone. hlsw.net and hlsw.org still resolve, both
to the same address, but hlsw.net answers nothing at all and hlsw.org returns
403. There has been no official download for years, and what a search engine
offers instead is mostly portals that wrap the installer in their own.

So the last release is on the [releases page][releases] of this repository,
unchanged. That is allowed: `license.txt` inside HLSW says so in as many words.

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
copy against. What these hashes give you is that everybody gets the same bytes,
and that those bytes are the ones this fix was developed and tested against.
What was checked here: the version resources, the contents of `license.txt`, the
installer's behaviour, a virus scan, and that the result runs.

The installer is also confusing about its own version, and always was. It is
distributed as 1.4.0.5, the about box says 1.4.0.5, the entry it leaves in
Windows says 1.4.0.5, and the version resource inside `hlsw.exe` says 1.4.0.4.
That is how the developers shipped it.

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

Twelve functions are redirected inside the HLSW process, and no others:

| redirected | why |
|---|---|
| `sendto` | Note that an `A2S_INFO` went out, pace it, and attach a known challenge right away **to the servers that have shown they require one**, so repeat queries cost one round trip instead of two. |
| `send` | The same, except that nothing is ever held back here. On a connected socket a dropped query would strand the reply. |
| `recvfrom`, `recv`, `WSARecvFrom`, `WSARecv` | Catch the `0x41` that answers such a query, put the repeat on the wire as a side effect, and hand HLSW exactly what arrived. HLSW never learns anything happened. Also where a duplicate answer is made invisible, if `hide_duplicate_info` is on. |
| `connect` | Only for the optional rcon redirect below. Stream sockets only, and it does nothing at all unless `rcon_redirect` is configured. |
| `select` | Nothing but a control point: if it never fires, nothing is reaching the hooks. Always installed, changes no behaviour. |
| `gethostbyname`, `getaddrinfo`, `WSAAsyncGetHostByName` | Refuse to look up hlsw.net and hlsw.org, so HLSW cannot report to servers that are no longer its own. See below. Not installed at all if `block_home_calls = 0`. |
| `SetWindowTextW` | Only for the version in the title bar. |

All four receive functions are hooked because HLSW uses all of them: the plain
ones for the burst of probes it sends when a server is added, the `WSA` ones
once it settles into monitoring. Hooking only `recvfrom` worked for about a
minute, until the first challenge rotation.

What this puts on the network: repeated `A2S_INFO` queries, to the same server
HLSW already asked, and nothing else. No update check, no telemetry, no
connection to anything of ours. The launcher's own only connection is a probe
to `127.0.0.1`, to see whether an rcon tunnel came up, and only if you asked for
one.

Challenges that HLSW asked for itself are passed through untouched, because
losing one would cost it the player list, while a challenge the fix loses only
costs an extra round trip. Such a claim lapses after three seconds, so an
answer that never arrives cannot block the info query for good.

## Pacing HLSW's queries

HLSW asks the server it is watching again the instant the previous answer
arrives. Measured: up to eighty queries a second for each of info, players and
rules, with the rules answer alone coming back as two hundred and fifty
datagrams a second. A Source server answers `sv_max_queries_sec` queries per
address, three a second by default, and silently drops everything beyond that,
so HLSW's own refresh rate is what makes the server it is watching appear to
time out. `ServerAutoUpdateRate` in the registry does not govern that loop.

`query_interval_ms`, one second by default, holds each kind of query to one per
interval per server socket, which comes to the three a second a server will
answer without dropping any. Measured on one server, that took the traffic from
222 packets a second down to under 3.

A held back query is **delayed, not dropped**. It is kept and put on the wire
the moment its window opens, by a thread of the fix's own.

That distinction is the whole design, and getting it wrong is what made servers
show as timed out. HLSW asks again only once an answer has arrived, and while it
believes a query is outstanding it sends nothing at all for about two seconds. A
query that is quietly dropped therefore does not cost one refresh, it costs that
entire deadline. Measured before this was fixed: every server the user was not
currently looking at ran at one real query every 2.04 seconds with an answer
outstanding for 2.03 of them, which HLSW paints as a timeout. In a rig that asks
the way HLSW asks, dropping answered 4 queries out of 8, alternating a 2.5 second
timeout with an answer that arrives instantly because it is the stale one from
the previous window. Delaying answers 8 out of 8.

The cost is the ping in the list. HLSW starts its stopwatch when it hands the
query over, so a query delayed by most of a second is reported as most of a
second. Both together are not possible, because HLSW times from its own call and
asks again immediately. A wrong number in the ping column is a smaller lie than a
server shown as unreachable when it is not. `query_interval_ms = 0` turns the
pacing off entirely for anyone who would rather have honest pings.

Two other designs were tried and are worse:

*Delaying the call itself* is not available. HLSW does its socket work on one
thread with a blocking `recvfrom`, so any wait inside a hook stalls it. That is
also why the delayed queries are sent from a separate thread: HLSW never calls
`select` at all, so there is no call of its own to hang the work on at the one
moment it matters, which is while it sits in `recvfrom` waiting.

*Answering the query locally*, from the previous reply, lies about the ping in
the other direction, reporting 1 to 3 ms because that is genuinely how long a
locally produced answer takes, and it makes HLSW spin: it asks again the instant
it is answered, so an instant answer is an invitation to loop.

## Settings

Everything in `hlswfix.ini` is optional, including the file itself. The fix
needs no configuration. The comments in the file explain each setting; three
are worth repeating here.

**`title_version`** is why the title bar says **HLSW v1.6.1**. The developers'
last release was 1.4.0.5 in 2011, and the new number says at a glance that this
HLSW has the fix in it. Only the displayed string changes: HLSW builds that
title from its own version resource, and the resource is untouched. Comment the
line out and the original version comes back.

**`rcon_redirect`** sends the rcon connection for a server to a local port
instead of straight at it, for when the rcon port is firewalled off and you
reach it through an ssh tunnel. Repeat the line for more servers, up to eight.
Queries are never diverted, so HLSW still shows and queries the real address.
Worth doing even where the port is open, because Source rcon sends its password
in clear text. Use it together with `tunnel_command`, which waits until the port
actually answers: on its own, with nothing listening there, HLSW simply fails to
connect, but with something *else* listening there, your rcon password goes to
it.

**`tunnel_command`** is started as a program every time HLSW starts, with your
rights and without a window, and is stopped again with HLSW. It does not have to
be ssh. Treat `hlswfix.ini` like a startup entry: whoever can write it can run
anything as you.

**`block_home_calls`**, **`skip_login_screen`** and **`hide_duplicate_info`** are
all on by default and each has a section of its own below.

**`log`** writes `hlswfix.log` next to `hlsw.exe`. Plain text, appended, never
rotated. At `1` it names the servers involved, at `2` it holds the packets
themselves, and a tunnel failure is written even at `0`, including your
`tunnel_command` line. If the folder is not writable, nothing is written at all.
`uninstall.cmd` deletes the file.

## Antivirus and SmartScreen

Be prepared for a fight, and know what you are agreeing to.

The launcher starts HLSW suspended, writes a path into it and calls
`LoadLibraryA` over there through `CreateRemoteThread`. The library then
allocates executable memory and writes a jump over the first five bytes of each
winsock function it redirects. That is the textbook shape of dll injection, it
is exactly what scanners are trained to notice, and there is no way to do this
job without it. All of it happens inside the HLSW process: no other process is
touched, and nothing on disk is modified.

The files are not code signed either, so SmartScreen will call the publisher
unknown. What you will see in practice: Windows marks everything unpacked from a
downloaded archive, so the first run of `install.cmd` may bring up a blue
"Windows protected your PC" box. **More info**, then **Run anyway**. Right
clicking the zip, **Properties**, **Unblock** before unpacking avoids it for
every file at once.

Windows Defender was run against the release archive and each file in it, with
current signatures, and found nothing. Another scanner may still object on
sight rather than on behaviour.

The release page lists the SHA-256 of the archive and of every file in it, so
you can check that what you downloaded is what was published. Note that a build
from source will not reproduce those bytes exactly, because the compiler stamps
each build; building it yourself is a replacement for the hashes, not a check
against them.

## Building it yourself

Nothing here is hidden from you. It is about 1500 lines of C in [src](src) and
it builds in a few seconds:

    build.cmd

`install.cmd` is fourteen lines, one of which runs anything: it starts
`install.ps1`, a text file you can read first, with `-NoProfile
-ExecutionPolicy Bypass`. That switch applies to that one call and changes no
setting on your machine. Nothing is installed anywhere except the HLSW folder
itself: no registry entries, no start menu entry, no uninstall entry.

The build needs a 32 bit compiler, because `hlsw.exe` is PE32: a 64 bit library
cannot be loaded into it, and a 64 bit launcher would look up `LoadLibraryA` at
an address that does not exist over there. A portable MinGW-w64 i686 toolchain
unpacks anywhere and needs no installation. Point `MINGW` at its `bin` folder,
or put `gcc` on the PATH. Output lands in `build\`, and `pack.ps1` assembles the
release archive from it.

## HLSW reports to servers that are no longer its own

From HLSW's own `license.txt`:

> HLSW transfers data to central servers owned by Stripf Software to enable
> several functionalities in HLSW like version check, console log, location
> detection, login, buddy list, etc.

Measured rather than taken on trust: HLSW sends a ten byte packet to
`s9b.hlsw.org`, that is `62.75.203.63`, every five seconds for as long as it
runs, and nothing ever comes back. It does that on its own, with no feature of
yours involved.

hlsw.net and hlsw.org are still registered and still point at a host. Whoever
holds them next inherits that traffic, and a program from 2011 will keep
reporting to them without ever asking again. Nothing HLSW does for you needs
any of it.

So `block_home_calls` is on by default. It refuses the name lookups, and it
also refuses the address at the socket, because HLSW caches what it last
resolved under `HKCU\Software\HLSW\Master Server` and would otherwise never
need to look the name up again. Measured over a run afterwards: eight packets
held back, none sent, and the game servers answered exactly as before. Set it
to `0` in `hlswfix.ini` to leave HLSW's own traffic alone.

## The login screen

A fresh installation opens with a login screen for an account on servers that
stopped answering years ago. `skip_login_screen`, on by default, switches it
off. It is HLSW's own setting, `LoginOnStartup` and `AutoLogin` under
`HKCU\Software\HLSW\Management`, so nothing is intercepted and HLSW's own
settings dialog still shows the truth. Turning it back on inside HLSW lasts
until the next start, because this is applied at every start; set
`skip_login_screen = 0` if you want the screen back for good.

## Servers that answer the same query twice

A few servers answer a single `A2S_INFO` twice: once in the format GoldSrc used
before the Source query protocol existed, and once in the modern one. Measured
on one of them, on every query without exception, the old answer arrived after
14 ms and the modern one after 15.

The two disagree on exactly the fields HLSW reads the game and the version from.
The old answer carries no application id and calls itself protocol 47; the
modern one says 48. HLSW understands both and shows whichever landed last, so
the game icon and the version string flip back and forth for as long as such a
server is selected. This is the server's doing, not HLSW's: a plain socket that
knows nothing about either sees the same two answers.

`hide_duplicate_info`, on by default, makes the redundant old copy invisible to
HLSW. Nothing about the delivery changes: the packet is still handed over, same
length, same sender, same instant, and only its type byte becomes one the query
protocol does not use, so HLSW recognises a query protocol packet and then finds
nothing to do with it. Swallowing it and returning the next packet instead would
mean waiting for one that might never arrive, and waiting inside a receive hook
is what broke this once before.

It only ever acts with proof in hand: the same server, on the same socket, must
have answered in the modern format within the last ten seconds. So a server that
speaks nothing but the old format is never touched, and one that stops sending
the modern answer is fully visible again ten seconds later. The cost, when the
modern answer is the one that gets lost on the way, is a single missed refresh,
which is what a lost packet costs anyway. Set `hide_duplicate_info = 0` to see
everything a server sends, exactly as it sends it.

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

Several of these failed silently. A tunnel that will not start, or that starts
and never listens, is now written to `hlswfix.log` whatever the `log` setting
says. Anything worse than that says so in a message box. And `log = 2` dumps
every packet as hex, which is the tool that ended the guessing.

## What was tested

HLSW 1.4.0.5 on Windows 10, against seven servers at once over several hours.
Queries, the player and rules lists, and rcon all work, including the large
SourceMod admin interface in `cfg\rcon_sourcemod.cfg`.

The calls home were measured with `log = 2` before and after, and blocking them
changed nothing else: the game servers answered exactly as before.

The pacing was checked against a 28 minute packet log of real traffic, and the
log overturned what had been assumed about it. HLSW does not poll on a timer, it
asks again the instant an answer arrives, and it sends nothing while it is
waiting. Unthrottled it took the server it was watching to 50 queries a second
and put 176 datagrams a second on the wire across seven servers, with five of
them losing between 8 and 24 per cent of their answers. Throttled by dropping,
every server that was not the one on screen ran at one query every 2.04 seconds
with an answer outstanding for 2.03 of them, which HLSW paints as a timeout.
That is what led to delaying queries instead of dropping them.

`hide_duplicate_info` has a test of its own, because the server that prompted it
stopped answering twice halfway through the afternoon and a test must not depend
on a server's mood. Three fake servers on loopback cover the cases that matter:
one that always answers twice, one that only ever speaks the old format and must
never be touched, and one that stops sending the modern answer so the ten second
window can be seen to expire and the old answer to come back.

GoldSrc works too, and the way that was got wrong first is worth writing down,
because it cost most of a day.

Those servers answer `A2S_INFO` without demanding a challenge at all, so they
never had the problem the fix exists for. The first version attached one anyway,
as soon as it had learned a challenge from the players or rules exchange, and
that looked safe: counted from a packet log against two Counter-Strike 1.6
servers at the time, 29 of 29 and 15 of 15 challenged info queries were
answered. They ignored what they had not asked for.

Until one of them stopped ignoring it. The same server, later the same day,
answered a plain 25 byte `A2S_INFO` four times out of four and a 29 byte one
with a challenge appended zero times out of four. In HLSW it went completely
silent and looked exactly like a network fault, and it was not: it was four
bytes of ours on the end of a query it had understood perfectly well without
them.

So a challenge is now only ever appended to a server that has proved it wants
one, and the only proof accepted is that this server answered a plain
`A2S_INFO` of ours with `0x41`. On top of that, three challenged queries in a
row with no answer at all drop the assumption and the next query goes out plain
again, so a server that changes its mind is followed rather than argued with.

Anything that needed hlsw.net stays broken. The web lists and `GamersSearch`
have nothing to talk to, so they stay empty and servers have to be entered by
hand.

## Translations

HLSW came out of Germany and had a following well beyond it: it shipped
language files for fifteen or so, and the servers it was pointed at were spread
across as many. So translations are welcome.

Copy `README.md`, name it `README.<code>.md` with the usual two letter code,
translate it, and add yourself to the line of languages at the top of every
`README*.md`. Keep the measurements and the file names as they are.

## License

This project is [MIT licensed](LICENSE).

It is not connected with HLSW or its authors, and is not endorsed by them. It
contains no HLSW code and changes the contents of no HLSW file. HLSW is
Copyright (c) 2000-2009 Stripf Software and was released as freeware.

[releases]: https://github.com/marcanxo/hlswfix/releases
