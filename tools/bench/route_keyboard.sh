#!/usr/bin/env bash
# Keyboard-only route (key: tokens through the bindings): Enter through menus, Escape for the
# tutorial pages, then each combat key once, with a frame dumped after some of them.
P=$1; T=$2; : > "$P"
key() { echo "MARK $1 $(date +%s.%N)" >> "$P.marks"; echo "key:$1" > "$P"; sleep 0.3; : > "$P"; sleep 0.5; }
hold() { echo "key:$1" > "$P"; sleep $2; : > "$P"; }
shot() { touch "$T"; sleep 1.0; }
sleep 6
for i in $(seq 1 20); do sleep 1; key Return; sleep 2.3; done
for i in 1 2 3; do sleep 1; key Escape; sleep 1.7; done
sleep 4
hold W 1.5; shot
for k in Mouse_Left J Mouse_Left Mouse_Right I E L F V Space K Left_Shift Tab R Q C Left_Ctrl G Mouse_Middle Mouse_X1 Mouse_X2 Up Down 1 2; do key $k; done
key Mouse_Left; key Mouse_Left; key F; sleep 0.2; shot
hold D 1.0; key R; sleep 0.6; shot
sleep 2; key Escape; sleep 1.5; shot; key Backspace; sleep 1.5; shot
