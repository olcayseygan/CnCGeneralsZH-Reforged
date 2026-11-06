#!/usr/bin/env python3
"""Bound a runtime canary, including a child stuck inside exec (for example Rosetta)."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import argparse
from dataclasses import dataclass
import math
import os
import signal
import sys
import time
from typing import Optional


TIMEOUT = 124
SUPERVISOR_ERROR = 125


@dataclass(frozen=True)
class Result:
    returncode: Optional[int]  # None means the deadline expired.
    pid: int


def run_canary(command, timeout=10.0):
    """Run a POSIX canary with no inherited output pipes and only nonblocking waits.

    A timed-out child receives SIGKILL. If the kernel cannot finish killing it
    immediately, return without waiting for it to leave uninterruptible sleep.
    The standalone supervisor can then exit; the OS reaps the child later.
    """
    if os.name != 'posix' or not hasattr(os, 'fork'):
        raise ValueError('runtime canaries require POSIX fork/exec')
    if not command or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('a command and a finite positive timeout are required')
    import resource
    limit = resource.getrlimit(resource.RLIMIT_NOFILE)[0]
    max_fd = 65536 if limit == resource.RLIM_INFINITY else min(int(limit), 1048576)
    deadline = time.monotonic() + timeout
    pid = os.fork()
    if pid == 0:
        try:
            null = os.open(os.devnull, os.O_RDWR)
            for descriptor in (0, 1, 2):
                os.dup2(null, descriptor)
            if null > 2:
                os.close(null)
            os.closerange(3, max_fd)
            os.execvp(command[0], command)
        except BaseException:
            os._exit(126)

    while True:
        try:
            completed, status = os.waitpid(pid, os.WNOHANG)
        except InterruptedError:
            continue
        if completed == pid:
            if os.WIFEXITED(status):
                return Result(os.WEXITSTATUS(status), pid)
            if os.WIFSIGNALED(status):
                return Result(-os.WTERMSIG(status), pid)
            return Result(SUPERVISOR_ERROR, pid)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            # Never waitpid(pid, 0): a child stuck in the kernel may not have
            # acted on SIGKILL yet, even though the parent reached its deadline.
            try:
                os.waitpid(pid, os.WNOHANG)
            except (ChildProcessError, InterruptedError):
                pass
            return Result(None, pid)
        time.sleep(min(0.01, remaining))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=10.0)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.command
    if command[:1] == ['--']:
        command = command[1:]
    try:
        result = run_canary(command, arguments.timeout)
    except (OSError, ValueError) as error:
        print('[runtime-probe] supervisor error: %s' % error, file=sys.stderr)
        return SUPERVISOR_ERROR
    if result.returncode is None:
        print('[runtime-probe] canary did not respond within %g seconds' % arguments.timeout, file=sys.stderr)
        return TIMEOUT
    return 128 - result.returncode if result.returncode < 0 else result.returncode


if __name__ == '__main__':
    sys.exit(main())
