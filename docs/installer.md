# Geführte Einrichtung auf macOS und Linux

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
Bereits abgeschlossene Schritte bleiben bei einem Abbruch erhalten.

1. Vorhandene Werkzeuge lokal prüfen. Fehlende Werkzeuge können über einen
   vorhandenen Homebrew-, apt-, dnf- oder pacman-Paketmanager installiert werden.
   Der genaue Befehl steht vor der Frage; Systempakete benötigen gegebenenfalls
   dein Administratorpasswort. Homebrew selbst wird nicht automatisch installiert.
2. Auf Apple Silicon wählen, ob der Compiler M2-Scheduling automatisch nutzen
   soll. Die arm64-Instruktionsbasis bleibt kompatibel mit anderen Apple-Macs.
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
6. Optional die Mac-App nach `~/Applications/` kopieren oder unter Linux einen
   persönlichen Menüeintrag anlegen. Eine vorhandene Mac-App wird nicht ersetzt;
   das aktuelle Paket im Build-Ordner bleibt direkt nutzbar.
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
make test                        # zusätzlich die Tests ausführen
make build CONFIG=Debug
make build CPU_TUNING=generic     # portables Scheduling statt automatischem M2-Tuning
```

Diese direkten Befehle fragen nicht nach jedem Build-Schritt und passen für
Entwickler/CI. `build.bat` und die Windows-Builds bleiben unverändert nutzbar.
