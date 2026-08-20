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
2. Download `hlswfix-1.8.2.0.zip` from the [releases page][releases]. Not the
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

The limit allows a run-up: six queries of a kind may go out back to back after a
quiet spell, and only then does the rate apply. Selecting a server makes HLSW
fire everything it knows at it at once, and each challenge that comes back earns
another query straight away, so the opening exchange is six attempts inside a
quarter of a second. A limit without an allowance turned exactly that into
refusals, and the server you had just clicked was the one showing a timeout.

**What HLSW is told about a held query decides what the ping column means**, and
it is the one thing here worth understanding.

**Refused** is the default. The send is turned down with the error that means
"nothing went out, try again shortly", so HLSW knows no answer is coming, never
sits waiting for one, and times only queries that really went out. The ping
column then tells the truth: 11 to 30 ms against seven servers. HLSW takes the
refusal calmly, waiting one to three seconds before asking again.

`refuse_held_queries = 0` **delays** the query instead: reported as sent, and
really sent when its window opens, by a thread of the fix's own. Nothing is ever
refused, but HLSW starts its stopwatch when it hands the query over, so the ping
column then reads about one interval rather than the round trip.

Refusing is not free, and both of its costs were found by trying it rather than
by reasoning, because HLSW's source is lost and there was no other way to learn
how it reacts. HLSW writes every refused send into its status bar as `ERROR in
CHLSWSocket::SendTo: (10035) A non-blocking socket operation could not be
completed immediately`, so the footer fills up on a program that is working
perfectly, and selecting a server in the list can briefly flash a timeout for it.
Neither breaks anything, and the ping is what a server browser is read for: a
wrong ping is a wrong answer, a noisy footer is only untidy.

Either way, whatever is not paced at all, meaning every query outside Source and
GoldSrc, always shows its real ping.

Getting this wrong the first time is what made servers show as timed out, and
the reason is worth writing down. HLSW asks again only once an answer has
arrived, and while it believes a query is outstanding it sends nothing at all
for about two seconds. A query that is quietly **dropped** therefore does not
cost one refresh, it costs that entire deadline. Measured before it was fixed:
every server the user was not currently looking at ran at one real query every
2.04 seconds with an answer outstanding for 2.03 of them, which HLSW paints as a
timeout. In a rig that asks the way HLSW asks, dropping answered 4 queries out
of 8, alternating a 2.5 second timeout with an answer that arrives instantly
because it is the stale one from the previous window.

Two further designs were tried and are worse:

*Delaying the call itself* is not available. HLSW does its socket work on one
thread with a blocking `recvfrom`, so any wait inside a hook stalls it. That is
also why delayed queries are sent from a separate thread: HLSW never calls
`select` at all, so there is no call of its own to hang the work on at the one
moment it matters, which is while it sits in `recvfrom` waiting.

*Answering the query locally*, from the previous reply, lies about the ping in
the other direction, reporting 1 to 3 ms because that is genuinely how long a
locally produced answer takes, and it makes HLSW spin: it asks again the instant
it is answered, so an instant answer is an invitation to loop.

**Known and left alone:** clicking through the server list can still flash a
timeout on the server being selected. The allowance covers the ordinary opening
exchange but not every shape of it. Nothing is wrong with the server, and it
clears by itself within a couple of seconds.

## Settings

Everything in `hlswfix.ini` is optional, including the file itself. The fix
needs no configuration. The comments in the file explain each setting; three
are worth repeating here.

**`title_version`** is why the title bar says **HLSW v1.8.2.0**. The developers'
last release was 1.4.0.5 in 2011, and the new number says at a glance that this
HLSW has the fix in it. You do not have to set it: with the line left out, the
version of the fix itself is shown, read from its own file, so it stays right
after an update without anyone maintaining it. That matters because the
installer never overwrites `hlswfix.ini`, on purpose, so a number written in
there would go stale the moment you upgrade. Only the displayed string changes:
HLSW builds that title from its own version resource, and the resource is
untouched. `title_version =` with nothing after it puts HLSW's own version back.

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

**`block_home_calls`**, **`skip_login_screen`**, **`hide_duplicate_info`**,
**`fix_dead_links`** and **`update_check`** are all on by default and each has a
section of its own below.

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

The updater adds a second thing scanners dislike: downloading a file and putting
it in place of one that is already there. On its own that is unremarkable, and
next to dll injection in the same program it is closer to the shape of a
downloader than either half would be alone. It is worth knowing why the usual
comparison does not carry: other programs offer exactly this dialog and are not
flagged for it, but they are ordinary programs that hand a downloaded installer
to Windows. This one injects. Nothing about that is hidden here, the code is in
[src/launcher.c](src/launcher.c) under `check_update`, and `update_check = 0`
switches the whole thing off.

