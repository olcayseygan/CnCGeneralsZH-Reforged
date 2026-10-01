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

from humvee_env import N_ACTIONS, N_STATES, PORT, HumveeEnv, discretize

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
                action = int(rng.integers(N_ACTIONS)) if rng.random() < epsilon else int(np.argmax(q[state]))
                obs, r, terminated, truncated, info = env.step(action)
                next_state = discretize(*obs)
                target = r if terminated else r + GAMMA * q[next_state].max()
                q[state, action] += ALPHA * (target - q[state, action])
                state, total, done = next_state, total + r, terminated or truncated
            arrivals += terminated
            np.save(arguments.out, q)
            print("episode %4d  return %7.2f  steps %3d  %s  distance %6.1f -> %6.1f  eps %.2f  arrivals %d  %.1fs"
                  % (episode, total, env.steps, "ARRIVED" if terminated else "timeout", start_distance,
                     info["distance"], epsilon, arrivals, time.time() - started), flush=True)
    finally:
        env.close()


if __name__ == "__main__":
    main()
