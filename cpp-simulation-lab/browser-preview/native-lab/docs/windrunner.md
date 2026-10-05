# WindRunner RL

WindRunner is a real, small C++17 reinforcement-learning experiment: a drone modeled as a point mass learns two continuous thrust controls while flying through a winding cave with wind shear. The native program owns the environment, PPO training, weights, seeded evaluation, and trajectory export. `mirror.js` reproduces its physics and deterministic trained policy for an interactive browser demo; the browser does not retrain the policy.

No external C++ libraries, frameworks, CMake, GPU, or network access are required. The checked-in model was trained from zero mean weights by the included PPO implementation. It is a linear Gaussian policy, not a scripted controller or a neural network.

## Build and run

```sh
make
make test

# Replay the actual trained mean policy on a selected cave.
./build/windrunner --seed 2147483659 --steps 400 --json

# Inspect the two baselines on the same cave.
./build/windrunner --seed 2147483659 --steps 400 --random --json
./build/windrunner --seed 2147483659 --steps 400 --zero --json

# Reproduce the published training run and artifacts.
./build/windrunner --train --seed 42 --steps 262144 --episodes 64 --out artifacts --json

# Evaluate the loaded final model on the fixed paired held-out seeds.
./build/windrunner --evaluate --episodes 64 --model artifacts/model.json --json

# Optional: verify the JavaScript mirror with Node.
make parity
```

Run these commands from the `native-rl` directory. `--model PATH` selects weights to load or save; `--out DIR` selects the training/evaluation artifact directory. A trajectory ends at the requested step count or the first terminal condition. `--steps` is a training interaction budget under `--train`, and a replay cap otherwise. The environment itself has a 400-step horizon. `--evaluate` always compares all three policies on its fixed seed schedule; its `--episodes` selects the schedule length. Training progress is written to stderr, so `--json` stdout remains machine-readable.

Every JSON result has `{project, seed, mode, summary, frames}`. Replay JSON also contains terrain `environment.phases`, complete state, actions, wind, reward, cumulative return, and terminal status for each frame, including the initial frame. Train/evaluate result frames are empty; their summaries and episode records carry the corresponding metrics. `schema.json` documents the public replay fields.

## Published measured result

Training seed **42**, **262,144 environment steps**, **64 updates**, **2,139 completed episodes**. The final update's completed training episodes averaged **111.8001 return**, with **100% success**. The initial update averaged **−26.7157 return**, with **0% success**. These are on-policy training episode statistics, separate from evaluation.

The final weights were evaluated on **64 fixed, scattered held-out cave seeds**. The same initial cave state was used for each policy. Evaluation uses the Gaussian **mean** action, clipped to `[-1,1]`; the random baseline samples independent uniform actions in `[-1,1]`; no-control applies `[0,0]` while wind, gravity, and drag remain active. Seeds and per-episode results are listed in `artifacts/evaluation.json` and `.csv`.

| Policy | Mean return | Return std. dev. | Goals | Collisions | Mean distance |
|---|---:|---:|---:|---:|---:|
| Trained PPO mean | 112.3255 | 0.2125 | 64 / 64 | 0 / 64 | 64.1769 |
| Uniform random | −26.4415 | 2.5251 | 0 / 64 | 64 / 64 | 1.5337 |
| No control | −26.4507 | 1.5348 | 0 / 64 | 64 / 64 | 1.2481 |

This is one final trained run on one deliberately compact synthetic environment. The result does not establish a real-drone flight capability, performance under changed physics, or guaranteed success on other seeds. The observations expose exact local wind and a terrain preview. The vehicle has no rotational dynamics, battery, sensors, or physical hardware.

## Environment

State is continuous `x, y, vx, vy`, plus elapsed time and integer integration step. Two independent normalized controls `ax, ay ∈ [-1,1]` drive translational thrusters. The drone starts at `x=0`, `vx=.8`, `vy=0`, with a small seeded vertical offset around the cave center. Terrain phases and the offset use a specified xorshift32 generator, so browser and native resets agree.

At `dt=.08`, velocity and position use semi-implicit Euler:

```text
vx += dt * (3*ax + windX - .25*vx)
vy += dt * (4*ay + windY - 1.2 - .32*vy)
x += dt * vx
y += dt * vy
```

The cave center combines three sine waves; its half-width varies between 2.35 and 2.95. The wind field depends on horizontal position, time, seeded phase, and vertical displacement from the cave center. Exact equations are in `src/windrunner.hpp` and the matching `mirror.js`.

