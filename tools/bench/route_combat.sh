# Combat route for regression checks (run_route.sh): menu, Story Mode, then about a minute of
# automated combat. Made for the pre-clear save (use RR_USER with a copy of it).
w 6
for i in $(seq 1 20); do w 1; press circle; w 2.5; done
for i in 1 2 3; do w 1; press options; w 2; done
t0=$(date +%s)
k=0
while [ $(( $(date +%s) - t0 )) -lt 60 ]; do
  k=$((k+1))
  for dir in "ly=0" "lx=255" "ly=255" "lx=0"; do
    echo "$dir" > "$P"; sleep 0.8
    echo "square $dir" > "$P"; sleep 0.12; echo "$dir" > "$P"; sleep 0.1
    echo "square $dir" > "$P"; sleep 0.12; echo "$dir" > "$P"; sleep 0.1
    echo "square" > "$P"; sleep 0.12; : > "$P"; sleep 0.1
    echo "triangle" > "$P"; sleep 0.15; : > "$P"; sleep 0.3
    echo "circle" > "$P"; sleep 0.15; : > "$P"; sleep 0.3
  done
  echo "r2" > "$P"; sleep 0.2; : > "$P"; sleep 0.3
  press circle
  [ $((k % 3)) -eq 0 ] && shot k$k
done
