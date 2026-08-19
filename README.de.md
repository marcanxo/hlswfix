[English](README.md) · **Deutsch**

# hlswfix

Bringt HLSW gegen heutige Gameserver zurück.

HLSW wurde zuletzt 2011 gebaut und zeigt heute jeden Server als Timeout. Das
hier behebt es, für jeden Server, den HLSW abfragen kann, und braucht keine
Konfiguration.

In HLSW selbst wird kein einziges Byte verändert. Der Fix ist eine kleine
Bibliothek, die beim Start in HLSW geladen wird und im Arbeitsspeicher
korrigiert, was HLSW auf das Netz schickt. Der Installer benennt `hlsw.exe` um
und legt Dateien daneben, und mehr fasst er nicht an.

## Installation

1. Zuerst HLSW installieren. Falls du es nicht hast, siehe
   [HLSW beschaffen](#hlsw-beschaffen) weiter unten.
2. `hlswfix-1.7.0.0.zip` von der [Releases-Seite][releases] laden. Nicht über den
   grünen **Code**-Knopf: der gibt dir die Quellen ohne die gebauten Dateien.
3. Irgendwohin entpacken und HLSW schließen, falls es läuft.
4. Doppelklick auf **`install.cmd`**.

Das war es. Starte HLSW wie immer, trage einen Server über seine Adresse ein,
und er sollte einen Ping zeigen statt eines Timeouts.

HLSW liegt üblicherweise unter `C:\Program Files (x86)`, deshalb fragt Windows
nach Administratorrechten. Fragt es nicht und das Skript meldet, es könne nicht
schreiben, dann Rechtsklick auf `install.cmd` und **Als Administrator
ausführen**. Es prüft, bevor es irgendetwas ändert, eine abgelehnte
Installation hinterlässt also nichts Halbfertiges.

**`uninstall.cmd`** stellt HLSW wieder her: aus `hlsw-real.exe` wird wieder
`hlsw.exe` und die `hlswfix`-Dateien werden gelöscht. Deine `hlswfix.ini`
bleibt liegen, falls du sie bearbeitet hast. Von Hand sind es dieselben drei
Schritte: die `hlswfix.*`-Dateien löschen, `hlsw.exe` löschen wenn es die
kleine ist, `hlsw-real.exe` zurückbenennen.

Drei Dinge, die man wissen sollte. Während HLSW läuft, siehst du jetzt zwei
Prozesse, `hlsw.exe` (der Launcher, der wartet) und `hlsw-real.exe` (HLSW
selbst). Die eingebauten Serverlisten bleiben leer, weil sie auf hlsw.net
zeigten; Server müssen von Hand eingetragen werden. Und die Login-Maske ist
abgeschaltet und HLSW meldet sich nicht mehr bei seinen alten Heimatservern,
beides lässt sich wieder einschalten, siehe [Einstellungen](#einstellungen).

Der Launcher übernimmt bei der Installation HLSWs Icon, damit deine
Verknüpfungen aussehen wie immer. Dieses Icon wird aus der HLSW-Kopie auf
deinem Rechner geholt und nicht mit ausgeliefert.

<details>
<summary>Ohne etwas umzubenennen</summary>

Wenn dir lieber ist, dass gar nichts umbenannt wird, entpacke `hlswfix.exe`,
`hlswfix.dll` und `hlswfix.ini` selbst in den HLSW-Ordner und starte
`hlswfix.exe` statt `hlsw.exe`. Derselbe Fix, keine Administratorrechte, und
rückgängig machen heißt diese drei Dateien löschen, dazu `hlswfix.log` falls
eines geschrieben wurde. Der einzige Unterschied: deine vorhandenen
Verknüpfungen starten weiterhin das ungefixte HLSW.

Das ist auch der Ausweg, wenn dein Rechner PowerShell-Skripte verbietet.
</details>

<details>
<summary>Nach einer Neuinstallation oder einem Update von HLSW</summary>

`install.cmd` einfach noch einmal ausführen. Ein frisches HLSW schreibt seine
eigene `hlsw.exe` und hebt die Anordnung damit auf, und die Timeouts kommen
zurück. Das Skript schaut nach, was `hlsw.exe` tatsächlich ist, statt es
anzunehmen, zweimal ausführen schadet also nicht.
</details>

## Wenn es nicht funktioniert

**HLSW startet gar nicht mehr.** Höchstwahrscheinlich hat dein Virenscanner
eine Datei in Quarantäne verschoben. Nach der Installation ist `hlsw.exe` der
Launcher, wird der weggenommen, bleibt nichts mehr zum Anklicken. Verloren ist
nichts: `hlsw-real.exe` ist dein unangetastetes HLSW. Führe `uninstall.cmd`
aus, oder stelle die Datei wieder her und nimm den HLSW-Ordner in deinem
Scanner aus, dann noch einmal installieren.

**"hlswfix.dll could not be loaded into HLSW".** Fast immer dieselbe Ursache.
Schau zuerst in den Verlauf deines Virenscanners. HLSW startet trotzdem, es
zeigt eben Timeouts.

**Server zeigen weiterhin Timeout.** Setze `log = 1` in `hlswfix.ini`, starte
HLSW und öffne `hlswfix.log` neben `hlsw.exe`. Eine Zeile, die mit `attached:`
beginnt, heißt: der Fix ist geladen und das Problem liegt woanders. Gar keine
Logdatei heißt, HLSW wurde ohne den Launcher gestartet, prüfe also, ob
`hlsw.exe` wirklich die kleine ist.

**Das Skript findet HLSW nicht.** Es liest den Ordner aus HLSWs eigenem
Deinstallationseintrag, den eine kopierte oder verschobene Installation nicht
hat. Sag ihm, wo es liegt: `install.cmd -Dir "D:\Games\HLSW"`.

Alles unterhalb dieser Zeile ist Hintergrund. Zum Benutzen brauchst du nichts
davon.

## HLSW beschaffen

Hast du es schon? Dann überspring das hier. Jede 1.4er-Installation
funktioniert, und die Prüfsummen unten sind nur wichtig, wenn du dir eine Kopie
besorgst.

Die offizielle Seite ist faktisch weg. hlsw.net und hlsw.org lösen zwar noch
auf, beide auf dieselbe Adresse, aber hlsw.net antwortet überhaupt nicht und
hlsw.org liefert 403. Einen offiziellen Download gibt es seit Jahren nicht
mehr, und was eine Suchmaschine stattdessen anbietet, sind überwiegend Portale,
die den Installer in ihren eigenen einpacken.

Deshalb liegt die letzte Version unverändert auf der
[Releases-Seite][releases] dieses Repositories. Das ist erlaubt, `license.txt`
in HLSW sagt es wörtlich:

> This program is Copyright (c) 2000-2009 by Stripf Software. It is FREEWARE
> which may be freely copied and distributed onto and through any and all
> computer media.

Prüfe, was du herunterlädst, von hier oder von woanders:

| Datei | SHA-256 | Größe |
|---|---|---|
| `hlsw_1_4_0_5_setup.exe` | `525b20c89b585704067bb38ae65ff817c2753bd20d680361c840ddce69dc15a7` | 12.034.292 |
| `hlsw.exe` (daraus installiert) | `ab1ca2abdc25879c7759da8549e386ae7d40fbd4258e12b57a9c820201bf178e` | 20.813.824 |

    certutil -hashfile hlsw_1_4_0_5_setup.exe SHA256

Zwei Dinge, bei denen man ehrlich sein muss. Der Installer ist nicht signiert,
und das Internet Archive hat von diesen Dateien immer nur die Weiterleitung
erwischt, nie die Datei selbst. Es gibt also weder eine Signatur noch ein
archiviertes Original, gegen das man eine Kopie prüfen könnte. Was diese
Prüfsummen leisten: alle bekommen dieselben Bytes, und es sind die Bytes, gegen
die dieser Fix entwickelt und getestet wurde. Geprüft wurde hier: die
Versionsressourcen, der Inhalt von `license.txt`, das Verhalten des Installers,
ein Virenscan, und dass das Ergebnis läuft.

Der Installer ist außerdem verwirrend, was seine eigene Version angeht, und war
es immer. Verteilt wird er als 1.4.0.5, die Info-Box sagt 1.4.0.5, der Eintrag,
den er in Windows hinterlässt, sagt 1.4.0.5, und die Versionsressource in
`hlsw.exe` sagt 1.4.0.4. So haben die Entwickler es ausgeliefert.

## Was kaputt war

Im Dezember 2020 hat Valve `A2S_INFO` eine Challenge vorgeschrieben, damit
Gameserver nicht länger als UDP-Reflection-Verstärker taugen. Ein Client
schickt die Abfrage, der Server antwortet mit einem 9 Byte langen
`S2C_CHALLENGE`-Paket (`0x41`), und erst eine Wiederholung der Abfrage mit
diesen vier Challenge-Bytes bekommt die echte `0x49`-Antwort.

HLSW weiß von diesem Schritt nichts. Es schickt die schlichte Abfrage, kann mit
dem, was zurückkommt, nichts anfangen, und zeigt jeden Server als Timeout ohne
Ping. Dieser eine fehlende Rundlauf ist das ganze Problem. `A2S_PLAYER` und
`A2S_RULES` brauchten schon immer eine Challenge und HLSW führt diesen
Handschlag korrekt aus, und rcon läuft über TCP und hat sich überhaupt nicht
geändert.

Auf dem Server lässt es sich nicht beheben. `sv_enableoldqueries` sieht aus wie
der passende Schalter und ist es nicht: gegen einen laufenden Server gemessen
antwortet `A2S_INFO` auch mit dieser Cvar auf 1 weiterhin mit `0x41`, weil
Abfragen von der Steam-Gameserver-API in `steamclient.so` beantwortet werden,
wo keine Engine-Cvar hinreicht.

## Was der Fix tut

Zwölf Funktionen werden innerhalb des HLSW-Prozesses umgeleitet, keine
weiteren:

| umgeleitet | wozu |
|---|---|
| `sendto` | Merken, dass ein `A2S_INFO` rausging, es drosseln, und eine bekannte Challenge gleich anhängen, aber **nur bei Servern, die gezeigt haben, dass sie eine verlangen**, damit wiederholte Abfragen einen statt zwei Rundläufe kosten. |
| `send` | Dasselbe, nur wird hier nie etwas zurückgehalten. Auf einer verbundenen Socket würde eine verworfene Abfrage die Antwort stranden lassen. |
| `recvfrom`, `recv`, `WSARecvFrom`, `WSARecv` | Das `0x41` abfangen, das auf so eine Abfrage antwortet, die Wiederholung nebenbei auf die Leitung legen, und HLSW genau das übergeben, was ankam. HLSW merkt nie, dass etwas passiert ist. Hier wird auch eine doppelte Antwort unsichtbar gemacht, sofern `hide_duplicate_info` an ist. |
| `connect` | Nur für die optionale rcon-Umleitung weiter unten. Ausschließlich Stream-Sockets, und ohne konfiguriertes `rcon_redirect` tut es gar nichts. |
| `select` | Nichts als ein Kontrollpunkt: feuert er nie, erreicht auch nichts die Hooks. Immer installiert, ändert kein Verhalten. |
| `gethostbyname`, `getaddrinfo`, `WSAAsyncGetHostByName` | Verweigern die Auflösung von hlsw.net und hlsw.org, damit HLSW sich nicht bei Servern meldet, die ihm nicht mehr gehören. Siehe unten. Werden bei `block_home_calls = 0` gar nicht erst installiert. |
| `SetWindowTextW` | Nur für die Version in der Titelzeile. |

Alle vier Empfangsfunktionen werden umgeleitet, weil HLSW alle vier benutzt:
die einfachen für den Schwung Testabfragen beim Hinzufügen eines Servers, die
`WSA`-Varianten, sobald es in die laufende Überwachung übergeht. Nur `recvfrom`
umzuleiten funktionierte etwa eine Minute, bis zum ersten Challenge-Wechsel.

Was der Fix auf das Netz schickt: wiederholte `A2S_INFO`-Abfragen, an denselben
Server, den HLSW ohnehin schon gefragt hat, und sonst nichts. Keine
Update-Prüfung, keine Telemetrie, keine Verbindung zu irgendetwas von uns. Die
einzige eigene Verbindung des Launchers geht an `127.0.0.1`, um zu sehen, ob
ein rcon-Tunnel hochgekommen ist, und auch nur wenn du einen willst.

Challenges, die HLSW selbst angefordert hat, werden unangetastet
durchgereicht, denn eine davon zu verlieren würde HLSW die Spielerliste kosten,
während eine vom Fix verlorene Challenge nur einen zusätzlichen Rundlauf
kostet. So ein Anspruch verfällt nach drei Sekunden, damit eine Antwort, die
nie kommt, die Info-Abfrage nicht dauerhaft blockiert.

Angehängt wird eine Challenge an `A2S_INFO` nur bei Servern, die **bewiesen**
haben, dass sie eine wollen, und der einzige anerkannte Beweis ist, dass dieser
Server eine blanke Abfrage von uns mit `0x41` beantwortet hat. Warum das so eng
gefasst ist, steht weiter unten unter "Was getestet wurde".

## Drosselung der Abfragen

HLSW fragt den Server, den es beobachtet, in dem Moment erneut, in dem die
vorige Antwort ankommt. Gemessen: bis zu achtzig Abfragen pro Sekunde für jede
der drei Arten Info, Spieler und Rules, wobei allein die Rules-Antwort als
zweihundertfünfzig Datagramme pro Sekunde zurückkam. Ein Source-Server
beantwortet `sv_max_queries_sec` Abfragen pro Adresse, standardmäßig drei pro
Sekunde, und verwirft alles darüber stillschweigend. HLSWs eigene
Aktualisierungsrate ist also das, was ausgerechnet den beobachteten Server als
Timeout erscheinen lässt. `ServerAutoUpdateRate` in der Registry steuert diese
Schleife nicht.

`query_interval_ms`, standardmäßig eine Sekunde, hält jede Abfrageart auf eine
pro Intervall pro Server-Socket, was zusammen genau die drei pro Sekunde
ergibt, die ein Server beantwortet, ohne etwas zu verwerfen. An einem Server
gemessen sank der Verkehr damit von 222 Paketen pro Sekunde auf unter 3.

Die Grenze erlaubt einen Anlauf: sechs Abfragen einer Art dürfen nach einer
ruhigen Phase direkt hintereinander raus, erst danach greift die Rate. Beim
Anwählen feuert HLSW alles, was es kennt, auf einmal ab, und auf jede
zurückkommende Challenge folgt sofort die nächste Abfrage, das sind sechs
Versuche in einer Viertelsekunde. Eine Grenze ohne Anlauf machte genau daraus
Ablehnungen, und ausgerechnet der gerade angeklickte Server zeigte dann Timeout.

**Was HLSW über eine zurückgehaltene Abfrage erfährt, entscheidet, was die
Ping-Spalte bedeutet**, und das ist das einzige hier, das man wissen sollte.

Standardmäßig wird die Abfrage **verzögert**: sie gilt als gesendet und geht
wirklich raus, sobald ihr Fenster aufgeht, von einem eigenen Thread des Fixes.
Es wird nie etwas abgelehnt und nichts sieht je falsch aus, aber HLSW startet
seine Stoppuhr beim Übergeben, und in der Ping-Spalte steht deshalb ungefähr ein
Intervall statt des echten Rundlaufs. Alles, was gar nicht gedrosselt wird, also
jede Abfrage außerhalb von Source und GoldSrc, zeigt weiterhin seinen echten
Ping.

Mit `refuse_held_queries = 1` wird das Senden stattdessen **abgelehnt**, mit dem
Fehler, der bedeutet "nichts ging raus, versuch es gleich nochmal". HLSW weiß
dann, dass keine Antwort unterwegs ist, wartet nie auf eine, und misst nur
Abfragen, die wirklich rausgingen: in der Ping-Spalte steht der echte Rundlauf,
gemessen 11 bis 30 ms über sieben Server, und HLSW nimmt die Ablehnung gelassen
und fragt nach ein bis drei Sekunden erneut.

Das war eine Weile die Vorgabe und wurde zurückgenommen. HLSW schreibt jede
abgelehnte Sendung in seine Statuszeile, als `ERROR in CHLSWSocket::SendTo:
(10035) A non-blocking socket operation could not be completed immediately`, und
färbt damit die Fußzeile rot, bei einem Programm, das einwandfrei arbeitet. Eine
falsche Zahl in einer Spalte ist der kleinere Preis. Vorher wissen konnte das
niemand: HLSWs Quelltext ist verloren, und der einzige Weg herauszufinden, wie es
auf eine abgelehnte Sendung reagiert, war eine abzulehnen und zuzusehen.

Das beim ersten Mal falsch zu treffen ist genau das, was Server als Timeout
erscheinen ließ, und der Grund gehört aufgeschrieben. HLSW fragt erst dann
erneut, wenn eine Antwort angekommen ist, und solange es eine Abfrage für
unterwegs hält, sendet es rund zwei Sekunden lang gar nichts. Eine still
**verworfene** Abfrage kostet also nicht eine Aktualisierung, sondern diese ganze
Frist. Vor der Korrektur gemessen: jeder Server, den man gerade nicht ansieht,
lief mit einer echten Abfrage alle 2,04 Sekunden, davon 2,03 Sekunden mit einer
offenen Antwort, auf die HLSW wartete. Auf einem Prüfstand, der so fragt wie
HLSW, beantwortete das Verwerfen 4 von 8 Abfragen und wechselte dabei zwischen
einem Timeout nach 2,5 Sekunden und einer Antwort, die sofort da ist, weil sie
die alte aus dem vorigen Fenster ist.

Zwei weitere Ansätze wurden probiert und sind schlechter:

*Den Aufruf selbst verzögern* geht nicht. HLSW erledigt seine Socket-Arbeit auf
einem Thread mit blockierendem `recvfrom`, jedes Warten in einem Hook hält ihn
also an. Das ist auch der Grund, warum die verzögerten Abfragen von einem
eigenen Thread kommen: HLSW ruft `select` überhaupt nie auf, es gibt also keinen
eigenen Aufruf, an den man die Arbeit in dem einen Moment hängen könnte, auf den
es ankommt, nämlich während HLSW im `recvfrom` wartet.

*Die Abfrage lokal beantworten*, aus der vorigen Antwort, lügt beim Ping in die
andere Richtung und meldet 1 bis 3 ms, weil eine lokal erzeugte Antwort genau so
lange braucht. Außerdem dreht HLSW dann hoch: es fragt sofort wieder, wenn es
eine Antwort hat, eine sofortige Antwort ist also eine Einladung zur Schleife.

**Bekannt und bewusst so gelassen:** beim Durchklicken der Serverliste kann bei
dem gerade angewählten Server kurz ein Timeout aufblitzen. Der Anlauf deckt den
üblichen Eröffnungsverkehr ab, aber nicht jede Form davon. Mit dem Server ist
nichts, und nach ein, zwei Sekunden ist es von selbst weg.

## Einstellungen

Alles in `hlswfix.ini` ist optional, die Datei selbst eingeschlossen. Der Fix
braucht keine Konfiguration. Die Kommentare in der Datei erklären jede
Einstellung, drei sind es wert, hier wiederholt zu werden.

**`title_version`** ist der Grund, warum in der Titelzeile **HLSW v1.7.0.0**
steht. Die letzte Version der Entwickler war 1.4.0.5 aus dem Jahr 2011, und die
neue Nummer sagt auf einen Blick, dass in diesem HLSW der Fix steckt. Setzen
musst du sie nicht: ohne die Zeile wird die Version des Fixes selbst angezeigt,
gelesen aus seiner eigenen Datei, und stimmt damit nach jedem Update von allein.
Das ist wichtig, weil der Installer `hlswfix.ini` absichtlich nie überschreibt,
eine dort eingetragene Nummer also mit dem nächsten Update veraltet. Geändert
wird nur die angezeigte Zeichenkette: HLSW baut den Titel aus seiner eigenen
Versionsressource, und die bleibt unberührt. `title_version =` ohne etwas
dahinter stellt HLSWs eigene Version wieder her.

**`rcon_redirect`** schickt die rcon-Verbindung für einen Server auf einen
lokalen Port statt direkt zu ihm, für den Fall, dass der rcon-Port hinter einer
Firewall liegt und du ihn über einen SSH-Tunnel erreichst. Für weitere Server
die Zeile wiederholen, bis zu acht. Abfragen werden nie umgeleitet, HLSW zeigt
und fragt also weiterhin die echte Adresse. Lohnt sich auch dort, wo der Port
offen ist, denn Source-rcon schickt sein Passwort im Klartext. Benutze es
zusammen mit `tunnel_command`, das wartet, bis der Port tatsächlich antwortet:
allein, mit nichts dahinter, scheitert HLSW einfach beim Verbinden, aber wenn
dort etwas *anderes* lauscht, geht dein rcon-Passwort dorthin.

**`tunnel_command`** wird bei jedem Start von HLSW als Programm gestartet, mit
deinen Rechten und ohne Fenster, und mit HLSW wieder beendet. Es muss nicht ssh
sein. Behandle `hlswfix.ini` deshalb wie einen Autostart-Eintrag: wer sie
schreiben kann, kann als du alles ausführen.

**`block_home_calls`**, **`skip_login_screen`**, **`hide_duplicate_info`**,
**`fix_dead_links`** und **`update_check`** sind alle standardmäßig an und haben
weiter unten je einen eigenen Abschnitt.

**`log`** schreibt `hlswfix.log` neben `hlsw.exe`. Klartext, wird angehängt,
nie rotiert. Bei `1` stehen die beteiligten Server drin, bei `2` die Pakete
selbst, und ein Tunnel-Fehler wird sogar bei `0` geschrieben, samt deiner
`tunnel_command`-Zeile. Ist der Ordner nicht beschreibbar, wird gar nichts
geschrieben. `uninstall.cmd` löscht die Datei.

## Virenscanner und SmartScreen

Stell dich auf Widerstand ein, und wisse, worauf du dich einlässt.

Der Launcher startet HLSW angehalten, schreibt einen Pfad hinein und ruft dort
drüben über `CreateRemoteThread` die Funktion `LoadLibraryA` auf. Die Bibliothek
belegt anschließend ausführbaren Speicher und schreibt einen Sprung über die
ersten fünf Bytes jeder Winsock-Funktion, die sie umleitet. Das ist die
Lehrbuchform der DLL-Injektion, genau darauf sind Scanner trainiert, und ohne
das ist diese Aufgabe nicht zu lösen. Alles davon passiert innerhalb des
HLSW-Prozesses: kein anderer Prozess wird angefasst, und auf der Festplatte
wird nichts verändert.

Signiert sind die Dateien auch nicht, SmartScreen nennt den Herausgeber also
unbekannt. Was du in der Praxis siehst: Windows markiert alles, was aus einem
heruntergeladenen Archiv entpackt wurde, beim ersten Start von `install.cmd`
kann also der blaue Kasten "Der Computer wurde durch Windows geschützt"
erscheinen. **Weitere Informationen**, dann **Trotzdem ausführen**. Ein
Rechtsklick auf die ZIP-Datei, **Eigenschaften**, **Zulassen** vor dem
Entpacken erspart dir das für alle Dateien auf einmal.

Windows Defender wurde mit aktuellen Signaturen auf das Release-Archiv und auf
jede Datei darin angesetzt und hat nichts gefunden. Ein anderer Scanner kann
trotzdem Einspruch erheben, nach Aussehen statt nach Verhalten.

Der Updater bringt eine zweite Sache mit, die Scanner nicht mögen: eine Datei
herunterladen und an die Stelle einer vorhandenen setzen. Für sich genommen ist
das unauffällig, im selben Programm neben einer DLL-Injektion ähnelt es der Form
eines Downloaders mehr, als jede Hälfte für sich es täte. Und der naheliegende
Vergleich trägt nicht: andere Programme bieten genau diesen Dialog an und werden
dafür nicht angemeckert, aber das sind gewöhnliche Programme, die einen
heruntergeladenen Installer an Windows übergeben. Dieses hier injiziert. Nichts
davon wird verschwiegen, der Code steht in
[src/launcher.c](src/launcher.c) unter `check_update`, und `update_check = 0`
schaltet die ganze Sache ab.

Eine Code-Signatur würde das meiste davon erledigen und ist realistisch nicht zu
haben: ein gewöhnliches Zertifikat kostet ein paar hundert im Jahr und startet
trotzdem ohne Reputation, die Sorte, die SmartScreen sofort zufriedenstellt,
will eine Firma dahinter sehen, und der günstige moderne Weg will eine
nachweisbare Geschäftshistorie, die ein Hobbyprojekt nicht hat. Die ehrliche
Position ist also: das hier kann angemeckert werden, die Quellen liegen zum
Nachlesen daneben, und ein Fehlalarm lässt sich bei Microsoft melden und ist
meist nach ein bis zwei Tagen erledigt.

Die Releases-Seite führt die SHA-256-Summen des Archivs und jeder Datei darin
auf, du kannst also prüfen, ob das Heruntergeladene das Veröffentlichte ist.
Beachte, dass ein Bau aus den Quellen diese Bytes nicht exakt reproduziert,
weil der Compiler jeden Bau stempelt: selbst bauen ist ein Ersatz für die
Prüfsummen, keine Gegenprobe.

## Selbst bauen

Hier ist nichts vor dir verborgen. Es sind ungefähr 3600 Zeilen C in
[src](src), und der Bau dauert ein paar Sekunden:

    build.cmd

`install.cmd` hat vierzehn Zeilen, von denen eine überhaupt etwas ausführt: sie
startet `install.ps1`, eine Textdatei, die du vorher lesen kannst, mit
`-NoProfile -ExecutionPolicy Bypass`. Dieser Schalter gilt für genau diesen
einen Aufruf und ändert keine Einstellung auf deinem Rechner. Installiert wird
nirgendwo etwas außer im HLSW-Ordner selbst: keine Registry-Einträge, kein
Startmenü-Eintrag, kein Deinstallationseintrag.

Der Bau braucht einen 32-Bit-Compiler, denn `hlsw.exe` ist PE32: eine
64-Bit-Bibliothek lässt sich nicht hineinladen, und ein 64-Bit-Launcher würde
`LoadLibraryA` an einer Adresse suchen, die es dort drüben nicht gibt. Eine
portable MinGW-w64-i686-Toolchain entpackt man irgendwohin und muss nichts
installieren. Zeige mit `MINGW` auf deren `bin`-Ordner oder leg `gcc` in den
PATH. Das Ergebnis landet in `build\`, und `pack.ps1` baut daraus das
Release-Archiv.

## Links, die ins Leere führen

Jeder Link in HLSWs Oberfläche zeigt auf `hlsw.org` oder `hlsw.net`, und keiner
davon führt noch irgendwohin. Geprüft am 19.08.2026: `wiki.hlsw.org` liefert nur
noch die nackte Apache-Standardseite und antwortet auf jeden Artikel mit 404,
Homepage, Registrierung und die Sentinel-Abfrage antworten mit 403, und unter
`hlsw.net` antwortet gar nichts mehr. Allein in `cfg\Games.cfg` und
`cfg\AddOns.cfg` stehen 180 Wiki-Links, einer pro Spiel und pro Server-Addon.
Deshalb zeigt die Fußzeile eine tote Adresse, sobald die Maus über einen davon
fährt.

Sie zu löschen wäre die einfache Antwort und die falsche, denn das meiste, worauf
sie zeigen, existiert noch. Das Wiki ist vor seinem Ende von `hlsw.org` nach
`hlsw.net` umgezogen, und das Internet Archive hat die `.net`-Fassung bewahrt.
Von diesen 180 Seiten sind **2 unter dem .org-Namen archiviert und 136 unter dem
.net-Namen**, und genau das ist der Unterschied zwischen totem und
funktionierendem Link. Ein Wiki-Link wird deshalb auf den `.net`-Namen
umgeschrieben und ans Archiv übergeben, und die Dokumentation funktioniert nach
fünfzehn Jahren wieder.

Zwei Links bekommen mehr als das. **Steam Community** im Rechtsklick-Menü auf
einen Spieler lief über `www.hlsw.org/steamprofile/<id>/`, HLSWs eigene
Weiterleitung zu Steam, heute ein 403. Die Konto-Kennung steht in der Adresse
selbst, also geht dieser Klick jetzt direkt zu `steamcommunity.com`, wobei die
alte Form `STEAM_0:1:...` in die 64-Bit-Kennung umgerechnet wird, die eine
Profiladresse erwartet.

Eine Sorte Link bleibt absichtlich kaputt. Eine Adresse, die einen *bestimmten*
Server oder Spieler nachschlägt, also der Register-Link unter einem Server und
die Sentinel-Einträge, behält die Adresse, die sie hatte, und scheitert so, wie
sie es ohnehin schon tat. Kein Crawler hatte 2011 einen Grund, die Seite für
genau diesen Server oder jenes Konto zu holen, das Archiv würde also jede davon
mit seinem eigenen "nicht archiviert" beantworten. Ein Link, der ehrlich
scheitert, ist mehr wert als einer, der auf eine Seite führt, die erklärt, dass
sie nichts hat.

Alles Übrige geht unter seiner eigenen Adresse ans Archiv und zeigt die Seite
von damals.

Die Fußzeile wird ebenfalls umgeschrieben, damit vor dem Klick dort steht, wohin
der Klick wirklich geht. Nichts davon rät, was ein Link bedeutet, gelesen wird
nur die Adresse. `fix_dead_links = 0` lässt jeden Link genau so, wie HLSW ihn
geschrieben hat, Sackgassen eingeschlossen.

Getragen wird das von zwei Einsprungpunkten: `ShellExecuteW` und
`ShellExecuteExW`, den einzigen beiden, die HLSW aus `SHELL32` importiert. Es
benutzt kein WinINet und kein urlmon, es gibt also keinen anderen Weg, auf dem
ein Link hinausgehen könnte.

## Sich selbst aktualisieren

Beim Start von HLSW fragt der Launcher bei GitHub nach, ob es ein neueres
hlswfix gibt. Wenn ja, erscheint ein Dialog mit drei Wegen hinaus: installieren,
Release-Seite öffnen, oder später.

Das ist die eine Stelle, an der der Fix tut, was er HLSW verbietet, also gehört
klar gesagt, worin der Unterschied besteht:

| | HLSW | hier |
|---|---|---|
| wie oft | alle fünf Sekunden, endlos | einmal, beim Start von HLSW |
| wohin | `s9b.hlsw.org`, das nichts beantwortet | `api.github.com`, wo das hier herkommt |
| was rausgeht | zehn Bytes, undokumentiert | die Anfrage nach einer Datei, und das Wort `hlswfix` |
| abschaltbar | nein | `update_check = 0` |
| dokumentiert | in einer Lizenz, die niemand liest | hier, und in `hlswfix.ini` |

Es ist bewusst an statt aus, und das gehört verteidigt statt versteckt. Eine
Prüfung, die niemand einschaltet, sagt niemandem etwas, und es gäbe auch sonst
nichts, das es täte: die Version in der Titelzeile wird aus der Datei selbst
gelesen und hat deshalb immer recht darüber, was läuft, und nie darüber, was es
gibt.

Die Prüfung läuft auf einem eigenen Thread, damit ein langsames Netz, ein
Proxy, der nie antwortet, oder eine GitHub-Störung den Start von HLSW nicht
aufhalten kann. Über dich oder diesen Rechner geht nichts hinaus: keine Version,
kein Konto, keine Kennung, nichts, woran sich zwei Nutzer unterscheiden ließen.

Installieren ersetzt zwei Dateien, `hlswfix.dll` und den Launcher. Beide werden
gegen die SHA-256 geprüft, die GitHub zum Release veröffentlicht, bevor auf der
Platte irgendetwas angefasst wird, und schlägt einer der beiden Schritte fehl,
wird alles zurückgedreht. HLSW darf dabei offen bleiben: eine Datei, die in
Benutzung ist, lässt sich unter Windows zwar nicht löschen, aber sehr wohl
umbenennen, also liegen die ersetzten Fassungen als `.old` daneben, bis der
Launcher sie beim nächsten Start wegräumt. Die neue Fassung übernimmt beim
nächsten Start von HLSW.

`hlswfix.ini` wird nie angefasst. Es ist die eine Datei, die du bearbeitest, der
Installer hat sie immer in Ruhe gelassen, und ein Updater, der sie überschreibt,
macht genau die Einstellungen kaputt, denen er helfen soll.

**Was die Prüfsumme beweist und was nicht.** Sie fängt einen Download ab, der
beschädigt ankam oder unterwegs verändert wurde. Sie fängt kein Release ab, das
aus einem Konto veröffentlicht wurde, das nicht mehr in den richtigen Händen
ist, denn Datei und Prüfsumme kommen von derselben Stelle. Das könnte nur eine
Code-Signatur, und dazu steht etwas unter *Virenscanner und SmartScreen*.

## HLSW meldet sich bei Servern, die ihm nicht mehr gehören

Aus HLSWs eigener `license.txt`:

> HLSW transfers data to central servers owned by Stripf Software to enable
> several functionalities in HLSW like version check, console log, location
> detection, login, buddy list, etc.

Gemessen statt geglaubt: HLSW schickt alle fünf Sekunden ein zehn Byte langes
Paket an `s9b.hlsw.org`, also `62.75.203.63`, solange es läuft, und es kommt nie
etwas zurück. Das tut es von sich aus, ohne dass du irgendeine Funktion
benutzt.

hlsw.net und hlsw.org sind weiterhin registriert und zeigen weiterhin auf einen
Host. Wer sie als Nächstes übernimmt, erbt diesen Verkehr, und ein Programm von
2011 meldet sich unbeirrt weiter, ohne je erneut zu fragen. Nichts, was HLSW
für dich tut, braucht davon irgendetwas.

Deshalb ist `block_home_calls` standardmäßig an. Es verweigert die
Namensauflösung, und es weist die Adresse zusätzlich an der Socket ab, denn
HLSW speichert die zuletzt aufgelöste unter
`HKCU\Software\HLSW\Master Server` und müsste den Namen sonst nie wieder
nachschlagen. Danach über einen Lauf gemessen: acht Pakete zurückgehalten,
keines gesendet, und die Spielserver antworteten genau wie vorher. Setze es in
`hlswfix.ini` auf `0`, um HLSWs eigenen Verkehr in Ruhe zu lassen.

## Die Login-Maske

Eine frische Installation öffnet mit einer Login-Maske für ein Konto auf
Servern, die seit Jahren nicht mehr antworten. `skip_login_screen`,
standardmäßig an, schaltet sie ab. Es ist HLSWs eigene Einstellung,
`LoginOnStartup` und `AutoLogin` unter `HKCU\Software\HLSW\Management`, es wird
also nichts abgefangen und HLSWs eigener Einstellungsdialog zeigt weiterhin die
Wahrheit. Innerhalb von HLSW wieder eingeschaltet hält es bis zum nächsten
Start, weil dies bei jedem Start angewandt wird; setze `skip_login_screen = 0`,
wenn du die Maske dauerhaft zurückwillst.

## Server, die dieselbe Abfrage zweimal beantworten

Ein paar Server beantworten ein einzelnes `A2S_INFO` zweimal: einmal in dem
Format, das GoldSrc benutzte, bevor es das Source-Abfrageprotokoll gab, und
einmal im modernen. Bei einem davon gemessen, bei jeder Abfrage ohne Ausnahme:
die alte Antwort nach 14 ms, die moderne nach 15.

Die beiden widersprechen sich in genau den Feldern, aus denen HLSW Spiel und
Version liest. Die alte Antwort führt keine App-ID mit sich und nennt sich
Protokoll 47, die moderne sagt 48. HLSW versteht beide und zeigt die zuletzt
eingetroffene, deshalb springen Spielsymbol und Versionsangabe hin und her,
solange so ein Server ausgewählt ist. Das macht der Server, nicht HLSW: ein
blanker Socket, der von beiden nichts weiß, sieht dieselben zwei Antworten.

`hide_duplicate_info`, standardmäßig an, macht die überflüssige alte Kopie für
HLSW unsichtbar. An der Zustellung ändert sich nichts: das Paket wird weiterhin
übergeben, gleiche Länge, gleicher Absender, gleicher Moment, und nur sein
Typ-Byte wird zu einem, das es im Abfrageprotokoll nicht gibt. HLSW erkennt also
weiterhin ein Paket dieses Protokolls und findet dann nichts damit anzufangen.
Es zu schlucken und stattdessen das nächste zurückzugeben hieße, auf ein Paket
zu warten, das vielleicht nie kommt, und Warten in einem Empfangs-Hook hat genau
das hier schon einmal zerlegt.

Es greift nur mit Beweis in der Hand: derselbe Server muss auf demselben Socket
innerhalb der letzten zehn Sekunden im modernen Format geantwortet haben. Ein
Server, der nur das alte Format spricht, wird deshalb nie angefasst, und einer,
der die moderne Antwort einstellt, ist zehn Sekunden später wieder vollständig
sichtbar. Der Preis, wenn ausgerechnet die moderne Antwort unterwegs verloren
geht, ist eine ausgefallene Aktualisierung, und die kostet ein verlorenes Paket
ohnehin. Mit `hide_duplicate_info = 0` siehst du alles, was ein Server schickt,
genau so, wie er es schickt.

## Wie die Funktionen umgeleitet werden, und warum nicht über die Importtabellen

Der naheliegende Weg wäre, die Importtabellen zu patchen, und der funktioniert
hier nicht. HLSW löst die Winsock-Funktionen zur Laufzeit auf und ruft sie über
eigene Zeiger auf, seine Importtabelle wird also nie herangezogen. Das ist
leicht falsch zu deuten, denn ein Hook in einer Importtabelle, die niemand
liest, verhält sich exakt wie ein Hook, dessen Einbau fehlgeschlagen ist. Die
Entscheidung brachte, die gepatchten Einträge aus dem laufenden Prozess wieder
auszulesen: sie enthielten die Hook-Adressen, `select` eingeschlossen, und kein
einziger wurde je aufgerufen, während sichtbar Pakete auf die Leitung gingen.

Also werden die Funktionen selbst umgeleitet. Jede beginnt mit dem
Microsoft-Hotpatch-Prolog `mov edi,edi; push ebp; mov ebp,esp`, und das sind
genau die fünf Bytes, die ein `jmp rel32` braucht. Der Sprung lässt sich also
darüberschreiben, ohne je eine Instruktion in der Mitte zu zerschneiden. Die
verdrängten Bytes plus ein Sprung zurück bilden ein Trampolin, das der Hook
aufruft, um die eigentliche Arbeit erledigen zu lassen. Das erwischt jeden
Aufrufer, egal auf welchem Weg er zur Adresse kam.

Das Patchen der Importtabellen bleibt nur als Rückfalloption erhalten, für eine
Funktion ohne diesen Prolog. Derzeit braucht es keine.

## Drei weitere Fallen

**HLSW braucht das richtige Arbeitsverzeichnis.** Es ist eine MFC-Anwendung und
sucht `cfg\Games.cfg` und den Rest seiner Daten relativ zum aktuellen
Verzeichnis, nicht relativ zu seinem eigenen Ort. Von woanders gestartet meldet
es seine Konfiguration als fehlend, was nach einer kaputten Installation
aussieht und keine ist. Der Launcher setzt es.

**Die Winsock-Funktionen umleiten, nicht ihre Aufrufer, und niemals über
Kreuz.** `WSOCK32.DLL` hat eigene `recv` und `recvfrom` und reicht `send`,
`sendto` und `connect` direkt an `WS2_32.DLL` weiter. Ein Hook muss deshalb
sein Trampolin aufrufen, nie den ursprünglichen Einsprungpunkt, sonst landet er
bei seinem eigenen Ersatz und rekursiert, bis der Stack alle ist.

**WOW64 lenkt einen 32-Bit-Prozess von System32 weg.** Der Windows-SSH-Client
liegt in `C:\Windows\System32\OpenSSH` und existiert nur als 64 Bit, ein
32-Bit-Prozess, der danach fragt, landet also in `SysWOW64\OpenSSH`, das es
nicht gibt. `ssh` zu starten scheiterte mit "Datei nicht gefunden", obwohl es
sichtbar im PATH lag. Der Launcher schaltet die Umlenkung für genau diesen
einen Aufruf ab.

Mehrere davon scheiterten lautlos. Ein Tunnel, der nicht startet, oder der
startet und nie lauscht, wird jetzt unabhängig von der `log`-Einstellung nach
`hlswfix.log` geschrieben. Alles Schlimmere sagt sich per Meldungsfenster. Und
`log = 2` gibt jedes Paket als Hex aus, was das Werkzeug war, das der Raterei
ein Ende gemacht hat.

## Was getestet wurde

HLSW 1.4.0.5 unter Windows 10, gegen sieben Server gleichzeitig über mehrere
Stunden. Abfragen, Spieler- und Rules-Listen sowie rcon funktionieren,
einschließlich der großen SourceMod-Administrationsoberfläche in
`cfg\rcon_sourcemod.cfg`.

Die Heimrufe wurden mit `log = 2` davor und danach gemessen, und ihre Sperre
hat sonst nichts verändert: die Spielserver antworteten genau wie zuvor.

Die Drosselung wurde an einem Paketmitschnitt über 28 Minuten geprüft, und der
Mitschnitt hat umgeworfen, was vorher über sie angenommen wurde. HLSW fragt
nicht nach einem Zeitgeber, sondern in dem Moment, in dem eine Antwort ankommt,
und solange es wartet, sendet es nichts. Ungedrosselt brachte es den gerade
angesehenen Server auf 50 Abfragen pro Sekunde und legte über sieben Server
hinweg 176 Datagramme pro Sekunde auf die Leitung, wobei fünf davon zwischen 8
und 24 Prozent ihrer Antworten verloren. Mit Drosselung durch Verwerfen lief
jeder Server, der nicht gerade auf dem Schirm war, mit einer Abfrage alle 2,04
Sekunden und einer offenen Antwort während 2,03 davon, was HLSW als Timeout
anzeigt. Daraus wurde das Verzögern statt des Verwerfens.

`hide_duplicate_info` hat einen eigenen Test, weil der Server, der dazu geführt
hat, mitten am Nachmittag aufhörte, doppelt zu antworten, und ein Test nicht von
der Laune eines Servers abhängen darf. Drei nachgebaute Server auf der lokalen
Schleife decken die Fälle ab, auf die es ankommt: einer antwortet immer doppelt,
einer spricht ausschließlich das alte Format und darf nie angefasst werden, und
einer stellt die moderne Antwort ein, damit man das Zehn-Sekunden-Fenster
ablaufen und die alte Antwort zurückkommen sieht.

GoldSrc funktioniert ebenfalls, und wie das zuerst falsch gemacht wurde, ist
das Aufschreiben wert, weil es fast einen ganzen Tag gekostet hat.

Diese Server beantworten `A2S_INFO`, ohne überhaupt eine Challenge zu
verlangen, sie hatten das Problem also nie, für das der Fix existiert. Die erste
Fassung hängte trotzdem eine an, sobald sie eine aus der Spieler- oder
Rules-Abfrage kannte, und das sah sicher aus: aus dem damaligen Paketmitschnitt
gegen zwei Counter-Strike-1.6-Server ausgezählt, 29 von 29 und 15 von 15
Info-Abfragen mit Challenge beantwortet. Sie ignorierten, was sie nicht
angefordert hatten.

Bis einer aufhörte, es zu ignorieren. Derselbe Server beantwortete später am
selben Tag eine blanke Abfrage über 25 Bytes vier von vier Mal und eine über 29
Bytes mit angehängter Challenge null von vier Mal. In HLSW verstummte er
vollständig und sah aus wie ein Netzproblem, und das war es nicht: es waren vier
Bytes von uns am Ende einer Abfrage, die er ohne sie bestens verstanden hatte.

Deshalb wird eine Challenge jetzt nur noch an Server angehängt, die bewiesen
haben, dass sie eine wollen, und der einzige anerkannte Beweis ist, dass dieser
Server eine blanke `A2S_INFO` von uns mit `0x41` beantwortet hat. Dazu kommt ein
Rückweg: bleiben drei Abfragen mit Challenge hintereinander ohne jede Antwort,
wird die Annahme fallengelassen und die nächste geht wieder blank raus. Ein
Server, der seine Meinung ändert, wird so verfolgt statt mit ihm gestritten.

Alles, was hlsw.net brauchte, bleibt kaputt. Die Weblisten und `GamersSearch`
haben niemanden mehr zum Reden, sie bleiben also leer und Server müssen von
Hand eingetragen werden.

## Übersetzungen

HLSW kam aus Deutschland und hatte weit darüber hinaus Anhänger: es brachte
Sprachdateien für rund fünfzehn Sprachen mit, und die Server, auf die man es
richtete, verteilten sich über ebenso viele. Übersetzungen sind also
willkommen.

Kopiere `README.md`, nenn die Kopie `README.<code>.md` mit dem üblichen
zweibuchstabigen Sprachcode, übersetze sie, und trag dich in die Sprachzeile
ganz oben in jeder `README*.md` ein. Messwerte und Dateinamen bitte so lassen,
wie sie sind.

## Lizenz

Dieses Projekt steht unter der [MIT-Lizenz](LICENSE).

Es steht in keiner Verbindung zu HLSW oder dessen Autoren und wird von ihnen
nicht unterstützt. Es enthält keinen HLSW-Code und verändert den Inhalt keiner
HLSW-Datei. HLSW ist Copyright (c) 2000-2009 Stripf Software und wurde als
Freeware veröffentlicht.

[releases]: https://github.com/marcanxo/hlswfix/releases
