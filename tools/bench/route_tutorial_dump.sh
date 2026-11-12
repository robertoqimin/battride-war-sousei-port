#!/usr/bin/env bash
# Captures each tutorial page (before pressing OPTIONS) and then the pause menu.
P=$1; T=$2; : > "$P"
press() { echo "$1" > "$P"; sleep 0.25; : > "$P"; }
shot() { touch "$T"; sleep 1.0; }
sleep 6
for i in $(seq 1 20); do sleep 1; press circle; sleep 2.5; done
for i in 1 2 3 4; do sleep 1; shot; press options; sleep 2; done
sleep 3; shot
press options; sleep 2; shot          # pause menu
press down; sleep 0.8; shot
press down; sleep 0.8; shot
press down; sleep 0.8; shot
