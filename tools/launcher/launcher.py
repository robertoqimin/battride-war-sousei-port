#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Battride War Genesis launcher: points the port at your dump, prepares it once and starts the game.

Based on the launcher of battride-war-recomp (same layout: Game, Settings, Keyboard, Controller, Log).
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402

LOG_LIMIT = 2_000_000
APP_TITLE = 'Battride War Genesis'


def host_env():
    """The environment for host programs (the file manager): without the AppImage's Python and GTK."""
    env = dict(os.environ)
    for key in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONPLATLIBDIR', 'PYTHONDONTWRITEBYTECODE', 'GI_TYPELIB_PATH',
                'GSETTINGS_SCHEMA_DIR', 'GSETTINGS_BACKEND', 'GIO_USE_VFS', 'GIO_MODULE_DIR', 'PORT_APPDIR'):
        env.pop(key, None)
    return env


def open_folder(path):
    try:
        subprocess.Popen(['xdg-open', str(path)], start_new_session=True, env=host_env(),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def cli(args):
    """--check / --prepare without the window (scripts, tests)."""
    settings = core.load_settings()
    base = args.base or settings.get('base')
    update = args.update or settings.get('update')
    data = Path(args.data_dir or settings.get('data')).expanduser()
    core.check_build()
    base_info = core.check_base(base)
    update_info = core.check_update(update)
    print('Base game: %s v%s (%s)' % (base_info.get('TITLE_ID'), base_info.get('APP_VER'), base))
    print('Update:    %s v%s (%s)' % (update_info.get('TITLE_ID'), update_info.get('APP_VER'), update))
    print('Data:      %s (%s)' % (data, 'prepared' if core.prepared(data, base, update) else 'not prepared'))
    if args.prepare and not core.prepared(data, base, update):
        data.mkdir(parents=True, exist_ok=True)
        with (data / 'prepare.log').open('wb') as log:
            core.prepare(data, base, update, core.Runner(), print, log)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true', help='check the dump and the build, then exit')
    ap.add_argument('--prepare', action='store_true', help='check, prepare the game if needed, then exit')
    ap.add_argument('--base', help='base game dump folder (overrides the saved one)')
    ap.add_argument('--update', help='update dump folder (overrides the saved one)')
    ap.add_argument('--data-dir', help='data folder (overrides the saved one)')
    ap.add_argument('--screenshot', type=Path, help=argparse.SUPPRESS)
    ap.add_argument('--page', default='game', help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.check or args.prepare:
        cli(args)
        return

    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Adw', '1')
    from gi.repository import Adw, Gdk, Gio, GLib, Gtk, Pango

    settings = core.load_settings()

    # GDK key names that differ from SDL's (bindings.c resolves SDL names).
    SDL_KEYS = {'space': 'Space', 'Return': 'Return', 'KP_Enter': 'Keypad Enter', 'BackSpace': 'Backspace',
                'Escape': 'Escape', 'Tab': 'Tab', 'ISO_Left_Tab': 'Tab', 'Shift_L': 'Left Shift',
                'Shift_R': 'Right Shift', 'Control_L': 'Left Ctrl', 'Control_R': 'Right Ctrl',
                'Alt_L': 'Left Alt', 'Alt_R': 'Right Alt', 'Super_L': 'Left GUI', 'Super_R': 'Right GUI',
                'Up': 'Up', 'Down': 'Down', 'Left': 'Left', 'Right': 'Right', 'Insert': 'Insert',
                'Delete': 'Delete', 'Home': 'Home', 'End': 'End', 'Page_Up': 'PageUp', 'Page_Down': 'PageDown',
                'Caps_Lock': 'CapsLock', 'KP_Add': 'Keypad +', 'KP_Subtract': 'Keypad -',
                'KP_Multiply': 'Keypad *', 'KP_Divide': 'Keypad /', 'KP_Decimal': 'Keypad .'}
    SDL_MOUSE = {1: 'Mouse Left', 2: 'Mouse Middle', 3: 'Mouse Right', 8: 'Mouse X1', 9: 'Mouse X2'}

    def sdl_key_name(keyval, keycode, group):
        found, plain, *_ = Gdk.Display.get_default().translate_key(keycode, 0, group)
        if found:
            keyval = plain
        name = Gdk.keyval_name(keyval) or ''
        if name in SDL_KEYS:
            return SDL_KEYS[name]
        if name.startswith('KP_') and name[3:].isdigit():
            return 'Keypad ' + name[3:]
        if len(name) <= 3 and name[:1] == 'F' and name[1:].isdigit():
            return name
        code = Gdk.keyval_to_unicode(keyval)
        char = chr(code).upper() if code else ''
        return char if char.isprintable() and char not in ' ,=' else None

    def flat_button(icon, tooltip, handler):
        button = Gtk.Button(icon_name=icon, tooltip_text=tooltip, valign=Gtk.Align.CENTER)
        button.add_css_class('flat')
        button.connect('clicked', handler)
        return button

    def combo_row(title, subtitle, choices, current, default):
        row = Adw.ComboRow(title=title, model=Gtk.StringList.new([label for _, label in choices]))
        if subtitle:
            row.set_subtitle(subtitle)
        ids = [key for key, _ in choices]
        row.set_selected(ids.index(current) if current in ids else ids.index(default) if default in ids else 0)
        row.value = lambda: ids[row.get_selected()]
        return row

    def switch_row(title, subtitle, active):
        row = Adw.ActionRow(title=title, subtitle=subtitle or '')
        switch = Gtk.Switch(active=bool(active), valign=Gtk.Align.CENTER)
        row.add_suffix(switch)
        row.set_activatable_widget(switch)
        row.switch = switch
        return row

    class Window:
        def __init__(self, app):
            self.closed = False
            self.runner = None
            self.busy = False
            self.playing = False
            self.log_dir = None
            self.log_offsets = {}
            self.log_current = None
            self.lockable = []
            self.win = Adw.ApplicationWindow(application=app, title=APP_TITLE)
            self.win.set_default_size(880, 780)
            self.win.connect('close-request', self.close)

            header = Adw.HeaderBar()
            self.stack = Adw.ViewStack()
            title = Adw.ViewSwitcherTitle(stack=self.stack, title=APP_TITLE)
            header.set_title_widget(title)
            self.launch = Gtk.Button()
            self.launch.connect('clicked', self.on_launch)
            header.pack_end(self.launch)
            header.pack_start(flat_button('help-about-symbolic', 'Credits & licenses', self.show_credits))
            bottom = Adw.ViewSwitcherBar(stack=self.stack)
            title.connect('notify::title-visible', lambda t, _: bottom.set_reveal(t.get_title_visible()))

            self.stack.add_titled(self.build_game_page(), 'game', 'Game').set_icon_name('applications-games-symbolic')
            self.stack.add_titled(self.build_settings_page(), 'settings', 'Settings').set_icon_name(
                'preferences-system-symbolic')
            self.stack.add_titled(self.build_keys_page(), 'keyboard', 'Keyboard').set_icon_name(
                'input-keyboard-symbolic')
            self.stack.add_titled(self.build_pad_page(), 'controller', 'Controller').set_icon_name(
                'input-gaming-symbolic')
            self.stack.add_titled(self.build_log_page(), 'log', 'Log').set_icon_name('utilities-terminal-symbolic')

            status = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, margin_start=18, margin_end=18,
                             margin_top=10, margin_bottom=12)
            self.progress = Gtk.ProgressBar(visible=False)
            self.status = Gtk.Label(xalign=0, wrap=True, selectable=True,
                                    label='Ready. Needs a Vulkan GPU and a dump of your own copy of the game.')
            self.stats = Gtk.Label(xalign=0, visible=False)
            self.stats.add_css_class('dim-label')
            self.stats.add_css_class('numeric')
            for widget in (self.progress, self.status, self.stats):
                status.append(widget)

            self.toasts = Adw.ToastOverlay(child=self.stack, vexpand=True)
            content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            for widget in (header, self.toasts, Gtk.Separator(), status, bottom):
                content.append(widget)
            self.win.set_content(content)
            self.stack.set_visible_child_name(args.page)
            self.update_launch()
            GLib.timeout_add(1000, self.tick)
            GLib.timeout_add(300, self.follow_logs)
            self.win.present()

        # --- Game page -------------------------------------------------------------------
        def build_game_page(self):
            page = Adw.PreferencesPage()
            page.add(Adw.PreferencesGroup(
                title='Kamen Rider: Battride War Genesis',
                description='Native PC port  •  PS4 %s v%s\n\n'
                            'Choose the folders of your own dump (base game and the v%s update) and press Play. '
                            'The first launch links them into the data folder and prepares the game (a few '
                            'seconds, once); nothing is copied or changed. Saves and logs stay in the data '
                            'folder.' % (core.TITLE_ID, core.UPDATE_VERSION, core.UPDATE_VERSION)))
            game = Adw.PreferencesGroup(title='Game')
            self.base_row = Adw.ActionRow(title='Base game (%s v01.00)' % core.TITLE_ID)
            self.base_row.add_suffix(flat_button('folder-open-symbolic', 'Choose the base game folder',
                                                 lambda _: self.choose_folder('base', 'Choose the base game folder')))
            self.update_row = Adw.ActionRow(title='Update (v%s)' % core.UPDATE_VERSION)
            self.update_row.add_suffix(flat_button('folder-open-symbolic', 'Choose the update folder',
                                                   lambda _: self.choose_folder('update', 'Choose the update folder')))
            self.data_row = Adw.ActionRow(title='Data folder')
            self.data_row.add_suffix(flat_button('folder-open-symbolic', 'Choose the data folder',
                                                 lambda _: self.choose_folder('data', 'Choose the data folder')))
            self.data_row.add_suffix(flat_button('system-file-manager-symbolic', 'Open in the file manager',
                                                 lambda _: open_folder(settings['data'])))
            for row in (self.base_row, self.update_row, self.data_row):
                game.add(row)
            page.add(game)
            page.add(Adw.PreferencesGroup(
                title='Playing',
                description='A controller (PlayStation, Xbox, most others) or keyboard and mouse: click the game '
                            'window so the mouse turns the camera. This is a Japanese release: ○ confirms and ✕ '
                            'goes back. Insert (or L3+R3) opens the port\'s menu in game.'))
            self.lockable.append(game)
            self.refresh_paths()
            return page

        def refresh_paths(self):
            def describe(key, check):
                path = settings.get(key) or ''
                if not path:
                    return 'Not chosen'
                try:
                    info = check(path)
                    return '%s  —  %s v%s' % (path, info.get('TITLE_ID'), info.get('APP_VER'))
                except core.SetupError as e:
                    return '%s  —  %s' % (path, e)
                except OSError as e:
                    return '%s  —  %s' % (path, e)
            self.base_row.set_subtitle(GLib.markup_escape_text(describe('base', core.check_base)))
            self.update_row.set_subtitle(GLib.markup_escape_text(describe('update', core.check_update)))
            self.data_row.set_subtitle(GLib.markup_escape_text(settings['data']))

        def choose_folder(self, key, title):
            dialog = Gtk.FileChooserNative(title=title, transient_for=self.win,
                                           action=Gtk.FileChooserAction.SELECT_FOLDER, modal=True)
            current = settings.get(key)
            if current and Path(current).exists():
                dialog.set_current_folder(Gio.File.new_for_path(str(Path(current).parent)))

            def done(d, response):
                if response == Gtk.ResponseType.ACCEPT and d.get_file() and d.get_file().get_path():
                    settings[key] = d.get_file().get_path()
                    self.save()
                    self.refresh_paths()
                    self.update_launch()
                d.destroy()
            dialog.connect('response', done)
            self.dialog = dialog
            dialog.show()

        # --- Settings page ---------------------------------------------------------------
        def build_settings_page(self):
            page = Adw.PreferencesPage()
            display = settings['display']
            screen = Adw.PreferencesGroup(title='Display',
                                          description='The picture keeps 16:9, with black bars on other shapes. '
                                                      'Changes apply the next time the game starts.')
            self.mode = combo_row('Window', None, core.WINDOW_MODES, display.get('mode'), 'windowed')
            self.render = combo_row('Render resolution', 'Above your screen\'s size it works as supersampling',
                                    core.RENDER_SIZES, display.get('render'), '1080p')
            self.fps = combo_row('Frame rate', 'The game speed is the same either way', core.FRAME_RATES,
                                 str(display.get('fps')), '60')
            self.area = switch_row('Sharp downscaling', 'An area filter when the render resolution is larger '
                                   'than the window (off: bilinear)', display.get('area_filter', True))
            self.show_fps = switch_row('Show the frame rate', 'In a corner of the game window',
                                       display.get('show_fps'))
            for row in (self.mode, self.render, self.fps, self.area, self.show_fps):
                screen.add(row)
            page.add(screen)
            perf_saved = settings['performance']
            perf = Adw.PreferencesGroup(title='Performance')
            self.low_cpu = switch_row('CPU with few cores', 'Frees about a third of a core while the game waits '
                                      'for the GPU; can cost an occasional dropped frame in heavy fights',
                                      perf_saved.get('low_core_cpu'))
            self.gamemode_path = __import__('shutil').which('gamemoderun')
            self.gamemode = switch_row('GameMode', 'Raises CPU clocks and priority while playing (Feral GameMode)'
                                       if self.gamemode_path else 'Install the gamemode package to use it',
                                       perf_saved.get('gamemode', True) and self.gamemode_path)
            self.gamemode.set_sensitive(bool(self.gamemode_path))
            self.hud = switch_row('MangoHud overlay', 'Frame rate and frame times, if MangoHud is installed',
                                  perf_saved.get('mangohud'))
            for row in (self.low_cpu, self.gamemode, self.hud):
                perf.add(row)
            page.add(perf)
            for row in (self.mode, self.render, self.fps):
                row.connect('notify::selected', lambda *_: self.settings_changed())
            for row in (self.area, self.show_fps, self.low_cpu, self.gamemode, self.hud):
                row.switch.connect('notify::active', lambda *_: self.settings_changed())
            self.lockable += [screen, perf]
            return page

        def settings_changed(self):
            settings['display'].update({'mode': self.mode.value(), 'render': self.render.value(),
                                        'fps': self.fps.value(), 'area_filter': self.area.switch.get_active(),
                                        'show_fps': self.show_fps.switch.get_active()})
            settings['performance'].update({'low_core_cpu': self.low_cpu.switch.get_active(),
                                            'gamemode': self.gamemode.switch.get_active(),
                                            'mangohud': self.hud.switch.get_active()})
            self.save()

        # --- Keyboard page ---------------------------------------------------------------
        def build_keys_page(self):
            page = Adw.PreferencesPage()
            stored = settings.get('keys') if isinstance(settings.get('keys'), dict) else {}
            self.key_defaults = {n: [k.strip() for k in d.split(',') if k.strip()] for n, _, d in core.KEY_CONTROLS}
            self.keys = {n: list(stored[n]) if isinstance(stored.get(n), list) else list(self.key_defaults[n])
                         for n, _, _ in core.KEY_CONTROLS}
            self.key_labels = {n: label for n, label, _ in core.KEY_CONTROLS}
            self.key_rows = {}
            intro = Adw.PreferencesGroup(title='Keyboard and mouse',
                                         description='Up to 4 keys or mouse buttons per action. F (special 3) and V '
                                                     '(Rider Glide) press two buttons at once, as the game asks. '
                                                     'Changes apply the next time the game starts.')
            reset = Gtk.Button(label='Reset to defaults', valign=Gtk.Align.CENTER)
            reset.add_css_class('flat')
            reset.connect('clicked', self.reset_keys)
            intro.set_header_suffix(reset)
            page.add(intro)
            for title, prefixes in (('Movement and camera', ('move_', 'look_')), ('Buttons', ('',))):
                group = Adw.PreferencesGroup(title=title)
                for name, label, _ in core.KEY_CONTROLS:
                    if name in self.key_rows or not name.startswith(prefixes):
                        continue
                    row = Adw.ActionRow(title=label)
                    row.add_suffix(flat_button('document-edit-symbolic', 'Replace with a new key or mouse button',
                                               lambda _, n=name: self.capture(n, True)))
                    row.add_suffix(flat_button('list-add-symbolic', 'Add another key',
                                               lambda _, n=name: self.capture(n, False)))
                    row.add_suffix(flat_button('edit-clear-symbolic', 'Clear',
                                               lambda _, n=name: self.set_keys(n, [])))
                    self.key_rows[name] = row
                    self.refresh_keys(name)
                    group.add(row)
                page.add(group)
            self.lockable.append(page)
            return page

        def refresh_keys(self, name):
            self.key_rows[name].set_subtitle(GLib.markup_escape_text(', '.join(self.keys[name]) or 'Not set'))

        def set_keys(self, name, keys):
            self.keys[name] = keys
            self.refresh_keys(name)
            settings['keys'] = self.keys
            self.save()

        def reset_keys(self, _):
            for name in self.keys:
                self.keys[name] = list(self.key_defaults[name])
                self.refresh_keys(name)
            settings['keys'] = {}
            self.save()
            self.toast('Keyboard reset to the defaults')

        def capture(self, name, replace):
            cap = Gtk.Window(title='Press a key', transient_for=self.win, modal=True, resizable=False,
                             default_width=460)
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin_start=18, margin_end=18,
                          margin_top=18, margin_bottom=18)
            area = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, height_request=140)
            area.add_css_class('card')
            heading = Gtk.Label(label=self.key_labels[name], vexpand=True, valign=Gtk.Align.END)
            heading.add_css_class('title-3')
            hint = Gtk.Label(label='Press a key, or click here with a mouse button', vexpand=True,
                             valign=Gtk.Align.START)
            hint.add_css_class('dim-label')
            area.append(heading)
            area.append(hint)
            cancel = Gtk.Button(label='Cancel', halign=Gtk.Align.END)
            cancel.connect('clicked', lambda _: cap.destroy())
            box.append(area)
            box.append(cancel)
            cap.set_child(box)

            def chosen(key):
                keys = self.keys[name]
                if replace:
                    keys = [key]
                elif key not in keys and len(keys) < 4:
                    keys = keys + [key]
                self.set_keys(name, keys)
                cap.destroy()

            def on_key(ctrl, keyval, keycode, _state):
                key = sdl_key_name(keyval, keycode, ctrl.get_group())
                if key:
                    chosen(key)
                return True

            def on_click(gesture, *_):
                button = gesture.get_current_button()
                if button in SDL_MOUSE:
                    chosen(SDL_MOUSE[button])

            keys = Gtk.EventControllerKey()
            keys.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
            keys.connect('key-pressed', on_key)
            cap.add_controller(keys)
            click = Gtk.GestureClick(button=0)
            click.connect('pressed', on_click)
            area.add_controller(click)
            cap.present()

        # --- Controller page -------------------------------------------------------------
        def build_pad_page(self):
            page = Adw.PreferencesPage()
            stored = settings.get('pad') if isinstance(settings.get('pad'), dict) else {}
            group = Adw.PreferencesGroup(
                title='Controller',
                description='PlayStation, Xbox, Switch and most other controllers work through SDL. Buttons '
                            'follow their position: the bottom face button is ✕ on every layout. The sticks '
                            'stay as they are. Changes apply the next time the game starts.')
            reset = Gtk.Button(label='Reset to defaults', valign=Gtk.Align.CENTER)
            reset.add_css_class('flat')
            reset.connect('clicked', self.reset_pad)
            group.set_header_suffix(reset)
            self.pad_rows = {}
            for name, label, default in core.PAD_CONTROLS:
                row = combo_row(label, None, core.PAD_BUTTONS, stored.get(name, default), default)
                row.connect('notify::selected', lambda *_: self.pad_changed())
                self.pad_rows[name] = row
                group.add(row)
            page.add(group)
            self.lockable.append(page)
            return page

        def pad_changed(self):
            settings['pad'] = {name: row.value() for name, row in self.pad_rows.items()}
            self.save()

        def reset_pad(self, _):
            ids = [key for key, _ in core.PAD_BUTTONS]
            for name, _, default in core.PAD_CONTROLS:
                self.pad_rows[name].set_selected(ids.index(default))
            settings['pad'] = {}
            self.save()
            self.toast('Controller reset to the defaults')

        # --- Log page --------------------------------------------------------------------
        def build_log_page(self):
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            self.log_view = Gtk.TextView(editable=False, monospace=True, cursor_visible=False,
                                         wrap_mode=Gtk.WrapMode.WORD_CHAR, top_margin=10, bottom_margin=10,
                                         left_margin=12, right_margin=12)
            self.log_scroll = Gtk.ScrolledWindow(vexpand=True, child=self.log_view)
            box.append(self.log_scroll)
            bar = Gtk.ActionBar()
            self.log_place = Gtk.Label(xalign=0, ellipsize=Pango.EllipsizeMode.MIDDLE)
            self.log_place.add_css_class('dim-label')
            bar.pack_start(self.log_place)
            folder = Gtk.Button(label='Open logs folder')
            folder.connect('clicked', lambda _: open_folder(self.log_dir or Path(settings['data']) / 'logs'))
            self.log_copy = Gtk.Button(label='Copy')
            self.log_copy.connect('clicked', lambda _: (self.win.get_clipboard().set(self.log_text()),
                                                        self.toast('Log copied')))
            for button in (self.log_copy, folder):
                bar.pack_end(button)
            box.append(bar)
            self.log_view.get_buffer().connect('changed', lambda b: self.log_buttons())
            self.log_buttons()
            return box

        def log_text(self):
            buffer = self.log_view.get_buffer()
            return buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), False)

        def log_buttons(self):
            self.log_copy.set_sensitive(self.log_view.get_buffer().get_char_count() > 0)
            self.log_place.set_text(str(self.log_dir) if self.log_dir else 'Preparation and game output appear '
                                                                           'here while they run')

        def append_log(self, text):
            buffer = self.log_view.get_buffer()
            adj = self.log_scroll.get_vadjustment()
            at_end = adj.get_value() >= adj.get_upper() - adj.get_page_size() - 40
            buffer.insert(buffer.get_end_iter(), text)
            extra = buffer.get_char_count() - LOG_LIMIT
            if extra > 0:
                start = buffer.get_start_iter()
                cut = buffer.get_iter_at_offset(extra)
                cut.forward_line()
                buffer.delete(start, cut)
            if at_end:
                GLib.idle_add(lambda: adj.set_value(adj.get_upper() - adj.get_page_size()) and False)

        def follow_logs(self):
            if self.closed:
                return False
            if not self.log_dir:
                return True
            for name in ('prepare.log', 'game.log'):
                path = self.log_dir / name
                offset = self.log_offsets.get(name, 0)
                try:
                    with path.open('rb') as f:
                        f.seek(offset)
                        chunk = f.read(512 * 1024)
                except OSError:
                    continue
                end = chunk.rfind(b'\n') + 1 if len(chunk) < 512 * 1024 else len(chunk)
                if not end:
                    continue
                self.log_offsets[name] = offset + end
                if self.log_current != name:
                    self.log_current = name
                    self.append_log('\n── %s ──\n' % name)
                self.append_log(chunk[:end].decode('utf-8', errors='replace'))
            return True

        # --- Launch ----------------------------------------------------------------------
        def ready(self):
            try:
                return core.prepared(settings['data'], settings['base'], settings['update'])
            except Exception:
                return False

        def update_launch(self):
            for cls in ('suggested-action', 'destructive-action'):
                self.launch.remove_css_class(cls)
            if self.busy:
                self.launch.set_label('Stop')
                self.launch.add_css_class('destructive-action')
            else:
                self.launch.set_label('Play' if self.ready() else 'Prepare & Play')
                self.launch.add_css_class('suggested-action')

        def toast(self, text):
            self.toasts.add_toast(Adw.Toast(title=GLib.markup_escape_text(text), timeout=3))

        def message(self, text):
            def apply():
                if not self.closed:
                    self.status.set_text(text)
                    self.append_log('» ' + text + '\n')
                return False
            GLib.idle_add(apply)

        def set_busy(self, busy):
            def apply():
                if self.closed:
                    return False
                self.busy = busy
                for widget in self.lockable:
                    widget.set_sensitive(not busy)
                self.progress.set_visible(busy and not self.playing)
                self.update_launch()
                return False
            GLib.idle_add(apply)

        def on_launch(self, _):
            if self.busy:
                if self.runner:
                    self.message('Stopping...')
                    threading.Thread(target=self.runner.stop, daemon=True).start()
            else:
                self.begin()

        def begin(self):
            try:
                core.check_build()
                core.check_base(settings.get('base'))
                core.check_update(settings.get('update'))
                data = Path(settings['data']).expanduser()
                data.mkdir(parents=True, exist_ok=True)
            except (core.SetupError, OSError) as e:
                self.status.set_text(str(e))
                self.stack.set_visible_child_name('game')
                return
            self.runner = core.Runner()
            self.log_dir = data / 'logs' / (time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
            self.log_dir.mkdir(parents=True, exist_ok=False)
            self.log_offsets, self.log_current = {}, None
            self.log_view.get_buffer().set_text('')
            snapshot = core.json.loads(core.json.dumps(settings))
            log_dir, runner = self.log_dir, self.runner
            self.busy = True
            self.set_busy(True)

            def worker():
                try:
                    if not core.prepared(data, snapshot['base'], snapshot['update']):
                        with (log_dir / 'prepare.log').open('wb') as log:
                            core.prepare(data, snapshot['base'], snapshot['update'], runner, self.message, log)
                    (data / 'user').mkdir(exist_ok=True)
                    self.playing = True
                    self.message('Starting the game. Logs: %s' % log_dir)
                    with (log_dir / 'game.log').open('wb') as log:
                        code = runner.run(core.game_command(data, snapshot), log=log, cwd=data,
                                          env=core.game_env(data, snapshot))
                    self.message('Game closed.' if code in (0, -15, 143) else
                                 'The game stopped (exit %d). The Log page shows why; logs: %s' % (code, log_dir))
                except (OSError, core.SetupError, ValueError) as e:
                    self.message('%s\nLogs: %s' % (e, log_dir))
                finally:
                    self.playing = False
                    self.set_busy(False)
            threading.Thread(target=worker, daemon=False).start()

        def save(self):
            try:
                core.save_settings(settings)
            except OSError as e:
                self.status.set_text('Could not save the settings: %s' % e)

        def show_credits(self, _):
            dialog = Gtk.Window(title='Credits & licenses', transient_for=self.win, modal=True,
                                default_width=700, default_height=560)
            dialog.set_titlebar(Adw.HeaderBar())
            view = Gtk.TextView(editable=False, cursor_visible=False, wrap_mode=Gtk.WrapMode.WORD,
                                top_margin=12, bottom_margin=12, left_margin=16, right_margin=16)
            try:
                text = core.CREDITS.read_text()
            except OSError:
                text = 'CREDITS.md is missing.'
            text = re.sub(r'\*\*(.+?)\*\*', r'\1', text)
            text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
            text = re.sub(r'^#+ ', '', text, flags=re.M)
            text += '\nSource code: https://github.com/robertoqimin/battride-war-sousei-port\n'
            view.get_buffer().set_text(text)
            dialog.set_child(Gtk.ScrolledWindow(child=view, vexpand=True))
            dialog.present()

        def close(self, *_):
            self.closed = True
            if self.runner:
                self.runner.stop()
            return False

        def tick(self):
            if self.closed:
                return False
            if self.busy and not self.playing:
                self.progress.pulse()
            self.progress.set_visible(self.busy and not self.playing)
            if self.playing and self.log_dir:
                try:
                    with (self.log_dir / 'game.log').open('rb') as f:
                        f.seek(0, 2)
                        size = f.tell()
                        f.seek(max(0, size - 65536))
                        tail = f.read().decode(errors='replace')
                    rates = re.findall(r'Present rate: ([0-9.]+) FPS', tail)
                    if rates:
                        self.stats.set_text('Frame rate: %s FPS' % rates[-1])
                        self.stats.set_visible(True)
                    if 'STOP:' in tail or 'Fault (signal' in tail or 'Guest fault' in tail:
                        self.status.set_text('The game hit an error. The Log page shows it; logs: %s' % self.log_dir)
                except OSError:
                    pass
            else:
                self.stats.set_visible(False)
            return True

    def shot(ui):
        win = ui.win
        paintable = Gtk.WidgetPaintable.new(win)
        snapshot = Gtk.Snapshot()
        paintable.snapshot(snapshot, win.get_width(), win.get_height())
        node = snapshot.to_node()
        if node:
            win.get_renderer().render_texture(node, None).save_to_png(str(args.screenshot))
        ui.close()
        win.get_application().quit()
        return False

    app = Adw.Application(application_id='io.github.robertoqimin.BattrideWarSouseiPort',
                          flags=Gio.ApplicationFlags.NON_UNIQUE)

    def activate(app):
        ui = Window(app)
        if args.screenshot:
            GLib.timeout_add(900, shot, ui)
    app.connect('activate', activate)
    app.run([sys.argv[0]])


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, core.SetupError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
