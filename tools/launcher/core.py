# SPDX-License-Identifier: GPL-2.0-only
"""Launcher back end: the user's dump, the prepared game, the settings file and the game command.

Kept free of GTK so it can be checked from the command line (launcher.py --check/--prepare).
"""
import hashlib
import json
import os
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

# From a checkout (tools/launcher/ beside port/), or from the AppImage (AppRun sets PORT_APPDIR:
# app/ holds this file and the scripts, usr/bin/ the port).
APPDIR = os.environ.get('PORT_APPDIR')
if APPDIR:
    REPO = Path(APPDIR)
    PORT = REPO / 'usr' / 'bin'
    PROBE = PORT / 'bb-probe'
    GPU_LIB = PORT / 'gpu' / 'libbbgpu.so'
    SCRIPTS = REPO / 'app' / 'scripts'
    CREDITS = REPO / 'app' / 'CREDITS.md'
else:
    REPO = Path(__file__).resolve().parents[2]
    PORT = REPO / 'port'
    PROBE = PORT / 'out' / 'bb-probe'
    GPU_LIB = PORT / 'out' / 'gpu' / 'libbbgpu.so'
    SCRIPTS = PORT / 'scripts'
    CREDITS = REPO / 'CREDITS.md'
TITLE_ID = 'CUSA02716'
UPDATE_VERSION = '01.02'
APP_ID = 'battride-war-sousei-port'


class SetupError(Exception):
    """A problem the user can fix (a wrong folder, a missing build)."""


# ---- Controls -------------------------------------------------------------------------------
# PS4 inputs as port/src/runtime_pad/bindings.c names them, with the label shown and the
# defaults it uses (keep in step with bind_defaults there).
KEY_CONTROLS = [
    ('move_up', 'Move forward', 'W'), ('move_down', 'Move back', 'S'),
    ('move_left', 'Move left', 'A'), ('move_right', 'Move right', 'D'),
    ('look_up', 'Camera up', 'Keypad 8'), ('look_down', 'Camera down', 'Keypad 5'),
    ('look_left', 'Camera left', 'Keypad 4'), ('look_right', 'Camera right', 'Keypad 6'),
    ('square', '□  Normal attack', 'Mouse Left, J'),
    ('triangle', '△  Special 1', 'Mouse Right, I, F'),
    ('circle', '○  Special 2 / confirm', 'E, L, Return, F'),
    ('cross', '✕  Jump / Rider Cancel / back', 'Space, K, Backspace, V'),
    ('r2', 'R2  Super special', 'R, Mouse Middle'),
    ('l1', 'L1  Guard', 'Left Shift, Mouse X1'),
    ('r1', 'R1  Switch rider (TAG)', 'Tab, Mouse X2'),
    ('l2', 'L2', 'Q'), ('l3', 'L3', 'Left Ctrl'), ('r3', 'R3', 'C'),
    ('options', 'OPTIONS  Pause / next page', 'Escape'),
    ('touchpad', 'Touchpad', 'G'),
    ('up', 'D-pad up', 'Up, 3, Wheel Up'), ('down', 'D-pad down', 'Down, 4, Wheel Down'),
    ('left', 'D-pad left', 'Left, 1'), ('right', 'D-pad right', 'Right, 2'),
    ('walk', 'Walk (hold)', 'Left Alt'),
]
# Note: F (special 3 = △+○) and V (Rider Glide = □+✕) are on two inputs each, as in the game.

