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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_i18n import prompt, translator as _  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]


def confirm(message):
    try:
        answer = prompt('\n%s %s' % (message, _.hint))
    except EOFError:
        raise SystemExit(_('Input ended; no further steps will be run.'))
    return _.is_yes(answer)


def choose_text_setting(message, choices, current):
    if current not in dict(choices):
        raise ValueError(_('Unknown text setting: %s', current))
    print('\n' + message)
    for index, (value, label) in enumerate(choices, 1):
        print('  %d. %s%s' % (index, label, ' *' if value == current else ''))
    while True:
        answer = prompt(_('Selection [Enter = %s]: ', dict(choices)[current])).strip().casefold()
        if not answer:
            return current
        if answer in dict(choices):
            return answer
        if answer.isdecimal() and 1 <= int(answer) <= len(choices):
            return choices[int(answer)-1][0]
        print(_('Choose a listed number or press Enter to keep the current choice.'))


def run(command, **kwargs):
    print('\n$ ' + ' '.join(shlex.quote(str(arg)) for arg in command), flush=True)
    kwargs.setdefault('stderr', subprocess.STDOUT)
    subprocess.run([str(arg) for arg in command], check=True, cwd=ROOT, **kwargs)


def external_path(value):
    path = Path(value).expanduser().resolve()
    if path == ROOT or ROOT in path.parents or path in ROOT.parents:
        raise ValueError(_('The data destination must be outside this repository and its parent folders.'))
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
                print(_('Search limit reached. You can enter another game folder manually.'))
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
        raw = prompt(_('Enter a number or the full installation path (empty = skip): ')).strip()
        if not raw:
            return None
        try:
            path = candidates[int(raw)-1] if raw.isdecimal() and 1 <= int(raw) <= len(candidates) else Path(raw).expanduser()
            path = path.resolve()
            if path == ROOT or ROOT in path.parents:
                raise ValueError(_('Please choose an installation outside the repository.'))
            if not marker(path, required):
                raise ValueError(_('%s is missing from this folder.', required))
            return path
        except (ValueError, OSError) as error:
            print(error)


def copy_game(source, destination, exclude=()):
    destination = external_path(str(destination))
    if destination.exists():
        raise ValueError(_('The destination already exists. Choose a new folder; existing data is never overwritten.'))
    # Copy complete installed folders. Never follow source links into arbitrary
    # locations or carry executable links into the destination.
    def reject_links(folder, names):
        skipped = [name for name in names if Path(folder) == source and name.casefold() in exclude]
        for name in names:
            if name in skipped:
                continue
            entry = Path(folder) / name
            if entry.is_symlink():
                raise ValueError(_('The installation contains a symbolic link: %s. Please use a complete local copy.', entry))
        return skipped
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError(_('Source and destination folders must not contain one another.'))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, ignore=reject_links, copy_function=shutil.copy2)


