# SHMUP stages

A SHMUP stage is a self-contained, horizontally scrolling shoot-'em-up level entered from a
side-view room through a door whose archetype or placement sets `"shmup": "<stage id>"`
(for example `door_shmup_hangar` in the Landing Bay). Everything about a stage is data in
`assets/shmup/stages.json`; C++ (`src/game/shmup.*`) only provides generic motion and attack
primitives. The simulation is SDL-free, uses the fixed step and fixed-capacity storage.

Entering a stage suspends the room simulation (the room and player stay at the door). Leaving
it returns to the room: a failed run records nothing, the first clear sets the stage bit
(saved in checkpoints as `shmupCleared`, in saves as the optional `world.shmup_cleared`),
sets `completionFact` (usable as `requiresFact`) and grants `rewardItem` once. Mid-stage state
is not saved. Controls: move with the movement input; fire with the shoot or jump button.

## Format (`version: 1`)

Unknown keys are rejected. Limits: 4 stages, 8 enemy types, 24 waves, 4 boss phases and 3
attacks per phase.

- Stage: `id` (`echo:shmup/...`), `name`, `background` (RGB), `scrollSpeed`,
  `completionFact`, optional `rewardItem` (must exist in the item catalog), `player`,
  `enemyTypes`, `waves`, `boss`.
- `player`: `health`, `speed`, `width`, `height`, `fireInterval`, `bulletSpeed`,
  `bulletDamage`, `invulnerability`.
- Enemy type: `id`, `health`, `width`, `height`, `score`, `color`, `motion`, optional `attack`.
- Motion `kind`: `straight`, `sine` (`amplitude`, `frequency`), `dive` (homes on the pilot's
  height once near), `hold` (flies to `anchorX`, then bobs); bosses also use `patrol` and
  `track`.
- Attack `kind`: `aimed`, `spread`, `stream`, `radial`, `wall`, with `interval`, `delay`,
  `count`, `spreadDegrees`, `bulletSpeed`, `bulletSize`.
- Wave: `time`, `enemy` (enemy type id), `count`, `y`, `yStep`, `spacing`.
- Boss: `name`, `health`, `width`, `height`, `spawnTime`, `y`, `color`, `phases`. The first
  phase starts at `healthBelow: 1.0`; later phases use strictly decreasing fractions. Each phase
  has `motion`, `attacks`, optional `transitionSeconds` and optional
  `summon: { enemy, count, interval }`. A phase change makes the boss invulnerable for
  `transitionSeconds` and clears enemy bullets.

The shipped `echo:shmup/cargo_run` has five enemy types and the three-phase WARDEN CORE boss.
Windows tests cover parsing, determinism, phase changes, clearing, failure and checkpoints;
PS5 runtime behaviour is not verified.
