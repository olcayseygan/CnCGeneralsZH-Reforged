"""Tabular Q-learning for MacroEnv: USA against the easy USA AI under -ruleset usabasic, one macro
action every five seconds.

    cd GeneralsMD/Code/Tools/rl
    python train_macro.py --episodes 300 --workers 5            # trains, one real match an episode
    python train_macro.py --episodes 20 --resume --epsilon 0    # play the saved table greedily
    python train_macro.py --episodes 3 --resume --watch         # one windowed game, no game interface, the agent top left

Each worker owns one game at a time on its own port (--port, --port + 1, ...), so the matches run side
by side and every one is a fresh generals_rl.exe with its own seed. The table is shared: a worker plays
its whole match with the table as it stands, then replays the match backward through the Q update,
last decision first, so a win or a loss reaches the opening moves in one pass instead of creeping
back one step per episode.

Every finished match is one line in --log (JSON), and the table is saved to --out after each.
"""

import argparse
import json
import sys
import threading
import time

import numpy as np

from humvee_env import overlay_lines
from macro_env import ACTIONS, N_ACTIONS, N_STATES, PORT, MacroEnv, prepare_exe, read_templates

ACTION_NAMES = [a[0] for a in ACTIONS]
TITLE = "RL macro: USA vs easy USA, usabasic"

ALPHA = 0.1
GAMMA = 0.99
EPSILON_START, EPSILON_END = 1.0, 0.05
EXPLORE_SHARE = 0.7             # epsilon reaches its floor after this share of the episodes


def greedy(row, rng):
    best = np.flatnonzero(row == row.max())
    return int(rng.choice(best))


def learn(q, episode):
    """One backward sweep of Q-learning over a finished match: [(state, action, reward, next, done)]."""
    for state, action, r, next_state, done in reversed(episode):
        target = r if done else r + GAMMA * q[next_state].max()
        q[state, action] += ALPHA * (target - q[state, action])


def summarise(rows, blocks=4):
    """Win, draw and loss counts, mean return and mean end frame for each consecutive block of matches."""
    size = max(1, len(rows) // blocks)
    lines = []
    for start in range(0, len(rows), size):
        block = rows[start:start + size]
        results = [row["result"] for row in block]
        lines.append("episodes %3d-%3d  win %2d  draw %2d  loss %2d  return %7.2f  frame %6.0f  refused %4.1f%%"
                     % (block[0]["episode"], block[-1]["episode"], results.count("win"), results.count("draw"),
                        results.count("loss"), np.mean([row["return"] for row in block]),
                        np.mean([row["frame"] for row in block]),
                        100 * np.mean([row["refused"] / row["decisions"] for row in block])))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--episodes", type=int, default=300)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--seed", type=int, default=0, help="first match seed; episode n plays seed + n")
    parser.add_argument("--epsilon", type=float, help="fixed exploration rate instead of the schedule")
    parser.add_argument("--out", default="macro_q.npy")
    parser.add_argument("--log", default="macro_log.jsonl")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--watch", action="store_true", help="windowed instead of headless, one worker")
    arguments = parser.parse_args()
    if arguments.watch:
        arguments.workers = 1

    prepare_exe()
    templates = read_templates()
    q = np.load(arguments.out) if arguments.resume else np.zeros((N_STATES, N_ACTIONS))
    lock = threading.Lock()
    next_episode = [0]
    rows = []
    log = open(arguments.log, "a")

    def epsilon(episode):
        if arguments.epsilon is not None:
            return arguments.epsilon
        share = min(1.0, episode / max(1.0, EXPLORE_SHARE * arguments.episodes))
        return EPSILON_START + (EPSILON_END - EPSILON_START) * share

    def worker(index):
        rng = np.random.default_rng(arguments.seed * 1000 + index)
        env = MacroEnv(port=arguments.port + index, templates=templates, log_prefix="rlmacro%d_" % index,
                       watch=arguments.watch)
        try:
            while True:
                with lock:
                    episode = next_episode[0]
                    if episode >= arguments.episodes:
                        return
                    next_episode[0] += 1
                eps = epsilon(episode)
                started = time.time()
                state, _ = env.reset(arguments.seed + episode)
                transitions, total, refused, counts = [], 0.0, 0, [0] * N_ACTIONS
                env.show(overlay_lines(TITLE, [("episode", "%d  seed %d" % (episode, arguments.seed + episode)),
                                               ("epsilon", "%.2f" % eps)], ACTION_NAMES, q[state], None))
                done = False
                while not done:
                    explore = rng.random() < eps
                    action = int(rng.integers(N_ACTIONS)) if explore else greedy(q[state], rng)
                    next_state, r, terminated, truncated, info = env.step(action)
                    env.show(overlay_lines(TITLE, [
                        ("episode", "%d  seed %d" % (episode, arguments.seed + episode)),
                        ("step", "%d  frame %d" % (len(transitions) + 1, info["frame"])),
                        ("action", "%s (%s)" % (ACTION_NAMES[action], "explore" if explore else "greedy")),
                        ("result", "REFUSED" if info["refused"] else "done"), ("reward", "%+.3f" % r),
                        ("return", "%+.3f" % (total + r)), ("epsilon", "%.2f" % eps),
                        ("army", "%d vs %d" % (info["army"], info["enemy_army"]))], ACTION_NAMES, q[state], action))
                    transitions.append((state, action, r, next_state, terminated))
                    state, total, done = next_state, total + r, terminated or truncated
                    refused += info["refused"]
                    counts[action] += 1
                env.close()
                with lock:
                    learn(q, transitions)
                    np.save(arguments.out, q)
                    row = {"episode": episode, "seed": arguments.seed + episode, "epsilon": round(eps, 3),
                           "result": info["result"], "frame": info["frame"], "return": round(total, 3),
                           "lead": round(info["lead"], 3), "decisions": len(transitions), "refused": refused,
                           "actions": dict(zip([a[0] for a in ACTIONS], counts)),
                           "seconds": round(time.time() - started, 1)}
                    rows.append(row)
                    log.write(json.dumps(row) + "\n")
                    log.flush()
                    print("episode %4d  %-4s  frame %5d  return %7.2f  lead %6.2f  refused %3d/%3d  eps %.2f  %.0fs"
                          % (episode, row["result"], row["frame"], total, row["lead"], refused, len(transitions),
                             eps, row["seconds"]), flush=True)
        finally:
            env.close()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(arguments.workers)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    log.close()
    rows.sort(key=lambda row: row["episode"])
    print(summarise(rows))


def self_check():
    q = np.zeros((3, 2))
    # a two-step match: the win at the end reaches the first decision in one backward sweep
    learn(q, [(0, 1, 0.0, 1, False), (1, 0, 10.0, 2, True)])
    assert q[1, 0] == ALPHA * 10 and q[0, 1] == ALPHA * GAMMA * q[1, 0]
    assert greedy(np.array([0.0, 2.0, 2.0]), np.random.default_rng(0)) in (1, 2)
    print("self-check passed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-check"]:
        self_check()
    else:
        main()
