"""Tabular Q-learning for MacroEnv: USA against the easy USA AI under -ruleset usabasic, one macro
action every five seconds.

    cd GeneralsMD/Code/Tools/rl
    python train_macro.py --episodes 300 --workers 5            # trains, one real match an episode
    python train_macro.py --steps 1000000 --workers 6 --checkpoint-every 10000
    python train_macro.py --play macro_q.npy --matches 3        # watch a saved table play, no learning
    python train_macro.py --episodes 3 --resume --watch         # one windowed game, no game interface, the agent's panels over it

Each worker owns one game at a time on its own port (--port, --port + 1, ...), so the matches run side
by side and every one is a fresh generals_rl.exe with its own seed. The table is shared: a worker plays
its whole match with the table as it stands, then replays the match backward through the Q update,
last decision first, so a win or a loss reaches the opening moves in one pass instead of creeping
back one step per episode.

Exploration, the greedy pick and the update's bootstrap all look only at the actions the env's mask()
lets through, so the table never learns or chooses an action the game was certain to refuse.

Every finished match is one line in --log (JSON), and the table is saved to --out after each.

--steps budgets the run in decisions instead of matches; workers stop taking new matches once it is
spent, so the run ends a few hundred decisions past it. With --checkpoint-every K, each time the count
passes a multiple of K the table is saved as <out>_step<k>.npy and checkpoints/step<k>/ gets a
summary.json: the training since the previous checkpoint and the account of one greedy match of that
table on seed EVAL_SEED, played in a window on --eval-port beside the training, with pictures of it.
One such match runs at a time; a checkpoint that comes due while the last one is still playing gets
its match queued; when the playing one ends the newest queued checkpoint plays, and any older one
still waiting writes its summary without a match.

--play is that greedy match on its own: the table is loaded, epsilon is 0, nothing is learned, and
the game runs in a window with the agent's panels over it.
"""

import argparse
import glob
import json
import os
import sys
import threading
import time
from collections import Counter

import numpy as np

from humvee_env import Pace, history_chart, q_rows, signed
from control_client import ControlError
from macro_env import (ACTIONS, FRAME_CAP, N_ACTIONS, N_STATES, PORT, THREAT_RADIUS, MacroEnv, prepare_exe,
                       read_templates)

ACTION_NAMES = [a[0] for a in ACTIONS]

ALPHA = 0.1
GAMMA = 0.99
EPSILON_START, EPSILON_END = 1.0, 0.05
EXPLORE_SHARE = 0.7             # epsilon reaches its floor after this share of the episodes (or --steps)

EVAL_SEED = 7                   # every checkpoint plays the same match, so their pictures compare
EVAL_PORT = PORT + 40           # clear of the workers' ports and of 8787, 8788 and 8797
SHOT_EVERY = 15                 # decisions between pictures of a checkpoint's match
SHOTS_KEPT = 4                  # spread over the match, the last one always among them


def greedy(row, mask, rng):
    """The best of the valid actions, ties broken at random."""
    row = np.where(mask, row, -np.inf)
    return int(rng.choice(np.flatnonzero(row == row.max())))


def learn(q, episode):
    """One backward sweep of Q-learning over a finished match: [(state, action, reward, next, done,
    next mask)]. The bootstrap takes the best of the actions valid in the next state only."""
    for state, action, r, next_state, done, next_mask in reversed(episode):
        target = r if done else r + GAMMA * q[next_state][next_mask].max()
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


def spread(n, k):
    """Indices of k items spread evenly over n, first and last included."""
    return sorted({int(i) for i in np.linspace(0, n - 1, min(n, k)).round()})


def changes(known, units):
    """Templates finished and templates gone since the last call, known ({id: (template, built)})
    brought up to date. A unit counts as finished when first seen, a structure when built turns true."""
    finished = []
    for u in units:
        old = known.get(u["id"])
        if u["built"] and not (old and old[1]):
            finished.append(u["template"])
        known[u["id"]] = (u["template"], u["built"])
    alive = {u["id"] for u in units}
    gone = [known.pop(i)[0] for i in list(known) if i not in alive]
    return finished, gone


