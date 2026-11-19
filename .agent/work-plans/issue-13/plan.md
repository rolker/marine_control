# Plan: Build on ROS 2 Rolling: replace ament_target_dependencies, add a rolling branch and CI

## Issue

https://github.com/rolker/marine_control/issues/13

## Context

`ament_target_dependencies` is removed in Rolling, so `marine_control` does not configure there. The only calls are `marine_control/CMakeLists.txt` lines 25 (library) and 44 (test). `origin/rolling` already exists at 68118f2 (tip of `jazzy`). CI (`.github/workflows/ci.yml`) runs only on `jazzy` in `ros:jazzy-ros-core`. The machine has Rolling (and Lyrical) but no Jazzy, so Jazzy is verified only by CI. Delivered as two PRs on one timeline.

## Approach

### Part 1: PR to `rolling` (body says `Part of #13`; base from `adapter repo_pin`, not the forge default)

1. **Verify the baseline fails** — build all three packages on Rolling with the worktree's `build.sh`/`test.sh` before editing; record the `ament_target_dependencies` error (the failing test for the fix).
2. **Replace the calls** — in `marine_control/CMakeLists.txt`: `target_link_libraries(${PROJECT_NAME} PUBLIC rclcpp::rclcpp ${marine_control_interfaces_TARGETS})` (PUBLIC so consumers inherit them; the library is a static archive linked into plugins). Test target: drop its `ament_target_dependencies` line; link `${PROJECT_NAME}` only if it already propagates, else add `rclcpp::rclcpp ${marine_control_interfaces_TARGETS}`. Keep `ament_export_dependencies`. If `${marine_control_interfaces_TARGETS}` is empty on some distro, fall back to `marine_control_interfaces::marine_control_interfaces__rosidl_typesupport_cpp`.
3. **Build and test on Rolling locally** — all three packages (`marine_control`, `marine_control_interfaces`, `marine_control_py`) build and `colcon test` passes; note any other Rolling breakage (package.xml, py tests, lint) and fix what the issue covers, flag the rest.
4. **Rolling CI job** — edit `ci.yml`: add `rolling` to `push`/`pull_request` branches and turn the job into a matrix over `{jazzy, rolling}` (`image: ros:${{ matrix.distro }}-ros-core`, `source /opt/ros/${{ matrix.distro }}/setup.bash`, `runs-on` ubuntu-24.04 for jazzy and the matching runner for rolling), `fail-fast: false`; update the header comment. One workflow then serves both branches (a pushed `rolling` branch tests Rolling, a `jazzy` push tests Jazzy; a matrix also tests both on each PR, so decide in the write-up). If the matrix cannot be made to behave, use a separate job. Check the workflow with `actionlint` if available.
5. **README** — it names no supported distro; add one line stating Jazzy and Rolling (rolling branch) only if the owner wants it (see questions).
6. **Cost write-up** — after CI is green, comment on the issue: the failure, the fix, other breakage found, effort, and matrix choice.

### Part 2: backport PR to `jazzy`

7. **Cherry-pick the CMake commit only** onto a branch from `origin/jazzy`; leave `ci.yml` unchanged (Jazzy CI stays as is). The PR runs the existing Jazzy CI, the only check available here. If it fails on the interface-target form, adjust in both parts.

## Files to Change

| File | Change |
|------|--------|
| `marine_control/CMakeLists.txt` | Replace two `ament_target_dependencies` with `target_link_libraries` on exported targets (parts 1 and 2) |
| `.github/workflows/ci.yml` | Add `rolling` branch triggers and distro matrix (part 1 only) |
| `README.md` | Optional supported-distro line (part 1) |

## Prior Art

Existing code: the repo's own `target_link_libraries(test_control_server ${PROJECT_NAME})` line. Outside practice: the ROS 2 migration for `ament_target_dependencies` (modern targets, `<pkg>_TARGETS` variable from `rosidl_generate_interfaces`). Taken: that form. Rejected: a manual `include_directories` workaround.

## Serves

None (project has no written goals); no project design document. Part of rolker/unh_marine_autonomy#415 (first repo to move to Rolling).

## Design Picture

No change to the design picture; the project has no design document.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Small steps | Two small PRs, each checked by CI |
| Test what breaks | Baseline failure shown first; Rolling CI job covers all three packages on PRs to `rolling` |
| Verify before claiming | Rolling checked locally; Jazzy claimed only after the CI run is green |
| Leave in a project only what it chose to carry | Plan and progress files are not committed to the project PR branches (`.agent/` stays out of both PRs) |
| Know whether it works | The cost write-up is the review of the trial |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 Worktree isolation | Yes | Work done in the issue worktree |
| 0019 repo_pin | Yes | PR bases `rolling` then `jazzy` taken from the pin, not the forge default |
| 0013 progress timeline | Yes | Plan and progress entries on the timeline |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| CI branch triggers | Header comment of `ci.yml` | Yes |
| CMake link form | Downstream consumers of the exported `marine_control` target | Yes, PUBLIC linkage keeps them working; check the exported targets still resolve |
| Supported distros | README | Yes (optional) |
| Backport | Jazzy CI untouched | Yes |

## Open Questions

- Matrix in one workflow, or a separate Rolling job? Plan chooses the matrix; owner may prefer separate.
- Should the README state the supported distros?
- Which Rolling runner/OS label should CI use (ubuntu-24.04 with `ros:rolling-ros-core`, or a newer runner)? Plan keeps ubuntu-24.04 since the container supplies the OS.
- `.agent/` files: confirm they stay out of the project PR branches (the plan assumes yes).

## Estimated Scope

Two PRs: part 1 (CMake fix and CI) to `rolling`, part 2 (CMake backport) to `jazzy`.
