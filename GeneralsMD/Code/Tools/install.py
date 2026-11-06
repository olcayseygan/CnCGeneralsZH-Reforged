#!/usr/bin/env python3
"""Interactive macOS/Linux setup. Retail data is always kept outside the checkout."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import os
import platform
import plistlib
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_i18n import prompt, translator as _  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
SYSTEM_APPLICATIONS = Path('/Applications')
APP_NAME = 'Zero Hour Reforged.app'


def confirm(message):
    try:
        answer = prompt('\n%s %s' % (message, _.hint))
    except EOFError:
        raise SystemExit(_('Input ended; no further steps will be run.'))
    return _.is_yes(answer)


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


def writable_app_folder(folder):
    """Check the closest existing parent without creating folders or elevating rights."""
    while not folder.exists() and folder != folder.parent:
        folder = folder.parent
    return folder.is_dir() and os.access(folder, os.W_OK | os.X_OK)


def choose_app_destination():
    personal = Path.home() / 'Applications'
    default = '1' if writable_app_folder(SYSTEM_APPLICATIONS) else '2'
    if default == '2':
        print(_('Cannot write to %s without additional permissions. The personal Applications folder is recommended; no administrator command will be run.', SYSTEM_APPLICATIONS))
    print(_('Choose the application destination:'))
    print(_('  1. All users: %s', SYSTEM_APPLICATIONS))
    print(_('  2. This user only: %s', personal))
    print(_('  0. Keep the app in the build folder'))
    while True:
        answer = prompt(_('Destination [Enter = %s]: ', default)).strip() or default
        if answer == '0':
            return None
        if answer not in ('1', '2'):
            print(_('Choose 1, 2 or 0.'))
            continue
        folder = SYSTEM_APPLICATIONS if answer == '1' else personal
        if not writable_app_folder(folder):
            print(_('Cannot write to %s. Choose another destination or skip installation.', folder))
            continue
        # Resolve the parent, not the app leaf: an existing app symlink must be refused.
        return folder.expanduser().resolve() / APP_NAME


def verify_macos_app(app):
    """Validate the launch entry point and the complete bundle's existing signature."""
    app = Path(app).resolve()
    plist = app / 'Contents/Info.plist'
    try:
        if app not in plist.resolve().parents:
            raise ValueError('external Info.plist')
        with plist.open('rb') as handle:
            info = plistlib.load(handle)
        executable = info.get('CFBundleExecutable')
        if not isinstance(executable, str) or not executable or executable in ('.', '..') \
                or any(char in executable for char in ('/', '\\', '\0')):
            raise ValueError('unsafe CFBundleExecutable')
        macos = app / 'Contents/MacOS'
        binary = macos / executable
        if app not in macos.resolve().parents or macos.resolve() not in binary.resolve().parents \
                or not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError('missing or non-executable main program')
        if info.get('CFBundlePackageType') != 'APPL' or any(
                not isinstance(info.get(key), str) or not info[key].strip()
                for key in ('CFBundleShortVersionString', 'CFBundleVersion')):
            raise ValueError('missing application version')
    except (OSError, ValueError, AttributeError, plistlib.InvalidFileException) as error:
        raise ValueError(_('Invalid application bundle at %s: %s', app, error)) from error
    command = ['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise ValueError(_('Application signature verification failed at %s: %s', app, result.stderr.strip()))
    return info


def app_version(info):
    return _('Version %s, build %s', info['CFBundleShortVersionString'],
             info.get('ZHReforgedCommit') or info['CFBundleVersion'])


def same_file_identity(path, previous):
    current = path.lstat()
    return previous is not None and (current.st_dev, current.st_ino) == (previous.st_dev, previous.st_ino)


def install_app_bundle(source, destination, replace_existing=False):
    """Stage on the target volume; publish by rename and restore the previous app on failure."""
    source = Path(source).resolve()
    destination = Path(destination)
    destination = destination.parent.resolve() / destination.name
    if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
        raise ValueError(_('The app destination is a symbolic link or a file and was kept: %s', destination))
    if source == destination:
        return destination, verify_macos_app(destination)
    if source in destination.parents or destination in source.parents:
        raise ValueError(_('Source and destination folders must not contain one another.'))
    previous = destination.lstat() if destination.exists() else None
    if previous and not replace_existing:
        raise ValueError(_('An application already exists at %s; explicit replacement is required.', destination))
    expected = verify_macos_app(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix='.zhr-app-install-', dir=destination.parent))
    backup = None
    published = reserved = False
    published_identity = reserved_identity = None
    preserve = False
    try:
        staged = scratch / APP_NAME
        shutil.copytree(source, staged, symlinks=True)
        if verify_macos_app(staged) != expected:
            raise ValueError(_('The staged application does not match the selected build.'))
        if previous:
            if not same_file_identity(destination, previous):
                raise ValueError(_('The destination changed during installation and was kept: %s', destination))
            # Record the recovery path before rename: SIGINT may arrive after the filesystem
            # change commits but before Python executes the following assignment.
            backup = scratch / 'previous.app'
            os.replace(destination, backup)
        # Exclusive reservation prevents an app that appeared while copying from being overwritten.
        reserved = True
        destination.mkdir()
        reserved_identity = destination.lstat()
        published_identity = staged.lstat()
        published = True
        os.replace(staged, destination)
        installed = verify_macos_app(destination)
        if installed != expected:
            raise ValueError(_('The installed application does not match the selected build.'))
        return destination.resolve(), installed
    except BaseException as original:
        try:
            if published and (destination.exists() or destination.is_symlink()) \
                    and same_file_identity(destination, published_identity):
                os.replace(destination, scratch / 'rejected.app')
            elif reserved and (destination.exists() or destination.is_symlink()):
                if reserved_identity is None:
                    # Neither an interrupted mkdir nor a collision proves that this directory
                    # is ours. Never remove it or restore a backup over an unowned destination.
                    if not isinstance(original, FileExistsError) or (backup is not None and backup.exists()):
                        raise OSError('reservation ownership unknown')
                else:
                    if not same_file_identity(destination, reserved_identity):
                        raise OSError('reserved destination changed')
                    destination.rmdir()
            if backup is not None and backup.exists():
                os.replace(backup, destination)
        except BaseException as recovery:
            preserve = True
            raise OSError(_('Application installation failed and recovery could not finish. Recovery files were kept at %s. Original error: %s', scratch, original)) from recovery
        raise
    finally:
        if not preserve:
            try:
                shutil.rmtree(scratch)
            except OSError:
                print(_('Temporary application files were kept at %s; cleanup could not finish.', scratch), file=sys.stderr)


