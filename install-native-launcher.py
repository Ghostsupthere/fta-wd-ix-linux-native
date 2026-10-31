#!/usr/bin/env python3
"""Install this checkout's launcher in the user's applications menu and desktop."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path, help='write only here instead of installing in the user menu/desktop')
args = parser.parse_args()

root = Path(__file__).resolve().parent
launcher = root / 'launch-native.sh'
if not os.access(launcher, os.X_OK):
    raise SystemExit(f'Missing executable launcher: {launcher}')

def field(value):
    return str(value).replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r')

def executable(value):
    # Desktop Entry escaping is applied after Exec argument quoting.
    quoted = str(value).replace('\\', '\\\\')
    for character in ('"', '`', '$'):
        quoted = quoted.replace(character, '\\' + character)
    return '"' + field(quoted.replace('%', '%%')) + '"'

entry = '\n'.join([
    '[Desktop Entry]', 'Type=Application', 'Version=1.0',
    'Name=FTA Native Client', 'Comment=Launch the native Linux From The Ashes client',
    'Exec=' + executable(launcher), 'Icon=' + field(root / 'waystone.ico'),
    'Terminal=false', 'Categories=Game;RolePlaying;', 'StartupNotify=false', '',
])
applications = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'applications'
try:
    desktop = Path(subprocess.check_output(['xdg-user-dir', 'DESKTOP'], text=True).strip())
except (FileNotFoundError, subprocess.CalledProcessError):
    desktop = Path.home() / 'Desktop'
# Some desktops disable the Desktop directory by pointing it at HOME.
locations = [applications]
if desktop.is_absolute() and desktop != Path.home() and desktop.is_dir():
    locations.append(desktop)
if args.output_dir is not None:
    locations = [args.output_dir]
for directory in locations:
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / 'fta-native-client.desktop'
    destination.write_text(entry)
    destination.chmod(0o755)
    print(destination)
