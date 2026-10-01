"""Tabular Q-learning for HumveeEnv: one table row per discretized relative target position.

    cd GeneralsMD/Code/Tools/rl
    python train_q.py --episodes 200                     # launches Run/generals.exe, trains, quits
    python train_q.py --episodes 20 --watch              # the same in a window, to watch it learn
    $env:ZHR_RUN = "D:\\path\\to\\GeneralsMD\\Run"        # when the Run beside this tree has no game

The table is saved to --out after every episode, so a killed run keeps what it learned, and
--resume starts from a saved one.
"""

import argparse
import time

import numpy as np

from humvee_env import N_ACTIONS, N_STATES, PORT, HumveeEnv, Pace, discretize, history_chart, q_rows, signed

ACTION_NAMES = ["move %d deg" % (45 * k) for k in range(N_ACTIONS - 1)] + ["stop"]

ALPHA = 0.2
GAMMA = 0.95
EPSILON_START, EPSILON_END = 1.0, 0.05


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--episodes", type=int, default=200)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--out", default="q_table.npy")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--watch", action="store_true", help="windowed game, camera on the Humvee, a helicopter over the target")
    arguments = parser.parse_args()

    rng = np.random.default_rng(arguments.seed)
    q = np.load(arguments.out) if arguments.resume else np.zeros((N_STATES, N_ACTIONS))
    env = HumveeEnv(port=arguments.port, seed=arguments.seed, watch=arguments.watch)
    pace, returns = Pace(), []

    def watched(episode, epsilon, frame, state, action, explore, r, total, distance):
        """The overlay a --watch game shows after every order: the agent left, its episodes right."""
        payload = {
            "eyebrow": "Tabular Q-learning", "title": "Humvee driver", "subtitle": "drive to the helicopter",
            "progress": round(100.0 * episode / arguments.episodes, 1),
            "stats": [{"label": "episode", "value": "%d / %d" % (episode + 1, arguments.episodes)},
                      {"label": "step", "value": env.steps}, {"label": "frame", "value": frame},
                      {"label": "epsilon", "value": "%.2f" % epsilon}, {"label": "speed", "value": pace(frame)},
                      {"label": "state", "value": state}],
            "action": ACTION_NAMES[action], "mode": "explore" if explore else "greedy", "result": "ok",
            "actions": q_rows(ACTION_NAMES, q[state], action),
            "facts": [signed("reward", r), signed("return", total), {"label": "distance", "value": "%.0f" % distance}],
            "runs": "Episodes",
            "tally": [{"label": "arrived", "value": arrivals, "kind": "win"},
                      {"label": "timed out", "value": episode - arrivals, "kind": "loss"},
                      {"label": "arrival rate", "value": "%.0f%%" % (100.0 * arrivals / episode) if episode else "-",
                       "kind": "rate"}],
            "runfacts": [{"label": "best return", "value": "%+.2f" % max(returns) if returns else "-"}]}
        payload.update(history_chart(returns))
        return payload

    try:
        arrivals = 0
        for episode in range(arguments.episodes):
            # ponytail: linear epsilon decay over the run; a schedule per state if some rows stay unvisited
            epsilon = EPSILON_START + (EPSILON_END - EPSILON_START) * episode / max(1, arguments.episodes - 1)
            started = time.time()
            obs, info = env.reset()
            start_distance = info["distance"]
            state = discretize(*obs)
            total, done = 0.0, False
            while not done:
                explore = rng.random() < epsilon
                action = int(rng.integers(N_ACTIONS)) if explore else int(np.argmax(q[state]))
                obs, r, terminated, truncated, info = env.step(action)
                if arguments.watch:
                    env.show(watched(episode, epsilon, info["frame"], state, action, explore, r, total + r,
                                     info["distance"]))
                next_state = discretize(*obs)
                target = r if terminated else r + GAMMA * q[next_state].max()
                q[state, action] += ALPHA * (target - q[state, action])
                state, total, done = next_state, total + r, terminated or truncated
            arrivals += terminated
            returns.append(total)
            np.save(arguments.out, q)
            print("episode %4d  return %7.2f  steps %3d  %s  distance %6.1f -> %6.1f  eps %.2f  arrivals %d  %.1fs"
                  % (episode, total, env.steps, "ARRIVED" if terminated else "timeout", start_distance,
                     info["distance"], epsilon, arrivals, time.time() - started), flush=True)
    finally:
        env.close()


if __name__ == "__main__":
    main()