def user_data_folder(env):
    """Where the game writes sshotNNN.bmp, as its own log names it."""
    for line in open(os.path.join(env.run_folder, env.log_prefix + "DebugLogFile.txt"), errors="replace"):
        if line.startswith("User data folder: "):
            return line[len("User data folder: "):].strip()
    raise RuntimeError("the game's log names no user data folder")


def take_shot(env, sshots, path):
    """The screenshot verb's picture of the frame on screen (the back buffer, so the window need not be
    in front), moved out of the user data folder as a PNG.

    ponytail: the newest sshot that was not there before the request is taken as ours, which another
    session's screenshot landing in the same moment would fool; a per-run screenshot folder in the
    engine fixes that if it ever happens."""
    from PIL import Image
    pattern = os.path.join(sshots, "sshot*.bmp")
    before = set(glob.glob(pattern))
    env.game.screenshot()
    deadline = time.time() + 30
    while not set(glob.glob(pattern)) - before:
        if time.time() > deadline:
            raise RuntimeError("no screenshot appeared in %s" % sshots)
        time.sleep(0.05)
    bmp = max(set(glob.glob(pattern)) - before, key=os.path.getmtime)
    size = -1
    while size != os.path.getsize(bmp):     # the engine is still writing it
        size = os.path.getsize(bmp)
        time.sleep(0.2)
    with Image.open(bmp) as picture:
        picture.save(path)
    os.remove(bmp)


def play(env, q, seed, rng, show=None, shots=None):
    """One match with the table as it stands: the greedy pick over the masked actions, no exploration,
    no learning. show(step, frame, q_row, mask, action, refused, r, total, army) feeds a watched game's
    overlay. shots, a folder, gets a picture every SHOT_EVERY decisions and one at the end, of which
    SHOTS_KEPT spread over the match stay. Returns the account of the match as plain facts."""
    started = time.time()
    state, info = env.reset(seed)
    mask = info["mask"]
    ours = {u["id"]: (u["template"], u["built"]) for u in env.ours}
    theirs = {u["id"]: (u["template"], u["built"]) for u in env.theirs}
    built, enemy_built, lost, killed = Counter(), Counter(), Counter(), Counter()
    counts, army, events, pictures = Counter(), [], [], []
    sshots = user_data_folder(env) if shots else None
    total, refused, threat, step, done, last, damage, idle = 0.0, 0, False, 0, False, None, 0.0, 0
    if show:
        show(0, info["frame"], q[state], mask)
    while not done:
        action = greedy(q[state], mask, rng)
        frame = env.frame
        if ACTIONS[action][1] == "attack":
            target = env._target()
            events.append({"frame": frame, "event": "attack", "army": len(env._army(env.ours)),
                           "enemy_army": len(env._army(env.theirs)),
                           "target": next((u["template"] for u in env.theirs if (u["x"], u["y"]) == tuple(target)),
                                          "their start")})
        elif ACTIONS[action][1] == "gather" and action != last:      # a run of gathers is one event
            events.append({"frame": frame, "event": "gather", "army": len(env._army(env.ours))})
        last = action
        next_state, r, terminated, truncated, info = env.step(action)
        step += 1
        done = terminated or truncated
        if show:
            show(step, info["frame"], q[state], mask, action, info["refused"], r, total + r,
                 (info["army"], info["enemy_army"]))
        state, mask, total = next_state, info["mask"], total + r
        refused += info["refused"]
        counts[ACTION_NAMES[action]] += 1
        damage, idle = damage + info["damage"], idle + info["idle"]

        finished, gone = changes(ours, env.ours)
        enemy_finished, enemy_gone = changes(theirs, env.theirs)
        built.update(finished), lost.update(gone), enemy_built.update(enemy_finished), killed.update(enemy_gone)
        if finished or gone or enemy_gone:
            events.append({"frame": info["frame"], "event": "change", "built": finished, "lost": gone,
                           "killed": enemy_gone})
        centre = env.base()
        near = sum(np.hypot(u["x"] - centre[0], u["y"] - centre[1]) < THREAT_RADIUS for u in env._army(env.theirs))
        if bool(near) != threat:
            threat = bool(near)
            events.append({"frame": info["frame"], "event": "enemy army at our base" if threat else "base clear",
                           "enemy_near": int(near)})
        army.append([info["frame"], info["army"], info["enemy_army"], env.money])
        if shots and (step % SHOT_EVERY == 0 or done):
            name = "frame%05d.png" % info["frame"]
            take_shot(env, sshots, os.path.join(shots, name))
            pictures.append({"file": name, "frame": info["frame"]})
    if shots:
        keep = spread(len(pictures), SHOTS_KEPT)
        for index, picture in enumerate(pictures):
            if index not in keep:
                os.remove(os.path.join(shots, picture["file"]))
        pictures = [pictures[i] for i in keep]
    return {"seed": seed, "result": info["result"], "final_frame": info["frame"], "decisions": step,
            "return": round(total, 3), "refused": refused, "damage": round(damage), "idle": idle, "seconds": round(time.time() - started, 1),
            "action_counts": dict(counts), "built": dict(built), "lost": dict(lost), "enemy_built": dict(enemy_built),
            "killed": dict(killed), "army_columns": ["frame", "army", "enemy_army", "money"], "army": army,
            "events": events, "pictures": pictures}