PAD_CONTROLS = [
    ('cross', '✕', 'a'), ('circle', '○', 'b'), ('square', '□', 'x'), ('triangle', '△', 'y'),
    ('l1', 'L1', 'leftshoulder'), ('r1', 'R1', 'rightshoulder'),
    ('l2', 'L2', 'lefttrigger'), ('r2', 'R2', 'righttrigger'),
    ('l3', 'L3', 'leftstick'), ('r3', 'R3', 'rightstick'),
    ('options', 'OPTIONS', 'start'), ('touchpad', 'Touchpad', 'touchpad'),
    ('up', 'D-pad up', 'dpup'), ('down', 'D-pad down', 'dpdown'),
    ('left', 'D-pad left', 'dpleft'), ('right', 'D-pad right', 'dpright'),
]
PAD_BUTTONS = [
    ('a', 'South (A / ✕)'), ('b', 'East (B / ○)'), ('x', 'West (X / □)'), ('y', 'North (Y / △)'),
    ('leftshoulder', 'Left bumper'), ('rightshoulder', 'Right bumper'),
    ('lefttrigger', 'Left trigger'), ('righttrigger', 'Right trigger'),
    ('leftstick', 'Left stick click'), ('rightstick', 'Right stick click'),
    ('start', 'Start / Options / Menu'), ('back', 'Back / Share / View'),
    ('touchpad', 'Touchpad click'), ('misc1', 'Misc (Share / Capture)'),
    ('dpup', 'D-pad up'), ('dpdown', 'D-pad down'), ('dpleft', 'D-pad left'), ('dpright', 'D-pad right'),
    ('paddle1', 'Paddle 1'), ('paddle2', 'Paddle 2'), ('paddle3', 'Paddle 3'), ('paddle4', 'Paddle 4'),
    ('', 'Not used'),
]

# ---- Display settings -----------------------------------------------------------------------
WINDOW_MODES = [('windowed', 'Windowed'), ('borderless', 'Borderless fullscreen'),
                ('exclusive', 'Exclusive fullscreen')]
RENDER_SIZES = [('720p', '1280 × 720 (faster)'), ('1080p', '1920 × 1080 (the game\'s own)'),
                ('1440p', '2560 × 1440 (sharper)'), ('4k', '3840 × 2160 (sharpest, needs a strong GPU)')]
FRAME_RATES = [('60', '60 FPS'), ('30', '30 FPS (the original)')]

DEFAULT_SETTINGS = {
    'base': '', 'update': '', 'data': str(Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
                                          / APP_ID),
    'display': {'mode': 'windowed', 'render': '1080p', 'fps': '60', 'area_filter': True, 'show_fps': False},
    'performance': {'low_core_cpu': False, 'gamemode': True, 'mangohud': False},
    'keys': {}, 'pad': {},
}


def checkout_defaults():
    """A developer checkout already set up as in the README (game/, out/, user/ beside port/): the
    launcher starts from it, its saves included; the dump folders come from game/'s links."""
    found = {}
    if not APPDIR and (REPO / 'user').is_dir() and (REPO / 'game').is_dir():
        found['data'] = str(REPO)
        eboot, libc = REPO / 'game' / 'eboot.bin', REPO / 'game' / 'sce_module' / 'libc.prx'
        try:
            if eboot.is_symlink():
                found['update'] = str(eboot.resolve().parent)
            if libc.is_symlink():
                found['base'] = str(libc.resolve().parent.parent)
        except OSError:
            pass
    return found


def config_path():
    return Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / APP_ID / 'settings.json'


def load_settings():
    settings = json.loads(json.dumps(DEFAULT_SETTINGS))
    settings.update(checkout_defaults())
    try:
        saved = json.loads(config_path().read_text())
    except (OSError, ValueError):
        saved = {}
    if isinstance(saved, dict):
        for key, value in saved.items():
            if isinstance(value, dict) and isinstance(settings.get(key), dict):
                settings[key].update(value)
            else:
                settings[key] = value
    return settings


def save_settings(settings):
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(settings, indent=2))
    tmp.replace(path)


