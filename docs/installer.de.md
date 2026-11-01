# Geführte Einrichtung auf macOS und Linux

[English](installer.md) · Deutsch

Starte im Repository:

```sh
make install
```

Alternativ funktioniert `./GeneralsMD/Code/Tools/install-posix.sh` ohne Make.
Python 3.8 oder neuer wird benötigt. Auf einem frischen Mac zuerst Apples Command
Line Tools installieren (`xcode-select --install`); falls Python fehlt, meldet der
Einstieg das vor weiteren Schritten. `make` allein zeigt die verfügbaren Befehle.

Der Installer fragt vor jedem Schritt. **Enter bedeutet Nein**, Strg+C bricht ab.
Ein Terminal ist erforderlich; es gibt absichtlich kein automatisches Ja.
Die Sprache folgt `LC_ALL`, `LC_MESSAGES` oder `LANG` und lässt sich mit
`ZH_INSTALL_LANG=de` (oder `en`) festlegen.
Nicht verfügbare oder fehlerhafte Sprachkataloge fallen auf Englisch zurück.
Enter, `n` und `no` gelten auch bei fehlerhaften Ja-Einträgen niemals als Zustimmung.
Bereits abgeschlossene Schritte bleiben bei einem Abbruch erhalten.

1. Vorhandene Werkzeuge lokal prüfen. Fehlende Werkzeuge können über einen
   vorhandenen Homebrew-, apt-, dnf- oder pacman-Paketmanager installiert werden.
   Der genaue Befehl steht vor der Frage; Systempakete benötigen gegebenenfalls
   dein Administratorpasswort. Homebrew selbst wird nicht automatisch installiert.
2. Hardware-spezifische Optimierungen (z. B. CPU-Scheduling auf Apple Silicon)
   werden automatisch erkannt und nicht abgefragt. Die Instruktionsbasis bleibt
   kompatibel mit anderen Geräten derselben Architektur; deaktivieren mit
   `CPU_TUNING=generic`.
3. Fehlende Drittanbieter-Quellen laden, CMake konfigurieren, kompilieren, Tests
   ausführen und die Anwendung paketieren. **Jede dieser Aktionen hat eine eigene
   Frage.** Wenn ein notwendiger Build-Schritt abgelehnt wird, endet dieser Build;
   die Einrichtung kann mit vorhandenen Spieldaten fortgesetzt werden.
4. Optional nach deinen bereits installierten Originalspielen suchen. Gelesen
   werden Steam-Bibliotheken einschließlich `libraryfolders.vdf`, persönliche
   Spieleordner und bekannte Wine-, CrossOver-, Whisky- und Bottles-Verzeichnisse.
   Die Suche folgt keinen untergeordneten Verzeichnislinks und hat ein Suchlimit;
   weitere Ordner können manuell angegeben werden. Es wird kein ganzer Datenträger
   durchsucht und kein Spiel von Steam oder GOG heruntergeladen.
5. Die gefundenen Ordner auswählen oder Pfade manuell eingeben. Benötigt werden
   Zero Hour mit `INIZH.big` und Generals mit `Textures.big`. Die ausgewählten
   Installationen können direkt verwendet oder vollständig in einen **neuen**
   persönlichen Datenordner kopiert werden. Vor dem Kopieren werden beide
   Quellen und Ziele nochmals angezeigt. Generals landet in `ZH_Generals/`.
   Vorhandene Datenziele werden nicht überschrieben. Symbolische Links in den
   zu kopierenden Daten werden abgelehnt; verwende eine vollständige lokale Kopie.
6. Optional die Mac-App für alle Benutzer nach `/Applications/` oder nur für das
   eigene Konto nach `~/Applications/` installieren. Beide Ziele werden angezeigt;
   ohne zusätzliche Berechtigungen beschreibbares `/Applications/` ist die Vorgabe.
   Andernfalls erklärt der Installer die Berechtigungsgrenze und empfiehlt den
   persönlichen Ordner. Kopieren und Ersetzen einer vorhandenen App müssen
   ausdrücklich bestätigt werden. Zunächst wird eine temporäre Kopie erstellt;
   ausführbare Datei, Versionsangaben und Codesignatur werden vor dem Ersetzen
   und nochmals am endgültigen Ziel geprüft. Bei einem Fehler wird die vorherige
   App wiederhergestellt; scheitert die Wiederherstellung, wird der aufbewahrte
   Sicherungspfad angezeigt. Nach Erfolg erscheinen der tatsächliche Installationspfad
   und die Build-Version. Wer die Installation ablehnt, startet ausdrücklich das
   Paket im Build-Ordner. Unter Linux bleibt der persönliche Menüeintrag optional.
7. Optional das Spiel starten und den gewählten Zero-Hour-Pfad übergeben. Falls
   Generals separat liegt, kann die Anwendung dessen Ordner beim Start erfragen.