Code signing would settle most of this and is not realistically available: an
ordinary certificate costs a few hundred a year and still starts without
reputation, the kind that satisfies SmartScreen immediately wants a company
behind it, and the cheap modern route wants a verifiable business history that a
hobby project does not have. So the honest position is that this may be flagged,
that the sources are here to read, and that a false positive can be reported to
Microsoft and usually clears within a day or two.

The release page lists the SHA-256 of the archive and of every file in it, so
you can check that what you downloaded is what was published. Note that a build
from source will not reproduce those bytes exactly, because the compiler stamps
each build; building it yourself is a replacement for the hashes, not a check
against them.

## It speaks HLSW's language

Everything hlswfix puts on the screen comes in the seventeen languages HLSW
itself ships: Chinese, Czech, Danish, Dutch, English, Finnish, French, German,
Hungarian, Norwegian, Polish, Portuguese, Russian, Slovak, Spanish, Swedish and
Turkish.

**It follows HLSW's setting, not the Windows one.** HLSW keeps its choice in
`HKCU\Software\HLSW\Settings\Language` and names its own files in
`cfg\language` after the same number. Our dialogs appear inside HLSW and belong
to its window, so somebody who has set HLSW to German should not be answered in
English because Windows happens to be English. If HLSW has never been asked,
the Windows language decides, and if there is no table for either, English does.

The texts live in the two executables' own string tables, which is the
mechanism Windows provides for exactly this: no extra file lands in your
folder, nothing has to be parsed, and the updater carries them along without
knowing they exist. They are in [src/strings.rc](src/strings.rc), one block per
language, and adding or correcting one is editing that file and building. A
language with a text missing falls back to English for that text alone, so a
half finished translation is never an empty dialog.

The two files carry different texts on purpose. The launcher holds the twenty
two it can show, the library the single one it can ever show, because
seventeen languages of an update dialog inside a library that has no dialogs
would be sixty eight kilobytes for nothing.

## Building it yourself

Nothing here is hidden from you. It is about 3600 lines of C in [src](src) and
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

## Links that go nowhere

Every link in HLSW's interface points at `hlsw.org` or `hlsw.net`, and not one
of them leads anywhere any more. Checked on 2026-08-19: `wiki.hlsw.org` serves
the bare Apache default page and answers 404 for every article, the homepage,
the registration form and the Sentinel lookup answer 403, and nothing under
`hlsw.net` answers at all. There are 180 wiki links in `cfg\Games.cfg` and
`cfg\AddOns.cfg` alone, one per game and per server addon, which is why the
status bar shows a dead address whenever the mouse passes over one.

Deleting them would be the easy answer and the wrong one, because most of what
they point at still exists. The wiki moved from `hlsw.org` to `hlsw.net` before
it went dark, and the Internet Archive kept the `.net` copy. Of those 180 pages,
**2 are archived under the .org name and 136 under the .net one**, which is the
whole difference between a dead link and a working one. So a wiki link is
rewritten to the `.net` name and handed to the archive, and the documentation
works again after fifteen years.

Two links get better than that. **Steam Community** in the right click menu on a
player went through `www.hlsw.org/steamprofile/<id>/`, which was HLSW's own
redirect to Steam and is now a 403. The account id is right there in the
address, so that click goes straight to `steamcommunity.com` instead, with the
old `STEAM_0:1:...` form converted to the 64 bit id a profile address wants.

One kind of link is deliberately left broken. An address that looks up one
particular server or player, the register link under a server and the Sentinel
entries, keeps the address it had and fails the way it already did. No crawler
in 2011 had reason to fetch the page for this server or that account, so the
archive would answer every one of them with its own "not archived" notice, and a
link that plainly fails is more honest than one that leads to a page explaining
it has nothing.

Everything else is handed to the archive under its own address and shows the
page as it was.

The status bar is rewritten as well, so what it shows before you click is where
the click will actually go. Nothing here guesses at what a link means; only the
address is read. `fix_dead_links = 0` leaves every link exactly as HLSW wrote
it, dead ends included.

Two entry points carry this: `ShellExecuteW` and `ShellExecuteExW`, the only two
HLSW imports from `SHELL32`. It uses no WinINet and no urlmon, so there is
nowhere else for a link to leave by.

## Updating itself

When HLSW starts, the launcher asks GitHub whether there is a newer hlswfix. If
there is, one dialog appears with three ways out: install it, open the release
page, or not now.

This is the one place where the fix does what it stops HLSW from doing, so it is
worth saying plainly what the difference is:

| | HLSW | this |
|---|---|---|
| how often | every five seconds, forever | once, when HLSW starts |
| where | `s9b.hlsw.org`, which answers nothing | `api.github.com`, where you got this |
| what it sends | ten bytes, undocumented | a request for one file, and the word `hlswfix` |
| can you stop it | no | `update_check = 0` |
| written up | in a licence nobody reads | here, and in `hlswfix.ini` |

