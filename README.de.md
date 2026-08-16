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
2. `hlswfix-1.5.1.zip` von der [Releases-Seite][releases] laden. Nicht über den
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

Zwei Dinge, die man wissen sollte. Während HLSW läuft, siehst du jetzt zwei
Prozesse, `hlsw.exe` (der Launcher, der wartet) und `hlsw-real.exe` (HLSW
selbst). Und die eingebauten Serverlisten bleiben leer, weil sie auf hlsw.net
zeigten; Server müssen von Hand eingetragen werden.

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

Sieben Funktionen werden innerhalb des HLSW-Prozesses umgeleitet, keine
weiteren:

| umgeleitet | wozu |
|---|---|
| `sendto` | Merken, dass ein `A2S_INFO` rausging, es drosseln, und eine bereits bekannte Challenge gleich anhängen, damit wiederholte Abfragen nur einen Rundlauf kosten und der angezeigte Ping ehrlich bleibt statt sich zu verdoppeln. |
| `send` | Dasselbe, nur wird hier nie etwas zurückgehalten. Auf einer verbundenen Socket würde eine verworfene Abfrage die Antwort stranden lassen. |
| `recvfrom`, `recv`, `WSARecvFrom`, `WSARecv` | Das `0x41` abfangen, das auf so eine Abfrage antwortet, die Wiederholung nebenbei auf die Leitung legen, und HLSW genau das übergeben, was ankam. HLSW merkt nie, dass etwas passiert ist. |
| `connect` | Nur für die optionale rcon-Umleitung weiter unten. Ausschließlich Stream-Sockets, und ohne konfiguriertes `rcon_redirect` tut es gar nichts. |
| `select` | Nichts als ein Kontrollpunkt: feuert er nie, erreicht auch nichts die Hooks. Immer installiert, ändert kein Verhalten. |
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

Eine zurückgehaltene Abfrage wird verworfen, ohne dass etwas an ihre Stelle
tritt. Zwei andere Ansätze wurden vorher probiert und waren beide schlechter:

*Den Aufruf verzögern* geht überhaupt nicht. HLSW erledigt seine Socket-Arbeit
auf dem Thread, dem sein Fenster gehört, jedes Warten in einem Hook friert also
die Oberfläche ein.

*Die Abfrage lokal beantworten*, aus der vorigen Antwort, funktioniert, lügt
aber beim Ping. HLSW misst eine Abfrage vom eigenen Absenden bis zum Eintreffen
der Antwort, eine lokal erzeugte Antwort meldet also die Zeit, die ihre
Erzeugung gedauert hat: jeder gedrosselte Server zeigte 1 bis 3 ms, während der
gerade beobachtete, dessen Abfragen wirklich rausgehen, seine echten 26 zeigte.
Verwerfen lässt den angezeigten Ping stattdessen auf der letzten echten Messung
stehen, und HLSW liest die fehlenden Antworten nicht als Timeout, weil jedes
Intervall eine echte ankommt.

## Einstellungen

Alles in `hlswfix.ini` ist optional, die Datei selbst eingeschlossen. Der Fix
braucht keine Konfiguration. Die Kommentare in der Datei erklären jede
Einstellung, drei sind es wert, hier wiederholt zu werden.

**`title_version`** ist der Grund, warum in der Titelzeile **HLSW v1.5.1**
steht. Die letzte Version der Entwickler war 1.4.0.5 aus dem Jahr 2011, und die
neue Nummer sagt auf einen Blick, dass in diesem HLSW der Fix steckt. Geändert
wird nur die angezeigte Zeichenkette: HLSW baut den Titel aus seiner eigenen
Versionsressource, und die bleibt unberührt. Kommentiere die Zeile aus und die
ursprüngliche Version ist zurück.

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

Die Releases-Seite führt die SHA-256-Summen des Archivs und jeder Datei darin
auf, du kannst also prüfen, ob das Heruntergeladene das Veröffentlichte ist.
Beachte, dass ein Bau aus den Quellen diese Bytes nicht exakt reproduziert,
weil der Compiler jeden Bau stempelt: selbst bauen ist ein Ersatz für die
Prüfsummen, keine Gegenprobe.

## Selbst bauen

Hier ist nichts vor dir verborgen. Es sind ungefähr 1500 Zeilen C in
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

## HLSW spricht mit Servern, die ihm nicht mehr gehören

Das hat mit dem Fix nichts zu tun, aber du solltest es wissen. Aus HLSWs
eigener `license.txt`:

> HLSW transfers data to central servers owned by Stripf Software to enable
> several functionalities in HLSW like version check, console log, location
> detection, login, buddy list, etc.

Diese Domains sind weiterhin registriert und zeigen weiterhin auf einen Host,
sie liefern nur nichts Brauchbares mehr. Wer sie hält, kann das jederzeit
ändern, und ein Programm von 2011 redet unbeirrt weiter mit ihnen. Sperre sie
in deiner hosts-Datei oder deiner Firewall, wenn dich das stört, HLSW
funktioniert auch ohne sie. Dieses Projekt trägt zu diesem Verkehr weder etwas
bei noch blockiert es ihn. Wenn du es selbst sehen willst: `log = 2` hält jede
Adresse fest, zu der HLSW eine Verbindung aufbaut.

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

GoldSrc funktioniert ebenfalls, und es lohnt sich zu sagen, warum das nicht
selbstverständlich war. Diese Server beantworten `A2S_INFO`, ohne überhaupt
eine Challenge zu verlangen, sie hatten das Problem also nie. Der Fix hängt
trotzdem eine an, sobald er sie aus der Rules-Abfrage kennt. Ein Server, der
diese vier zusätzlichen Bytes für Müll hielte, wäre dadurch verstummt. Aus dem
Paketmitschnitt gegen zwei Counter-Strike-1.6-Server ausgezählt: 29 von 29 und
15 von 15 Info-Abfragen mit Challenge beantwortet. Sie ignorieren schlicht, was
sie nicht angefordert haben.

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