Originaldateien dürfen nicht in dieses Repository kopiert werden. Der Installer
prüft dafür auch auf Pfade über symbolische Links. Die Quellen werden nicht
verändert. Bei einem Kopierfehler bleiben die Originalinstallationen erhalten;
kurzzeitige Arbeitskopien werden aufgeräumt. Abbrüche während des abschließenden
Verschiebens können einen unvollständigen neuen Zielordner hinterlassen; wähle bei
Wiederholung einen neuen Ordner oder verwende deine unveränderten Quellen direkt.

Für eigene Steam-Daten und die derzeitige GOG-Verfügbarkeit siehe
[die Anleitung zu den Originalspielen](macos.md#obtain-your-own-generals-and-zero-hour-data).
Der Installer fragt niemals nach Steam-/GOG-Passwörtern. Er lädt keine EA-Archive,
Filme, Musik oder Sprachdateien herunter. Die GPL-Freigabe des bestehenden Codes
und seine Copyright-Hinweise bleiben davon getrennt.

Die bisherigen direkten Build-Einstiege bleiben verfügbar:

```sh
make build                       # Release bauen und paketieren
make test                        # bauen, alle Tests ausführen und paketieren
make test-only                   # Tests im vorhandenen Build erneut ausführen
make install CONFIG=Debug        # auch der geführte Build beachtet CONFIG
make install JOBS=8 TEST_JOBS=4
make install PERSONNEL=humans FACTIONS=original
make test-only TEST_JOBS=1        # bei Bedarf nacheinander testen
make build CPU_TUNING=generic     # portables Scheduling statt automatischer CPU-Anpassung
```

`PERSONNEL=unchanged|humans|robots` und `FACTIONS=unchanged|original|adapted`
wählen unabhängig voneinander Vorgaben für Einheiten- und Fraktionsbezeichnungen.
Im geführten Build werden beide Auswahlen vor dem Kompilieren angeboten; Enter
behält die angezeigte Vorgabe. Im Spiel lassen sich beide unter den
Oberflächen-/Spracheinstellungen ändern; nach einem Neustart gilt die neue Wahl.
Explizite persönliche Einstellungen haben Vorrang vor Build-Vorgaben.

Die Sprache bleibt eine eigene Einstellung. Die Varianten ändern gezielte kurze
Namen, beispielsweise Menschen-/Roboterbezeichnungen und USA/Westliche Allianz.
Sie ersetzen weder sämtliche Kampagnentexte noch Modelle, Videos oder Stimmen
und stellen keine vollständige andere Originaledition her. Originaldaten werden
dafür weder heruntergeladen noch ins Repo kopiert. `unchanged` bewahrt das
bisherige Verhalten. Direkte CMake-Aufrufe verwenden `ZH_TEXT_PERSONNEL` und
`ZH_TEXT_FACTIONS` mit denselben Werten; unter Windows sind diese CMake-Optionen
ebenfalls verfügbar.

Die Zahl der Build-Prozesse wird anhand von CPU und RAM gewählt, automatisch
höchstens acht. Tests laufen automatisch mit höchstens vier Prozessen parallel.
`JOBS` und `TEST_JOBS` akzeptieren `auto` oder eine positive ganze Zahl. Auch der
FFmpeg-Build hält sich an die Build-Grenze. `make -j` startet keine konkurrierenden
Abläufe im selben Build-Verzeichnis oder Terminal. Vorhandene CMake-/CTest-
Parallelitätseinstellungen werden berücksichtigt, sofern keine entsprechende
Make-Variable angegeben wird.

`BUILD=/pfad/zum/build` wählt ein anderes Build-Verzeichnis, auch für die vom
Installer angebotene Anwendung. `CMAKE=/pfad/zu/cmake` wählt CMake. Beide lassen
sich ebenfalls als Umgebungsvariable setzen und haben dann Vorrang vor
`build.local.sh`. Beispiel:

```sh
make install CONFIG=Debug BUILD="$HOME/Builds/Zero Hour Debug"
make test-only CONFIG=Debug BUILD="$HOME/Builds/Zero Hour Debug"
```

`test-only` startet die vollständige vorhandene Testsuite ohne Downloads,
Konfiguration, Kompilieren oder Paketierung. Das Verzeichnis muss bereits für die
angegebene `CONFIG` konfiguriert sein. Nach Quellcodeänderungen mit `make test`
zuerst neu bauen. Zeitlimits und bestehende Ressourcensperren bleiben erhalten.
Große Engine-Tests können mehrere Minuten dauern, ohne zwischendurch eine eigene
Meldung auszugeben. `Skipped` bedeutet meistens, dass Originalspieldaten oder
externe Referenzwerkzeuge fehlen: Der Test ist damit weder fehlgeschlagen noch
inhaltlich geprüft. Siehe [Testanleitung](../README.md#how-it-is-tested).

Die direkten Befehle fragen nicht nach jedem Schritt und passen für Entwickler
und CI. `make install` behält alle Bestätigungen. Fragen werden vollständig
angezeigt und geflusht, bevor auf die Antwort gewartet wird. Kindprozesse nutzen
denselben Ausgabekanal für ihre Meldungen. `build.bat` und die Windows-Builds
bleiben unverändert nutzbar.
