#!/usr/bin/env bash
P=$1; T=$2; : > "$P"
press() { echo "$1" > "$P"; sleep 0.25; : > "$P"; sleep 0.45; }
shot() { touch "$T"; sleep 1.2; }
sleep 6
for i in $(seq 1 20); do sleep 1; press circle; sleep 2.5; done
for i in 1 2 3; do sleep 1; press options; sleep 2; done
sleep 8
press options; sleep 2
press down; press down; press circle; sleep 1.5; press circle; sleep 2; shot
for k in 1 2 3 4 5 6; do press down; press down; press down; shot; done
press cross; sleep 1; press down; press circle; sleep 2; shot