The goal is `x≥64`. A wall contact occurs when `abs(y-center(x)) + .22 ≥ halfWidth(x)`, or when the drone moves behind `x=-1`. Wall contact and goal end an episode; the 400-step time limit truncates it. Collision takes precedence if both conditions occur on the same integration step. Collision checks are at integration endpoints.

The shaped reward at each step is:

```text
1.2 * horizontal_progress
+ .08
- .06 * (vertical_offset / cave_half_width)^2
- .015 * (ax^2 + ay^2)
- .003 * vy^2
- 30 on collision
+ 25 on goal
- 5 on timeout
```

The explicit collision penalty competes with progress and survival rewards; effort and vertical-speed penalties discourage unnecessary movement. The reward and all three evaluation policies use exactly the same environment.

The ten policy inputs, in order, are: bias; relative vertical displacement; `vx/4`; `vy/3`; cave center slope; center preview six units ahead divided by three; `windX/1.5`; `windY/1.5`; `x/64`; remaining-time fraction. Each Gaussian mean is a learned dot product over those inputs. Executed actions are clipped, but policy likelihoods are evaluated on the original Gaussian samples. This keeps the sampled-policy likelihood calculation consistent with the raw-action distribution.

## Learning implementation

The actor has **20 mean weights plus two learned log standard deviations**. The value baseline has **19 learned weights**, using the ten observations and the squares of the nine non-bias observations. Training collects 4,096 transitions per update, computes generalized advantage estimates with `gamma=.995, lambda=.95`, normalizes advantages, and applies up to eight shuffled minibatch epochs of size 256.

The actor maximizes `min(ratio*advantage, clip(ratio,.8,1.2)*advantage)` using its analytical Gaussian score gradient. The correctly sign-dependent clipped regions have zero surrogate gradient. Adam ascent uses actor learning rate `.0025`, value learning rate `.012`, actor gradient norm cap `.8`, value gradient norm cap `10`, and Gaussian entropy coefficient `.002`. Log standard deviations are constrained to `[-2.4,.3]`. The value update caps individual target residuals at ±40, a robust squared-error gradient. An epoch-level estimated KL above `.025` stops further actor/value epochs for that rollout.

GAE uses zero next-value at physical termination. Time-limit truncation bootstraps the last state's value, then stops the advantage recursion across the reset boundary. A partial rollout at an update boundary also bootstraps the last state.

The clipped surrogate follows [Schulman et al., *Proximal Policy Optimization Algorithms*, 2017, equation 7](https://arxiv.org/pdf/1707.06347). This repository implements a compact variant; it is not a reproduction of the paper's benchmark settings or results.

Training terrain seeds lie in `[1,1073741823]`. Held-out seeds set the high bit and are generated by a fixed avalanche hash of episode index plus `20261005`; they lie in `[2147483648,4294967295]`. The seed spaces cannot overlap. Held-out scores do not feed policy updates or choose a checkpoint; the final update's weights are published.

## Verification and artifacts

`make test` verifies Gaussian likelihood gradients and both signs of the PPO clipped-objective gradient with central finite differences; GAE termination and truncation behavior; exact same-seed reset/replay; action clipping; explicit acceleration/integration equations; finite observations; terrain bounds; collision/goal/timeout semantics; and rejecting a step after episode end.

The complete run was repeated with the same compiler/runtime: model, training CSV, held-out evaluation JSON, and trajectory were byte-identical. Standard-library random distributions are not specified to be bit-identical across all C++ runtime implementations, so retraining across other platforms can vary. The environment's xorshift32 reset and deterministic policy replay are specified separately.

`make parity` compares the browser mirror with native replay on five seeds, including seed zero and held-out cases. The checked run compared **747 frames**, with maximum absolute numerical difference **3.35×10⁻¹⁰**, due to JSON decimal formatting/floating-point arithmetic.

| File | Contents |
|---|---|
| `artifacts/model.json` | Actual learned mean weights, standard deviations, value weights, input order, seed, training budget |
| `artifacts/training_curve.csv` / `.json` | Every update's measured training return, success, distance, KL, clipping rate, standard deviations |
| `artifacts/evaluation.json` / `.csv` | Paired held-out metrics and all per-episode results |
| `artifacts/trajectory.json` | Full trained rollout on seed 2147483659; 149 steps, goal reached, return 112.4414 |
| `artifacts/provenance.json` | Hyperparameters, verification results, and hashes of the primary published artifacts |
| `schema.json` | JSON replay/result schema |
| `mirror.js` | Dependency-free browser physics and deterministic policy mirror |

The C++ source is the primary executable implementation. The JSON weights and trajectories are real outputs from that executable.