def play_panels(title, match, step, frame, q_row, mask, action=None, refused=False, r=0.0, total=0.0, army=(0, 0),
                finished=()):
    """The overlay of a played match; finished is [(result, return)] of the matches played before it."""
    results = [result for result, _ in finished]
    payload = {
        "eyebrow": "Greedy play, no learning", "title": title, "subtitle": "USA vs easy USA, usabasic, Winter Wolf",
        "progress": round(100.0 * frame / FRAME_CAP, 1),
        "stats": [{"label": "match", "value": match}, {"label": "step", "value": step},
                  {"label": "frame", "value": frame}, {"label": "epsilon", "value": "0"}],
        "action": ACTION_NAMES[action] if action is not None else "match starting",
        "mode": "none" if action is None else "greedy", "result": "refused" if refused else "ok",
        "actions": q_rows(ACTION_NAMES, q_row, action, mask),
        "facts": [signed("reward", r), signed("return", total), {"label": "army", "value": "%d vs %d" % army}],
        "runs": "Matches",
        "tally": [{"label": "won", "value": results.count("win"), "kind": "win"},
                  {"label": "drawn", "value": results.count("draw"), "kind": "draw"},
                  {"label": "lost", "value": results.count("loss"), "kind": "loss"}],
        "runfacts": [{"label": "played", "value": len(finished)}]}
    payload.update(history_chart([value for _, value in finished]))
    return payload


