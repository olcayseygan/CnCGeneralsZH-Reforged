"""Train, resume and play a MaskablePPO build-order agent on BuildOrderEnv (env.py).

    python train.py train --mock --envs 4 --steps 50000                  # no game needed
    python train.py train --envs 4 --steps 200000 --name usa_easy        # four games on ports 8800..8803
    python train.py train --resume models/usa_easy.zip --steps 100000    # carry on from a save
    python train.py eval --model models/usa_easy.zip --episodes 3        # print the build order it plays
    python train.py eval --model models/usa_easy.zip --watch             # watch it play in a window
    python train.py eval --model models/usa_easy.zip --watch 2           # the same at twice the pace
    tensorboard --logdir runs

Each env with --envs N runs its own generals.exe on port --port + rank, launched and killed by the
env itself (GeneralsMD/Run/generals.exe, or --exe / ZHR_GENERALS_EXE). Needs sb3-contrib.
"""

import argparse
import os

import numpy as np
from sb3_contrib import MaskablePPO
from stable_baselines3.common.callbacks import CheckpointCallback
from stable_baselines3.common.monitor import Monitor
from stable_baselines3.common.vec_env import DummyVecEnv, SubprocVecEnv

from env import BUILDINGS, DEFAULT_MAP, DEFAULT_PORT, FRAMES_PER_GAME_SECOND, UNITS, BuildOrderEnv


def env_kwargs(args, rank):
    return dict(port=args.port + rank, mock=args.mock, decision_seconds=args.decision_seconds,
                episode_minutes=args.episode_minutes, map_name=args.map, seed=args.seed + 100000 * rank,
                opponent=args.opponent, difficulty=args.difficulty, exe=args.exe, user_dir=args.user_dir,
                headless=not (args.window or getattr(args, "watch", 0)), frames_per_game_second=args.frames_per_game_second,
                watch_speed=getattr(args, "watch", 0) or 0)


def make_env(args, rank):
    return lambda: Monitor(BuildOrderEnv(**env_kwargs(args, rank)))


def train(args):
    makers = [make_env(args, rank) for rank in range(args.envs)]
    venv = SubprocVecEnv(makers) if args.envs > 1 else DummyVecEnv(makers)
    try:
        if args.resume:
            model = MaskablePPO.load(args.resume, env=venv, device=args.device, tensorboard_log=args.logdir)
        else:
            model = MaskablePPO("MlpPolicy", venv, n_steps=args.n_steps, batch_size=64, gamma=0.995,
                                ent_coef=0.01, learning_rate=3e-4, seed=args.seed, device=args.device,
                                tensorboard_log=args.logdir, verbose=1)
        checkpoints = CheckpointCallback(save_freq=max(1, args.checkpoint // args.envs),
                                         save_path=os.path.join(args.save_dir, args.name + "_checkpoints"),
                                         name_prefix=args.name)
        model.learn(total_timesteps=args.steps, callback=checkpoints, tb_log_name=args.name,
                    reset_num_timesteps=not args.resume)
        path = os.path.join(args.save_dir, args.name)
        model.save(path)
        print("saved %s.zip" % path)
    finally:
        venv.close()


def evaluate(args):
    env = BuildOrderEnv(**env_kwargs(args, 0))
    model = MaskablePPO.load(args.model, device=args.device) if args.model else None
    rng = np.random.default_rng(args.seed)
    try:
        for episode in range(args.episodes):
            obs, info = env.reset(seed=args.seed + episode)
            start, total, done = info["frame"], 0.0, False
            print("episode %d (seed %d)" % (episode, info["seed"]))
            while not done:
                if model:
                    action, _ = model.predict(obs, action_masks=env.action_masks(), deterministic=not args.sample)
                else:
                    action = rng.choice(np.flatnonzero(env.action_masks()))
                obs, reward, terminated, truncated, info = env.step(action)
                total, done = total + reward, terminated or truncated
                if info["action"] != "wait" or args.watch:
                    seconds = (info.get("frame", start) - start) / args.frames_per_game_second
                    print("  %02d:%02d  %-9s %s" % (seconds // 60, seconds % 60, info["action"],
                                                    "REFUSED: %s" % info.get("why") if info.get("refused")
                                                    else "$%d left" % info.get("money", 0)), flush=True)
            s = env.state
            seconds = (s["frame"] - start) / args.frames_per_game_second
            print("  return %.2f, %s at %02d:%02d%s" % (total, "lost" if info.get("lost") else "won" if info.get("won") else "time up",
                                                       seconds // 60, seconds % 60, " (%s)" % info["error"] if "error" in info else ""))
            print("  built %s" % {k: s["built"][k] for k in BUILDINGS})
            print("  units %s" % {k: s["units"][k] for k in UNITS})
    finally:
        env.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    shared = argparse.ArgumentParser(add_help=False)
    shared.add_argument("--mock", action="store_true", help="the fake game in mock_game.py instead of generals.exe")
    shared.add_argument("--port", type=int, default=DEFAULT_PORT, help="first control port; env n uses port + n")
    shared.add_argument("--exe", help="generals.exe (default ZHR_GENERALS_EXE, else this checkout's Run folder)")
    shared.add_argument("--user-dir", help="root of the per-game ZH_USER_DATA_DIR folders (default ZHR_RL_USER_DIR or temp)")
    shared.add_argument("--map", default=DEFAULT_MAP)
    shared.add_argument("--opponent", default="FactionGLA", help="slot 1's faction, e.g. FactionGLA, FactionAmerica")
    shared.add_argument("--difficulty", default="easy", choices=["easy", "medium", "hard"])
    shared.add_argument("--decision-seconds", type=float, default=5.0, help="game seconds between decisions")
    shared.add_argument("--episode-minutes", type=float, default=10.0, help="game minutes in an episode")
    shared.add_argument("--frames-per-game-second", type=int, default=FRAMES_PER_GAME_SECOND,
                        help="logic frames a game second: 30 Hz * 100 / GAME_SPEED_PERCENT")
    shared.add_argument("--window", action="store_true", help="draw the game in an 800x600 window instead of -headless")
    shared.add_argument("--seed", type=int, default=0)
    shared.add_argument("--device", default="cpu", help="torch device; a small MLP trains fastest on cpu")

    t = commands.add_parser("train", parents=[shared], help="train a new model or --resume one")
    t.add_argument("--envs", type=int, default=1, help="games in parallel (SubprocVecEnv above 1)")
    t.add_argument("--steps", type=int, default=100000, help="decisions to train for")
    t.add_argument("--n-steps", type=int, default=256, help="decisions per env per PPO update")
    t.add_argument("--resume", help="a saved model .zip to carry on from")
    t.add_argument("--name", default="usa_easy")
    t.add_argument("--save-dir", default="models")
    t.add_argument("--logdir", default="runs", help="tensorboard log folder")
    t.add_argument("--checkpoint", type=int, default=10000, help="save a checkpoint every this many decisions")

    e = commands.add_parser("eval", parents=[shared], help="play a saved model and print its build order")
    e.add_argument("--model", help="a saved model .zip; without one, masked random actions (a baseline)")
    e.add_argument("--episodes", type=int, default=1)
    e.add_argument("--sample", action="store_true", help="sample actions instead of taking the likeliest")
    e.add_argument("--watch", type=float, nargs="?", const=1.0, default=0.0, metavar="SPEED",
                   help="play in an 800x600 window paced for watching: 1 (the default) is the game's own pace, "
                        "2 twice that; every decision is printed as it happens")

    args = parser.parse_args()
    train(args) if args.command == "train" else evaluate(args)


if __name__ == "__main__":
    main()
