#!/usr/bin/env python3
"""Interactive macOS/Linux setup. Retail data is always kept outside the checkout."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def confirm(message):
    try:
        answer = input('\n' + message + ' [j/N] ').strip().lower()
    except EOFError:
        raise SystemExit('Eingabe beendet; es werden keine weiteren Schritte ausgeführt.')
    return answer in ('j', 'ja', 'y', 'yes')


def run(command, **kwargs):
    print('\n$ ' + ' '.join(shlex.quote(str(arg)) for arg in command), flush=True)
    subprocess.run([str(arg) for arg in command], check=True, cwd=ROOT, **kwargs)


def external_path(value):
    path = Path(value).expanduser().resolve()
    if path == ROOT or ROOT in path.parents or path in ROOT.parents:
        raise ValueError('Das Datenziel muss außerhalb des Repos und seiner übergeordneten Ordner liegen.')
    return path


def marker(folder, name):
    try:
        return any(child.is_file() and child.name.casefold() == name.casefold() for child in folder.iterdir())
    except OSError:
        return False


def steam_libraries(steam):
    """Read only library path entries; no Steam login, service or game download."""
    import re
    yield steam
    try:
        text = (steam / 'steamapps/libraryfolders.vdf').read_text(encoding='utf-8')
    except (OSError, UnicodeError):
        return
    for match in re.finditer(r'"(?:path|\d+)"\s+"((?:\\.|[^"\\])*)"', text):
        raw = match.group(1).replace('\\\\', '\\').replace('\\"', '"')
        candidate = Path(raw).expanduser()
        if candidate.is_absolute():
            yield candidate


def search_roots(home):
    # Deliberately bounded to game libraries and bottles. No whole-disk scan,
    # privilege escalation, mounted network volumes or account credentials.
    for steam in (home / 'Library/Application Support/Steam', home / '.steam/steam',
                  home / '.local/share/Steam', home / '.var/app/com.valvesoftware.Steam/data/Steam'):
        for library in steam_libraries(steam):
            yield library / 'steamapps/common'
    for relative in ('Games', 'GOG Games', 'Applications', '.wine/drive_c',
                     'Library/Application Support/CrossOver/Bottles',
                     'Library/Containers/com.isaacmarovitz.Whisky/Bottles',
                     '.local/share/bottles/bottles', '.var/app/com.usebottles.bottles/data/bottles/bottles'):
        yield home / relative


def discover(roots, limit=80000):
    found, seen, visited = {'zero_hour': [], 'generals': []}, set(), 0
    for root in roots:
        # Resolve roots for Steam's conventional .steam symlink; do not follow
        # arbitrary symlinked descendants (cycles or unrelated personal folders).
        root = root.resolve()
        if not root.is_dir() or root == ROOT or ROOT in root.parents:
            continue
        for directory, children, files in os.walk(root, followlinks=False):
            folder = Path(directory)
            children[:] = sorted(name for name in children if not name.startswith('.')
                                 and name not in ('node_modules', '__pycache__')
                                 and (folder / name).resolve() != ROOT)
            visited += 1
            if visited > limit:
                print('Suchlimit erreicht. Einen weiteren Spieleordner kannst du manuell angeben.')
                return found
            names = {name.casefold() for name in files}
            for key, required in (('zero_hour', 'inizh.big'), ('generals', 'textures.big')):
                if required in names and (key, folder) not in seen:
                    found[key].append(folder); seen.add((key, folder))
    return found


def choose_game(label, required, candidates):
    print('\n' + label + ':')
    for index, path in enumerate(candidates, 1):
        print('  %d. %s' % (index, path))
    while True:
        raw = input('Nummer oder vollständigen Installationspfad eingeben (leer = überspringen): ').strip()
        if not raw:
            return None
        try:
            path = candidates[int(raw)-1] if raw.isdecimal() and 1 <= int(raw) <= len(candidates) else Path(raw).expanduser()
            path = path.resolve()
            if path == ROOT or ROOT in path.parents:
                raise ValueError('Bitte eine Installation außerhalb des Repos wählen.')
            if not marker(path, required):
                raise ValueError('In diesem Ordner fehlt ' + required + '.')
            return path
        except (ValueError, OSError) as error:
            print(error)


def copy_game(source, destination, exclude=()):
    destination = external_path(str(destination))
    if destination.exists():
        raise ValueError('Das Ziel existiert bereits. Bitte einen neuen Ordner wählen; vorhandene Daten werden nicht überschrieben.')
    # Copy complete installed folders. Never follow source links into arbitrary
    # locations or carry executable links into the destination.
    def reject_links(folder, names):
        skipped = [name for name in names if Path(folder) == source and name.casefold() in exclude]
        for name in names:
            if name in skipped:
                continue
            entry = Path(folder) / name
            if entry.is_symlink():
                raise ValueError('Die Installation enthält einen symbolischen Link: %s. Bitte eine vollständige lokale Kopie verwenden.' % entry)
        return skipped
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Quell- und Zielordner dürfen nicht ineinander liegen.')
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, ignore=reject_links, copy_function=shutil.copy2)


def prerequisites(system):
    if not confirm('Zuerst Betriebssystem und vorhandene Entwicklungswerkzeuge lokal prüfen?'):
        return
    tools = ('git', 'curl', 'unzip', 'cmake', 'ninja', 'make', 'cc', 'c++')
    missing = [tool for tool in tools if shutil.which(tool) is None]
    print('System: %s / %s; fehlende Werkzeuge: %s' % (system, platform.machine(), ', '.join(missing) or 'keine'))
    if system == 'Darwin':
        if subprocess.run(['xcode-select', '-p'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            if confirm('Apples Installationsdialog für die Command Line Tools öffnen (Systemänderung)?'):
                run(['xcode-select', '--install'])
                raise SystemExit('Bitte Apples Installation abschließen und make install erneut ausführen.')
        if missing:
            if shutil.which('brew'):
                command = ['brew', 'install', 'cmake', 'ninja', 'unifdef', 'pkg-config', 'nasm']
                if confirm('Homebrew darf Entwicklungswerkzeuge herunterladen und installieren:\n' + shlex.join(command) + '\nFortfahren?'):
                    run(command)
            else:
                print('Bitte CMake/Ninja installieren; optional mit Homebrew (https://brew.sh). Der Installer installiert Homebrew nicht selbst.')
        return
    packages = {
        'apt-get': 'cmake ninja-build g++ git curl unzip xz-utils make pkg-config python3 libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev libdrm-dev libgbm-dev libvulkan-dev libfontconfig-dev lld unifdef',
        'dnf': 'cmake ninja-build gcc-c++ git curl unzip xz make pkgconf-pkg-config python3 libX11-devel libXext-devel libXcursor-devel libXi-devel libXfixes-devel libXrandr-devel libXScrnSaver-devel libXtst-devel wayland-devel libxkbcommon-devel wayland-protocols-devel mesa-libEGL-devel libdrm-devel mesa-libgbm-devel vulkan-loader-devel fontconfig-devel lld unifdef',
        'pacman': 'cmake ninja gcc git curl unzip xz make pkgconf python libx11 libxext libxcursor libxi libxfixes libxrandr libxss libxtst wayland libxkbcommon wayland-protocols mesa libdrm vulkan-icd-loader vulkan-headers fontconfig lld unifdef',
    }
    for manager, names in packages.items():
        if shutil.which(manager):
            arguments = ['-S', '--needed'] if manager == 'pacman' else ['install']
            command = ['sudo', manager] + arguments + names.split()
            if confirm('Für den Linux-Build werden auch Grafik- und Audioheader benötigt.\nFolgende Pakete mit Administratorrechten installieren (der Paketmanager fragt zusätzlich)?\n' + shlex.join(command)):
                run(command)
            break
    else:
        print('Kein unterstützter Paketmanager gefunden. build-linux.sh nennt fehlende Pakete beim Build.')


def main():
    if len(sys.argv) > 1:
        if sys.argv[1:] == ['--help']:
            print(__doc__ + '\nAufruf: make install (interaktiv, kein automatisches Ja).'); return 0
        raise SystemExit('Unbekannte Option. Aufruf: make install')
    if not sys.stdin.isatty():
        raise SystemExit('Der Installer benötigt ein Terminal und ausdrückliche Antworten. Für CI bitte build-macos.sh/build-linux.sh verwenden.')
    system = platform.system()
    if system not in ('Darwin', 'Linux'):
        raise SystemExit('Dieser Installer unterstützt macOS und Linux. Unter Windows bitte build.bat verwenden.')
    print('Zero Hour Reforged – geführte Einrichtung\nJeder ausführende Schritt braucht deine Zustimmung; Enter bedeutet Nein.\n'
          'Der Quellcode enthält keine hier heruntergeladenen Original-Spieldaten. Du benötigst deine eigenen vollständigen Installationen von Generals und Zero Hour.\n'
          'Spieldaten bleiben außerhalb dieses Repos. Steam-/GOG-Zugangsdaten werden nie abgefragt.\nAbbrechen jederzeit mit Strg+C; abgeschlossene Schritte bleiben erhalten.')
    prerequisites(system)
    if confirm('Den lokalen Release-Build vorbereiten? Vor Downloads, Konfiguration, Kompilieren, Tests und Paketierung folgt jeweils eine eigene Frage.'):
        env = dict(os.environ, ZH_GUIDED_BUILD='1')
        if system == 'Darwin' and platform.machine() == 'arm64':
            tuning = confirm('Apple-Silicon-CPU-Tuning automatisch erkennen und auf M2/M2 Pro/M2 Max/M2 Ultra anwenden?\nDie kompatible arm64-Instruktionsbasis und die deterministischen Gleitkommaregeln bleiben erhalten.')
            env['ZH_MACOS_CPU_TUNING'] = 'auto' if tuning else 'generic'
        run([ROOT / ('build-macos.sh' if system == 'Darwin' else 'build-linux.sh'), 'Release', 'test'], env=env)
    selected = None
    if confirm('Eigene Original-Spielinstallationen auswählen oder suchen und gegebenenfalls kopieren?'):
        candidates = {'zero_hour': [], 'generals': []}
        if confirm('Vorhandene Steam-Bibliotheken, ~/Games, GOG-Ordner und Wine/CrossOver/Whisky/Bottles-Verzeichnisse durchsuchen (nur lokal lesen, keine Downloads)?'):
            candidates = discover(search_roots(Path.home()))
            print('%d Zero-Hour- und %d Generals-Ordner gefunden.' % (len(candidates['zero_hour']), len(candidates['generals'])))
        zero = choose_game('Zero Hour', 'INIZH.big', candidates['zero_hour'])
        base = choose_game('Generals (Grundspiel)', 'Textures.big', candidates['generals'])
        selected = zero
        if zero and base and confirm('Die vollständigen ausgewählten Spielordner in einen neuen persönlichen Datenordner kopieren?\nDie Quellen bleiben erhalten; vorhandene Ziele werden nicht überschrieben.'):
            default = Path.home() / 'Games/Command & Conquer Generals Zero Hour'
            while True:
                try:
                    raw = input('Neuer Datenordner [%s]: ' % default).strip()
                    target = external_path(raw or str(default))
                    if target.exists():
                        raise ValueError('Dieser Zielordner existiert bereits. Bitte einen neuen Namen wählen.')
                    if target == zero or zero in target.parents or target == base or base in target.parents:
                        raise ValueError('Das Datenziel darf nicht in einer Quellinstallation liegen.')
                    break
                except (ValueError, OSError) as error:
                    print(error)
            if confirm('Jetzt kopieren:\n%s → %s\n%s → %s\nDies kann mehrere GB belegen. Fortfahren?' % (zero, target, base, target / 'ZH_Generals')):
                target.parent.mkdir(parents=True, exist_ok=True)
                with tempfile.TemporaryDirectory(prefix='.zhr-setup-', dir=target.parent) as scratch:
                    staged = Path(scratch) / 'game'
                    copy_game(zero, staged, exclude=('zh_generals',))
                    copy_game(base, staged / 'ZH_Generals')
                    target.mkdir()  # exclusive: an existing target is never replaced
                    for child in staged.iterdir():
                        child.rename(target / child.name)
                    shutil.copystat(staged, target)
                selected = target
                print('Beide Spiele wurden kopiert. Originaldateien wurden nicht ins Repo übernommen.')
        elif zero:
            print('Der vorhandene Zero-Hour-Ordner wird direkt verwendet. Beim ersten Start kann das Spiel separat nach Generals fragen.')
        else:
            print('Später beim ersten Start den Zero-Hour-Ordner auswählen. Hinweise zu Steam/GOG: docs/macos.md')
    build = ROOT / ('build-mac' if system == 'Darwin' else 'build-linux')
    app = build / 'Zero Hour Reforged.app'
    binary = build / 'ZeroHourReforged/bin/generals'
    if system == 'Darwin' and app.is_dir():
        destination = Path.home() / 'Applications/Zero Hour Reforged.app'
        if confirm('Die gebaute App nach %s kopieren (keine Administratorrechte)?' % destination):
            if destination.exists():
                print('Dort existiert bereits eine App; sie bleibt erhalten. Die gebaute App kann direkt gestartet werden.')
            else:
                shutil.copytree(app, destination, symlinks=True); app = destination
        command = ['open', str(app)] + (['--args', '-root', str(selected)] if selected else [])
    elif system == 'Linux' and binary.is_file():
        menu = build / 'ZeroHourReforged/add-to-menu.sh'
        if confirm('Einen Starter im persönlichen Linux-Anwendungsmenü anlegen?'):
            run(['bash', menu])
        command = [str(binary)] + (['-root', str(selected)] if selected else [])
    else:
        print('Es ist noch kein fertiges Anwendungspaket vorhanden. make install kann erneut ausgeführt werden.'); return 0
    if confirm('Die Anwendung jetzt starten?\n' + shlex.join(command)):
        run(command)
    print('Einrichtung beendet. Spieldaten und Einstellungen werden separat vom Quellcode verwaltet.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (KeyboardInterrupt, EOFError):
        print('\nAbgebrochen; bereits abgeschlossene Schritte bleiben erhalten.', file=sys.stderr); sys.exit(130)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('\nEinrichtung gestoppt: %s\nBereits abgeschlossene Schritte bleiben erhalten; nach der Korrektur make install erneut ausführen.' % error, file=sys.stderr); sys.exit(1)