def play_only(arguments, templates):
    q = np.load(arguments.play)
    env = MacroEnv(port=arguments.port, templates=templates, log_prefix="rlplay%d_" % arguments.port, watch=True)
    rng = np.random.default_rng(arguments.seed)
    finished = []
    title = os.path.basename(arguments.play)
    try:
        for match in range(arguments.matches):
            label = "%d / %d" % (match + 1, arguments.matches)
            account = play(env, q, arguments.seed + match, rng,
                           lambda *shown: env.show(play_panels(title, label, *shown, finished=finished)))
            finished.append((account["result"], account["return"]))
            print("match %d  %-4s  frame %5d  return %7.2f  refused %d/%d  built %s"
                  % (match, account["result"], account["final_frame"], account["return"], account["refused"],
                     account["decisions"], account["built"]), flush=True)
    finally:
        env.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--episodes", type=int, default=300)
    parser.add_argument("--steps", type=int, help="decision budget; replaces --episodes")
    parser.add_argument("--checkpoint-every", type=int, help="steps between checkpoints (snapshot, play match, pictures)")
    parser.add_argument("--checkpoints", default="checkpoints", help="folder of the step<k>/ checkpoint folders")
    parser.add_argument("--eval-port", type=int, default=EVAL_PORT)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--seed", type=int, default=0, help="first match seed; episode n plays seed + n")
    parser.add_argument("--epsilon", type=float, help="fixed exploration rate instead of the schedule")
    parser.add_argument("--out", default="macro_q.npy")
    parser.add_argument("--log", default="macro_log.jsonl")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--watch", action="store_true", help="windowed instead of headless, one worker")
    parser.add_argument("--play", metavar="TABLE", help="play a saved table greedily in a window, no learning")
    parser.add_argument("--matches", type=int, default=1, help="how many matches --play plays")
    arguments = parser.parse_args()
    if arguments.watch:
        arguments.workers = 1

    prepare_exe()
    templates = read_templates()
    if arguments.play:
        play_only(arguments, templates)
        return
    q = np.load(arguments.out) if arguments.resume else np.zeros((N_STATES, N_ACTIONS))
    lock = threading.Lock()
    next_episode = [0]
    rows = []
    # a resumed table carries the matches that taught it, so the watched history starts from them
    earlier = [json.loads(line) for line in open(arguments.log)] if arguments.resume and os.path.exists(arguments.log) else []
    log = open(arguments.log, "a")
    # a resumed run numbers its matches after the logged ones, so seeds and episode numbers do not repeat
    first = len(earlier)
    # a resumed --steps run counts the decisions that taught the table against its budget, which also
    # puts epsilon and the checkpoint numbering where the run left them
    steps = [sum(row["decisions"] for row in earlier) if arguments.steps else 0]
    every = arguments.checkpoint_every
    last_checkpoint = [steps[0] // every * every if every else 0]
    since_checkpoint = [0]          # rows[] index where the training since the last checkpoint starts
    evaluator, playing, pending = [None], [False], [None]
    began = time.time()

    def epsilon(episode, steps_done):
        if arguments.epsilon is not None:
            return arguments.epsilon
        if arguments.steps:
            share = min(1.0, steps_done / max(1.0, EXPLORE_SHARE * arguments.steps))
        else:
            share = min(1.0, episode / max(1.0, EXPLORE_SHARE * arguments.episodes))
        return EPSILON_START + (EPSILON_END - EPSILON_START) * share

    if arguments.resume:
        print("resumed: %d episodes, %d steps, epsilon %.3f, next episode %d, next checkpoint %s"
              % (first, steps[0], epsilon(0, steps[0]), first,
                 "step%d" % (last_checkpoint[0] + every) if every else "none"), flush=True)

    def write_summary(folder, summary):
        with open(os.path.join(folder, "summary.json"), "w") as f:
            json.dump(summary, f, indent=1)

    def evaluate(k, snapshot, summary, folder):
        """The checkpoint's play match, on its own thread while the workers carry on."""
        env = MacroEnv(port=arguments.eval_port, templates=templates, log_prefix="rlplay%d_" % arguments.eval_port,
                       watch=True)
        try:
            summary["play"] = play(env, snapshot, EVAL_SEED, np.random.default_rng(k),
                                   lambda *shown: env.show(play_panels("Checkpoint step %d" % k, "eval", *shown)),
                                   shots=folder)
        finally:
            env.close()
        write_summary(folder, summary)
        account = summary["play"]
        print("checkpoint %d  play %-4s  frame %5d  return %7.2f  %d pictures  %.0fs"
              % (k, account["result"], account["final_frame"], account["return"], len(account["pictures"]),
                 account["seconds"]), flush=True)

    def checkpoint(k):
        """Save the table as it stands and start its play match; runs under the lock."""
        snapshot = q.copy()
        table = "%s_step%d.npy" % (os.path.splitext(arguments.out)[0], k)
        np.save(table, snapshot)
        since = rows[since_checkpoint[0]:]
        since_checkpoint[0] = len(rows)
        results = [row["result"] for row in since]
        summary = {"step": k, "steps_done": steps[0], "episodes": len(earlier) + len(rows),
                   "wall_seconds": round(time.time() - began, 1), "epsilon": round(epsilon(len(rows), steps[0]), 3),
                   "table": os.path.abspath(table), "states_visited": int((snapshot != 0).any(axis=1).sum()),
                   "training_since_last": {
                       "episodes": len(since), "steps": sum(row["decisions"] for row in since),
                       "win": results.count("win"), "draw": results.count("draw"), "loss": results.count("loss"),
                       "mean_return": round(float(np.mean([row["return"] for row in since])), 3) if since else None,
                       "mean_refused": round(float(np.mean([row["refused"] for row in since])), 2) if since else None,
                       "mean_frame": round(float(np.mean([row["frame"] for row in since]))) if since else None}}
        folder = os.path.join(arguments.checkpoints, "step%d" % k)
        os.makedirs(folder, exist_ok=True)
        job = (k, snapshot, summary, folder)
        if not playing[0]:
            playing[0] = True
            evaluator[0] = threading.Thread(target=evaluations, args=(job,))
            evaluator[0].start()
            return
        if pending[0] is not None:          # only the newest waiting checkpoint gets played
            summary_skipped = pending[0][2]
            summary_skipped["play"] = None
            summary_skipped["play_skipped"] = "a newer checkpoint came due before this one's play match could start"
            write_summary(pending[0][3], summary_skipped)
        pending[0] = job

    def evaluations(job):
        """Play checkpoints one at a time, and when one is done the newest that came due meanwhile."""
        while job is not None:
            try:
                evaluate(*job)
            except (OSError, RuntimeError, ControlError) as error:
                # a game that died or a picture that never came costs this checkpoint its match, not
                # every checkpoint after it
                job[2]["play"], job[2]["play_error"] = None, repr(error)
                write_summary(job[3], job[2])
                print("checkpoint %d  play failed: %r" % (job[0], error), flush=True)
            with lock:
                job, pending[0] = pending[0], None
                playing[0] = job is not None

    pace = Pace()

    def watched(episode, eps, step, frame, q_row, mask, action=None, explore=False, refused=False, r=0.0, total=0.0,
                army=(0, 0)):
        """The overlay a --watch game shows after every decision: the agent left, its episodes right."""
        with lock:
            done = earlier + rows
        results = [row["result"] for row in done]
        payload = {
            "eyebrow": "Tabular Q-learning", "title": "Macro agent", "subtitle": "USA vs easy USA, usabasic, Winter Wolf",
            "progress": round(100.0 * (steps[0] / arguments.steps if arguments.steps else episode / arguments.episodes), 1),
            "stats": [{"label": "episode", "value": episode + 1 if arguments.steps
                       else "%d / %d" % (episode + 1, arguments.episodes)},
                      {"label": "step", "value": step}, {"label": "frame", "value": frame},
                      {"label": "epsilon", "value": "%.2f" % eps}, {"label": "speed", "value": pace(frame)},
                      {"label": "seed", "value": arguments.seed + first + episode}],
            "action": ACTION_NAMES[action] if action is not None else "match starting",
            "mode": "none" if action is None else "explore" if explore else "greedy",
            "result": "refused" if refused else "ok",
            "actions": q_rows(ACTION_NAMES, q_row, action, mask),
            "facts": [signed("reward", r), signed("return", total), {"label": "army", "value": "%d vs %d" % army}],
            "runs": "Episodes",
            "tally": [{"label": "won", "value": results.count("win"), "kind": "win"},
                      {"label": "drawn", "value": results.count("draw"), "kind": "draw"},
                      {"label": "lost", "value": results.count("loss"), "kind": "loss"}],
            "runfacts": [{"label": "best return",
                          "value": "%+.2f" % max(row["return"] for row in done) if done else "-"},
                         {"label": "refused", "value": "%d of %d" % (sum(row["refused"] for row in done),
                                                                     sum(row["decisions"] for row in done))}]}
        payload.update(history_chart([row["return"] for row in done]))
        return payload

    def worker(index):
        rng = np.random.default_rng(arguments.seed * 1000 + index)
        env = MacroEnv(port=arguments.port + index, templates=templates, log_prefix="rlmacro%d_" % (arguments.port + index),
                       watch=arguments.watch)
        try:
            while True:
                with lock:
                    episode = next_episode[0]
                    # ponytail: the budget is checked when a match starts, so the matches in flight
                    # carry the run up to workers x 180 decisions past --steps
                    if steps[0] >= arguments.steps if arguments.steps else episode >= arguments.episodes:
                        return
                    next_episode[0] += 1
                    eps = epsilon(episode, steps[0])
                started = time.time()
                number = first + episode
                state, info = env.reset(arguments.seed + number)
                mask = info["mask"]
                transitions, total, refused, counts, refused_by = [], 0.0, 0, [0] * N_ACTIONS, [0] * N_ACTIONS
                damage, idle = 0.0, 0
                if arguments.watch:
                    env.show(watched(episode, eps, 0, info["frame"], q[state], mask))
                done = False
                while not done:
                    explore = rng.random() < eps
                    action = int(rng.choice(np.flatnonzero(mask))) if explore else greedy(q[state], mask, rng)
                    next_state, r, terminated, truncated, info = env.step(action)
                    if arguments.watch:
                        env.show(watched(episode, eps, len(transitions) + 1, info["frame"], q[state], mask, action,
                                         explore, info["refused"], r, total + r, (info["army"], info["enemy_army"])))
                    transitions.append((state, action, r, next_state, terminated, info["mask"]))
                    state, mask, total, done = next_state, info["mask"], total + r, terminated or truncated
                    refused += info["refused"]
                    refused_by[action] += info["refused"]
                    counts[action] += 1
                    damage, idle = damage + info["damage"], idle + info["idle"]
                env.close()
                with lock:
                    learn(q, transitions)
                    np.save(arguments.out, q)
                    row = {"episode": number, "seed": arguments.seed + number, "epsilon": round(eps, 3),
                           "result": info["result"], "frame": info["frame"], "return": round(total, 3),
                           "lead": round(info["lead"], 3), "decisions": len(transitions), "refused": refused,
                           "unfinished": info["unfinished"], "dozers": info["dozers"],
                           "damage": round(damage), "idle": idle,
                           "actions": dict(zip(ACTION_NAMES, counts)),
                           "refused_by": {name: n for name, n in zip(ACTION_NAMES, refused_by) if n},
                           "seconds": round(time.time() - started, 1)}
                    rows.append(row)
                    log.write(json.dumps(row) + "\n")
                    log.flush()
                    print("episode %4d  %-4s  frame %5d  return %7.2f  lead %6.2f  refused %3d/%3d  idle %3d  "
                          "damage %6d  eps %.2f  %.0fs"
                          % (number, row["result"], row["frame"], total, row["lead"], refused, len(transitions),
                             idle, damage, eps, row["seconds"]), flush=True)
                    steps[0] += len(transitions)
                    if every and steps[0] >= last_checkpoint[0] + every:
                        last_checkpoint[0] = steps[0] // every * every
                        checkpoint(last_checkpoint[0])
        finally:
            env.close()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(arguments.workers)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    if evaluator[0] is not None:
        evaluator[0].join()
    log.close()
    rows.sort(key=lambda row: row["episode"])
    print(summarise(rows))


def self_check():
    q = np.zeros((3, 2))
    # a two-step match: the win at the end reaches the first decision in one backward sweep
    learn(q, [(0, 1, 0.0, 1, False, np.array([True, True])), (1, 0, 10.0, 2, True, np.array([True, False]))])
    assert q[1, 0] == ALPHA * 10 and q[0, 1] == ALPHA * GAMMA * q[1, 0]
    # the bootstrap ignores a masked action however good its Q-value
    q = np.array([[0.0, 0.0], [-1.0, 5.0]])
    learn(q, [(0, 0, 0.0, 1, False, np.array([True, False]))])
    assert q[0, 0] == ALPHA * GAMMA * -1.0
    rng = np.random.default_rng(0)
    assert greedy(np.array([0.0, 2.0, 2.0]), np.array([True, True, True]), rng) in (1, 2)
    assert all(greedy(np.array([-3.0, 2.0, 9.0]), np.array([True, False, False]), rng) == 0 for _ in range(10))
    # pictures kept: spread over the match, the first and the last always among them
    assert spread(7, 4) == [0, 2, 4, 6] and spread(2, 4) == [0, 1] and spread(1, 4) == [0]
    # the play account: a structure counts when it finishes, a unit when it appears, and both when gone
    known = {1: ("CC", True)}
    assert changes(known, [{"id": 1, "template": "CC", "built": True},
                           {"id": 2, "template": "Power", "built": False}]) == ([], [])
    assert changes(known, [{"id": 2, "template": "Power", "built": True},
                           {"id": 3, "template": "Ranger", "built": True}]) == (["Power", "Ranger"], ["CC"])
    assert changes(known, [{"id": 2, "template": "Power", "built": True}]) == ([], ["Ranger"])
    print("self-check passed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-check"]:
        self_check()
    else:
        main()
