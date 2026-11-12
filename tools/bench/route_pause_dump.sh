#!/usr/bin/env bash
P=$1; T=$2; : > "$P"
press() { echo "$1" > "$P"; sleep 0.25; : > "$P"; }
shot() { touch "$T"; sleep 1.2; }
sleep 6
for i in $(seq 1 20); do sleep 1; press circle; sleep 2.5; done
for i in 1 2 3; do sleep 1; press options; sleep 2; done
sleep 8
press options; sleep 2
press circle; sleep 2; shot              # 必殺技確認
press right; sleep 1; shot
press cross; sleep 1.5
press down; sleep 0.8; press circle; sleep 2; shot   # スキル確認
press cross; sleep 1.5
press down; sleep 0.8; press circle; sleep 2; shot   # ゲーム設定
press down; sleep 0.8; press down; sleep 0.8; shot
press down; sleep 0.8; press down; sleep 0.8; shot
press circle; sleep 2; shot