# ---- The dump -------------------------------------------------------------------------------
def read_sfo(path):
    """param.sfo: {key: str or int}."""
    data = Path(path).read_bytes()
    if len(data) < 20 or data[:4] != b'\0PSF':
        raise SetupError('%s is not a param.sfo file' % path)
    keys, values, count = struct.unpack_from('<III', data, 8)
    out = {}
    for i in range(count):
        key_off, fmt, length, _, val_off = struct.unpack_from('<HHIII', data, 20 + i * 16)
        end = data.index(b'\0', keys + key_off)
        key = data[keys + key_off:end].decode('utf-8', 'replace')
        raw = data[values + val_off:values + val_off + length]
        out[key] = struct.unpack('<I', raw[:4])[0] if fmt == 0x404 else raw.rstrip(b'\0').decode('utf-8', 'replace')
    return out


def check_base(folder):
    """The base game dump (CUSA02716 v01.00): its eboot and param.sfo."""
    folder = Path(folder or '')
    if not folder.is_dir():
        raise SetupError('Choose the folder of your base game dump (%s).' % TITLE_ID)
    sfo = folder / 'sce_sys' / 'param.sfo'
    if not (folder / 'eboot.bin').is_file() or not sfo.is_file():
        raise SetupError('%s has no eboot.bin and sce_sys/param.sfo: not a game dump folder.' % folder)
    info = read_sfo(sfo)
    if info.get('TITLE_ID') != TITLE_ID:
        raise SetupError('That dump is %s; this port needs %s.' % (info.get('TITLE_ID', '?'), TITLE_ID))
    return info


def check_update(folder):
    """The v01.02 update dump."""
    folder = Path(folder or '')
    if not folder.is_dir():
        raise SetupError('Choose the folder of your v%s update dump.' % UPDATE_VERSION)
    sfo = folder / 'sce_sys' / 'param.sfo'
    if not (folder / 'eboot.bin').is_file() or not sfo.is_file():
        raise SetupError('%s has no eboot.bin and sce_sys/param.sfo: not an update dump folder.' % folder)
    info = read_sfo(sfo)
    version = str(info.get('APP_VER', ''))
    if info.get('TITLE_ID') != TITLE_ID or not version.startswith(UPDATE_VERSION):
        raise SetupError('That update is %s v%s; this port needs %s v%s.' %
                         (info.get('TITLE_ID', '?'), version or '?', TITLE_ID, UPDATE_VERSION))
    return info


def check_build():
    if not PROBE.is_file() or not GPU_LIB.is_file():
        raise SetupError('The port is not built yet: run port/build.sh first (see the README).')


# ---- Preparation ----------------------------------------------------------------------------
def stamp_of(base, update):
    h = hashlib.sha256()
    for folder in (base, update):
        eboot = Path(folder) / 'eboot.bin'
        st = eboot.stat()
        h.update(('%s|%d|%d|' % (eboot.resolve(), st.st_size, int(st.st_mtime))).encode())
    for script in ('prepare.py', 'link_libc.py', 'link_modules.py'):
        h.update((SCRIPTS / script).read_bytes())
    return h.hexdigest()


def prepared(data, base, update):
    data = Path(data)
    try:
        stamp = json.loads((data / 'out' / 'prepared.json').read_text())
    except (OSError, ValueError):
        return False
    try:
        return (stamp.get('stamp') == stamp_of(base, update) and (data / 'out' / 'boot-linked.bin').is_file()
                and (data / 'game' / 'eboot.bin').exists())
    except OSError:
        return False


def link_tree(source, dest, replace):
    """dest mirrors source with symbolic links (cp -rs; replace: cp -rsf)."""
    source = Path(source).resolve()
    for root, dirs, files in os.walk(source):
        rel = Path(root).relative_to(source)
        target = Path(dest) / rel
        target.mkdir(parents=True, exist_ok=True)
        for name in files:
            link = target / name
            if link.is_symlink() or link.exists():
                if not replace:
                    continue
                link.unlink()
            link.symlink_to(Path(root) / name)


