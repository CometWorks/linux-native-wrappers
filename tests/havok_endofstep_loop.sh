#!/usr/bin/env bash
# Closed-loop soak for SE1-0015: cycles the havok_endofstep_test scenarios with fresh seeds
# for a fixed wall-clock budget and stops at the first violation or crash.
# usage: tests/havok_endofstep_loop.sh BUILD_DIR HAVOK_DLL [MINUTES] [STEPS_PER_RUN] [HARNESS_OPTIONS...]
#   e.g. pass --fix to soak the Bugfixes plugin workaround
set -u
build=$1; dll=$2; minutes=${3:-60}; steps=${4:-200000}; shift $(( $# < 4 ? $# : 4 ))
test_bin="$build/havok_endofstep_test"; sidecar="$build/havok-endofstep-loop-Havok.dll"
scenarios=(field mixed dispose-first deferred-remove detach-toggle baseline)
deadline=$(( $(date +%s) + minutes * 60 )); seed=${SEED:-100}; runs=0; total_steps=0
echo "start $(date -Is) minutes=$minutes steps/run=$steps options=$*"
while [ "$(date +%s)" -lt "$deadline" ]; do
  for s in "${scenarios[@]}"; do
    [ "$(date +%s)" -lt "$deadline" ] || break
    seed=$((seed + 1)); runs=$((runs + 1))
    out=$(nice -n 19 "$test_bin" "$dll" "$sidecar" "$s" --seed "$seed" --steps "$steps" --report 0 "$@" 2>&1); rc=$?
    total_steps=$((total_steps + steps))
    if [ $rc -ne 0 ]; then
      echo "FAIL $(date -Is) scenario=$s seed=$seed rc=$rc"; echo "$out" | tail -20
      echo "summary: runs=$runs steps=$total_steps result=FAIL"; exit 1
    fi
    echo "ok   $(date -Is) run=$runs scenario=$s seed=$seed $(echo "$out" | grep -o 'collisions +[0-9]* -[0-9]*.*' | tail -1)"
  done
done
echo "summary: runs=$runs steps=$total_steps result=CLEAN $(date -Is)"