def install_macos_app(source):
    source = source.resolve()
    if confirm(_('Install the built macOS app? You can choose /Applications or ~/Applications; an existing app is only replaced after a separate confirmation.')):
        destination = choose_app_destination()
        if destination is not None:
            if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
                raise ValueError(_('The app destination is a symbolic link or a file and was kept: %s', destination))
            replace_existing = destination.exists()
            if source == destination:
                question = _('Verify the app already at %s?', destination)
            elif replace_existing:
                question = _('Copy, verify and replace the existing app at %s with this build? The previous app is restored if installation fails.', destination)
            else:
                question = _('Copy and verify the application now?\n%s -> %s', source, destination)
            if confirm(question):
                installed, info = install_app_bundle(source, destination, replace_existing=replace_existing)
                print(_('Application ready and verified at %s\n%s', installed, app_version(info)))
                return installed
    print(_('App installation skipped. The build folder will be used for launch: %s', source))
    return source


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
    if confirm(_('Build, test and package the %s configuration now? This may download dependencies and write build files.', config)):
        run([ROOT / ('build-macos.sh' if system == 'Darwin' else 'build-linux.sh'), config, 'test'])
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
            print(_('Choose the Zero Hour folder on first launch. See the README for Steam and GOG notes.'))
    configured_build = os.environ.get('BUILD', '')
    build = Path(configured_build) if configured_build else ROOT / ('build-mac' if system == 'Darwin' else 'build-linux')
    if not build.is_absolute():
        build = ROOT / build
    app = build / 'Zero Hour Reforged.app'
    binary = build / 'ZeroHourReforged/bin/generals'
    if system == 'Darwin' and app.is_dir():
        app = install_macos_app(app)
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