class Runner:
    """Runs one command at a time; stop() ends it (and its children)."""

    def __init__(self):
        self.proc = None
        self.stopped = False
        self.lock = threading.Lock()

    def run(self, cmd, log=None, cwd=None, env=None):
        with self.lock:
            if self.stopped:
                return -1
            self.proc = subprocess.Popen([str(c) for c in cmd], cwd=cwd, env=env, stdout=log or subprocess.DEVNULL,
                                         stderr=subprocess.STDOUT, start_new_session=True)
        code = self.proc.wait()
        with self.lock:
            self.proc = None
        return code

    def stop(self):
        with self.lock:
            self.stopped = True
            proc = self.proc
        if proc and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
                proc.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except OSError:
                    pass


def prepare(data, base, update, runner, message, log):
    """The merged base + update view (symbolic links only) and the linked game image."""
    data = Path(data)
    game, out = data / 'game', data / 'out'
    message('Linking your base game and update (nothing is copied or changed)...')
    if game.exists() and not game.is_dir():
        raise SetupError('%s exists and is not a folder.' % game)
    game.mkdir(parents=True, exist_ok=True)
    link_tree(base, game, replace=False)
    link_tree(update, game, replace=True)
    out.mkdir(parents=True, exist_ok=True)
    python = sys.executable if APPDIR else (shutil.which('python3') or 'python3')
    for step, script in enumerate(('prepare.py', 'link_libc.py', 'link_modules.py'), 1):
        message('Preparing the game (%d of 3: %s)...' % (step, script))
        log.write(('\n$ %s %s %s --out %s\n' % (python, script, game, out)).encode())
        log.flush()
        code = runner.run([python, SCRIPTS / script, game, '--out', out], log=log, cwd=data)
        if code:
            raise SetupError('Preparing the game failed at %s (exit %d). See the Log page.' % (script, code))
    (out / 'prepared.json').write_text(json.dumps({'stamp': stamp_of(base, update), 'base': str(base),
                                                   'update': str(update), 'time': time.time()}, indent=2))
    message('Game prepared.')


# ---- Running --------------------------------------------------------------------------------
def write_ini(data, settings):
    """bbport.ini (BB_CONFIG): key./pad. bindings that differ from the defaults, and show_fps."""
    lines = ['# Written by the launcher (settings.json); edits here are replaced.']
    keys = settings.get('keys') or {}
    for name, _, default in KEY_CONTROLS:
        value = keys.get(name)
        if isinstance(value, list) and [v.strip() for v in default.split(',')] != value:
            lines.append('key.%s=%s' % (name, ', '.join(value)))
    pad = settings.get('pad') or {}
    for name, _, default in PAD_CONTROLS:
        value = pad.get(name)
        if isinstance(value, str) and value != default:
            lines.append('pad.%s=%s' % (name, value))
    lines.append('show_fps=%s' % ('1' if settings['display'].get('show_fps') else '0'))
    path = Path(data) / 'bbport.ini'
    path.write_text('\n'.join(lines) + '\n')
    return path


def game_env(data, settings):
    env = dict(os.environ)
    display, perf = settings['display'], settings['performance']
    env['BB_CONFIG'] = str(write_ini(data, settings))
    env['BB_FULLSCREEN'] = {'windowed': '0', 'borderless': '1', 'exclusive': 'exclusive'}.get(display.get('mode'), '0')
    env['KR_RES'] = display.get('render', '1080p')
    env['KR_FPS'] = str(display.get('fps', '60'))
    env['KR_DOWNSCALE'] = '1' if display.get('area_filter', True) else '0'
    env['KR_FLIP_WAIT_MARGIN_US'] = '1000' if perf.get('low_core_cpu') else '0'
    env['BB_FPS_PRINT'] = '1'
    if perf.get('mangohud'):
        env['MANGOHUD'] = '1'
    return env


def game_command(data, settings):
    data = Path(data)
    cmd = [PROBE, data / 'out' / 'boot-linked.bin', '--app0', data / 'game', '--user', data / 'user',
           '--timeout', '0']
    if settings['performance'].get('gamemode') and shutil.which('gamemoderun'):
        cmd = [shutil.which('gamemoderun')] + cmd
    return cmd
