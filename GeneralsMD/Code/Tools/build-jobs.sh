#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
# Sourced by the POSIX builder and FFmpeg helper; no work occurs when loaded.

zh_jobs_integer() {
  local value=$1
  case "$value" in ''|*[!0-9]*) return 1 ;; esac
  while [ "${value#0}" != "$value" ]; do value=${value#0}; done
  [ -n "$value" ] && [ "${#value}" -le 10 ] || return 1
  [ "$value" -le 2147483647 ] || return 1
  printf '%s\n' "$value"
}

zh_auto_build_jobs() {
  local cpus memory_kib= memory_jobs=2 limit
  cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
  cpus=$(zh_jobs_integer "$cpus") || cpus=2
  [ "$cpus" -le 8 ] || cpus=8
  case "$(uname -s)" in
    Darwin)
      memory_kib=$(sysctl -n hw.memsize 2>/dev/null | awk '/^[0-9]+$/ {printf "%.0f\n", $1/1024}' || true)
      ;;
    Linux)
      memory_kib=$(awk '/^MemTotal:/ {print $2; exit}' /proc/meminfo 2>/dev/null || true)
      # A container may expose host memory while imposing a much smaller limit.
      for limit in /sys/fs/cgroup/memory.max /sys/fs/cgroup/memory/memory.limit_in_bytes; do
        if [ -r "$limit" ]; then
          memory_kib=$(awk -v available="$memory_kib" '
            /^[0-9]+$/ {value=$1/1024; if (available=="" || value<available) available=value}
            END {if (available!="") printf "%.0f\n", available}' "$limit")
        fi
      done
      ;;
  esac
  case "$memory_kib" in ''|*[!0-9]*) ;;
    *)
      # Allow roughly 2 GiB per compiler after leaving 2 GiB for the OS. Unknown
      # memory uses at most two workers; explicit settings remain under user control.
      memory_jobs=$(awk -v kib="$memory_kib" 'BEGIN {
        jobs=int((kib-2097152)/2097152); if(jobs<1)jobs=1; if(jobs>8)jobs=8; print jobs}')
      ;;
  esac
  [ "$cpus" -le "$memory_jobs" ] || cpus=$memory_jobs
  printf '%s\n' "$cpus"
}

zh_resolve_build_jobs() {
  local value=$1 result
  if [ "$value" = auto ]; then zh_auto_build_jobs; return; fi
  result=$(zh_jobs_integer "$value") || {
    echo '[build] ZH_BUILD_JOBS/CMAKE_BUILD_PARALLEL_LEVEL must be auto or a positive integer (at most 2147483647).' >&2
    return 2
  }
  printf '%s\n' "$result"
}

zh_resolve_test_jobs() {
  local value=$1 build_jobs=$2 result
  if [ "$value" = auto ]; then
    [ "$build_jobs" -le 4 ] || build_jobs=4
    printf '%s\n' "$build_jobs"
    return
  fi
  result=$(zh_jobs_integer "$value") || {
    echo '[build] ZH_TEST_JOBS/CTEST_PARALLEL_LEVEL must be auto or a positive integer (at most 2147483647).' >&2
    return 2
  }
  printf '%s\n' "$result"
}