It is on rather than off on purpose, and that is worth defending rather than
hiding. A check nobody switches on tells nobody anything, and there is nothing
else that would: the version in the title bar is read out of the file itself, so
it is always right about what is running and never about what exists.

The check runs on a thread of its own, so a slow network, a proxy that never
answers or a GitHub outage cannot keep HLSW from starting. Nothing is sent about
you or this machine: no version, no account, no identifier, nothing that would
tell two users apart.

Installing downloads the same archive a person would, checks it against the
SHA-256 that GitHub publishes for it, and takes `hlswfix.dll` and the launcher
out of it. Nothing on disk is touched until all of that has succeeded, and if a
step fails afterwards everything is put back as it was. HLSW can stay open while
it happens: a file that is in use can still be renamed out of the way on
Windows, only not deleted, so the replaced copies sit as `.old` until the
launcher clears them at its next start. The new version takes over the next time
HLSW starts.

It needs no unpacking tool for that, and that is deliberate. Shipping one would
mean a second unsigned executable in the archive of a program that already has
to explain itself to virus scanners, plus somebody else's parser to keep up
with. Calling one that Windows may have would mean the feature quietly stops
working on a machine nobody ever sees. Instead, `pack.ps1` writes those two
entries into the archive uncompressed, so reading them is finding them and
copying bytes; everything else in it is compressed as usual and the result is an
ordinary zip that Explorer and anything else opens. The archive is about a
hundred kilobytes larger for it, and the release page has one file on it rather
than three.

`hlswfix.ini` is never touched. It is the one file you edit, the installer has
always left it alone, and an updater that overwrote it would undo every setting
on the machine it was meant to help.

When it is done, it offers to start HLSW again. The new version only runs from
the next start, and in practice nobody restarts a program because a dialog
asked them to. It is an offer and not an action: the other button says plainly
that the new version starts with HLSW the next time. Choosing to restart asks
HLSW to close the polite way, so it can save what it has and ask a question of
its own if it wants to, and if that question is answered with no then HLSW
stays open and nothing further happens. The starting itself is done by this
launcher as the very last thing it does, when HLSW is gone, the tunnel is down
and every handle is closed.

Worth being straight about what that adds: downloading a file, putting it in
place and then running it is the shape virus scanners are built to notice. Two
things make this a weaker case than it sounds. The file being started is this
program's own name in its own folder, so from the outside it is `hlsw.exe`
starting `hlsw.exe`, which is a restart and not something fetched and set
loose. And it is the file that was just checked against the checksum published
with the release, after somebody pressed a button asking for exactly this.

**What the hash check does and does not prove.** It catches a download that
arrived damaged or was interfered with on the way. It cannot catch a release
published from an account that is no longer in the right hands, because the file
and its hash come from the same place. Nothing short of code signing would, and
that is discussed under *Antivirus and SmartScreen*.

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

Its own check for a newer HLSW is switched off too, and in its own setting
rather than at the socket: `AutoUpdateCheck` under
`HKCU\Software\HLSW\Settings`. Not placing a call is tidier than refusing one,
and there has been nothing at the other end for years. Turning it back on
inside HLSW lasts until the next start, because this runs at every start.

## What DllMain does, and what it stopped doing

A library gets its DllMain called with the loader lock held, and what may be
done in there is narrow: no `LoadLibrary`, and nothing that creates a window or
pumps messages, because a window can send a message to a thread that then wants
the loader, and both sides wait for each other.

This broke that rule twice. It put up a message box when the redirection
failed, which is exactly the window-creating case, and it took a Toolhelp
snapshot to write the module list into the log, which walks the very list the
lock is there to protect. Neither ever went wrong here, over weeks, which is
the reason to fix it rather than a reason not to: it works until it meets a
machine with something else injected into the same process, an anti-virus or a
game overlay, and then it looks like HLSW simply not starting, with nothing
anywhere to find.

Both now run on a thread created as the last thing DllMain does. That thread
cannot run any earlier, because starting a thread goes through the loader as
well and waits for the same lock, so by the time it runs the lock is gone. The
log shows it: everything up to the last detour carries one thread id, the
module list carries another.

What stays behind stays for a reason. The detours have to be in place before
HLSW runs its first instruction, and they are plain memory writes into an image
that is already mapped, which is allowed. The settings have to be read before
it is known which detours to place at all. The two registry values have to be
written before HLSW reads them, which is moments later. Reading `hlswfix.ini`
still uses the C runtime, which is linked statically and initialised before
DllMain is called, so it touches nothing the loader owns.

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