def prerequisites(system):
    if not confirm(_('First check the operating system and installed development tools locally?')):
        return
    tools = ('git', 'curl', 'unzip', 'cmake', 'ninja', 'make', 'cc', 'c++')
    missing = [tool for tool in tools if shutil.which(tool) is None]
    print(_('System: %s / %s; missing tools: %s', system, platform.machine(), ', '.join(missing) or _('none')))
    if system == 'Darwin':
        if subprocess.run(['xcode-select', '-p'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            if confirm(_("Open Apple's installer dialog for the Command Line Tools (system change)?")):
                run(['xcode-select', '--install'])
                raise SystemExit(_('Please finish the Apple installation, then run make install again.'))
        if missing:
            if shutil.which('brew'):
                command = ['brew', 'install', 'cmake', 'ninja', 'unifdef', 'pkg-config', 'nasm']
                if confirm(_('Homebrew may download and install development tools:\n%s\nContinue?', shlex.join(command))):
                    run(command)
            else:
                print(_('Please install CMake/Ninja, optionally with Homebrew (https://brew.sh). The installer does not install Homebrew itself.'))
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
            if confirm(_('The Linux build also needs graphics and audio headers.\nInstall these packages with administrator rights (the package manager asks as well)?\n%s', shlex.join(command))):
                run(command)
            break
    else:
        print(_('No supported package manager found. build-linux.sh names missing packages during the build.'))


def main():
    if len(sys.argv) > 1:
        if sys.argv[1:] == ['--help']:
            print(__doc__ + '\n' + _('Usage: make install (interactive, never answers yes automatically).')); return 0
        raise SystemExit(_('Unknown option. Usage: make install'))
    if not sys.stdin.isatty():
        raise SystemExit(_('The installer needs a terminal and explicit answers. For CI use build-macos.sh/build-linux.sh.'))
    system = platform.system()
    if system not in ('Darwin', 'Linux'):
        raise SystemExit(_('This installer supports macOS and Linux. On Windows use build.bat.'))
    print(_('Zero Hour Reforged - guided setup\nEvery executing step needs your consent; Enter means no.\n'
            'The installer does not download original game data. You need your own complete installations of Generals and Zero Hour.\n'
            'Game data stays outside this repository. Steam/GOG credentials are never requested.\n'
            'Cancel any time with Ctrl+C; completed steps are kept.'))
    config = os.environ.get('ZH_BUILD_CONFIG', 'Release') or 'Release'
    if config.lower() == 'clean':
        raise ValueError(_('Choose a build configuration such as Release or Debug; cleaning is a separate command.'))
    prerequisites(system)
    if confirm(_('Prepare the local %s build? Downloads, configuration, compiling, tests and packaging each get their own question.', config)):
        print(_('Text choices set defaults for new preferences; existing personal settings take precedence. They change names only, not models, voices or gameplay.'))
        personnel = choose_text_setting(_('Unit names:'),
            [('unchanged', _('Unchanged')), ('humans', _('Humans')), ('robots', _('Robots'))],
            os.environ.get('ZH_TEXT_PERSONNEL', 'unchanged') or 'unchanged')
        factions = choose_text_setting(_('Faction names:'),
            [('unchanged', _('Unchanged')), ('original', _('Original countries')), ('adapted', _('Adapted factions'))],
            os.environ.get('ZH_TEXT_FACTIONS', 'unchanged') or 'unchanged')
        env = dict(os.environ, ZH_GUIDED_BUILD='1', ZH_TEXT_PERSONNEL=personnel, ZH_TEXT_FACTIONS=factions)
        run([ROOT / ('build-macos.sh' if system == 'Darwin' else 'build-linux.sh'), config, 'test'], env=env)
    selected = None
    if confirm(_('Select or search for your own original game installations and optionally copy them?')):
        candidates = {'zero_hour': [], 'generals': []}
        if confirm(_('Search existing Steam libraries, ~/Games, GOG folders and Wine/CrossOver/Whisky/Bottles directories (local read-only, no downloads)?')):
            candidates = discover(search_roots(Path.home()))
            print(_('Found %d Zero Hour and %d Generals folders.', len(candidates['zero_hour']), len(candidates['generals'])))
        zero = choose_game(_('Zero Hour'), 'INIZH.big', candidates['zero_hour'])
        base = choose_game(_('Generals (base game)'), 'Textures.big', candidates['generals'])
        selected = zero
        if zero and base and confirm(_('Copy the complete selected game folders into a new personal data folder?\nThe sources are kept; existing destinations are never overwritten.')):
            default = Path.home() / 'Games/Command & Conquer Generals Zero Hour'
            while True:
                try:
                    raw = prompt(_('New data folder [%s]: ', default)).strip()
                    target = external_path(raw or str(default))
                    if target.exists():
                        raise ValueError(_('This destination folder already exists. Please choose a new name.'))
                    if target == zero or zero in target.parents or target == base or base in target.parents:
                        raise ValueError(_('The data destination must not be inside a source installation.'))
                    break
                except (ValueError, OSError) as error:
                    print(error)
            if confirm(_('Copy now:\n%s -> %s\n%s -> %s\nThis can take several GB. Continue?', zero, target, base, target / 'ZH_Generals')):
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
                print(_('Both games were copied. No original files were added to the repository.'))
        elif zero:
            print(_('The existing Zero Hour folder is used directly. On first launch the game may ask separately for Generals.'))
        else:
            print(_('Choose the Zero Hour folder on first launch. Steam/GOG notes: docs/macos.md'))
    configured_build = os.environ.get('BUILD', '')
    build = Path(configured_build) if configured_build else ROOT / ('build-mac' if system == 'Darwin' else 'build-linux')
    if not build.is_absolute():
        build = ROOT / build
    app = build / 'Zero Hour Reforged.app'
    binary = build / 'ZeroHourReforged/bin/generals'
    if system == 'Darwin' and app.is_dir():
        destination = Path.home() / 'Applications/Zero Hour Reforged.app'
        if confirm(_('Copy the built app to %s (no administrator rights)?', destination)):
            if destination.exists():
                print(_('An app already exists there and is kept. The built app can be launched directly.'))
            else:
                shutil.copytree(app, destination, symlinks=True); app = destination
        command = ['open', str(app)] + (['--args', '-root', str(selected)] if selected else [])
    elif system == 'Linux' and binary.is_file():
        menu = build / 'ZeroHourReforged/add-to-menu.sh'
        if confirm(_('Add a launcher to your personal Linux application menu?')):
            run(['bash', menu])
        command = [str(binary)] + (['-root', str(selected)] if selected else [])
    else:
        print(_('No finished application package exists yet. make install can be run again.')); return 0
    if confirm(_('Launch the application now?\n%s', shlex.join(command))):
        run(command)
    print(_('Setup finished. Game data and settings are managed separately from the source code.'))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (KeyboardInterrupt, EOFError):
        print('\n' + _('Cancelled; completed steps are kept.'), file=sys.stderr); sys.exit(130)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('\n' + _('Setup stopped: %s\nCompleted steps are kept; fix the problem and run make install again.', error), file=sys.stderr); sys.exit(1)
