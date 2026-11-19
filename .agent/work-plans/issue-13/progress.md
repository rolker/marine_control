---
issue: 13
---

# Issue #13 — Build on ROS 2 Rolling: replace ament_target_dependencies, add a rolling branch and CI

## Issue Review
**Status**: complete
**When**: 2026-11-19 12:00 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)

**Issue**: #13

### Scope Assessment

**Well-scoped?** Yes. The fix is two CMake lines; the CI Rolling job, the backport and the cost write-up are small, separate steps. Two PRs (rolling, then jazzy) matches the stated plan.
**Right repo?** Yes. The change is in marine_control's own CMakeLists.txt and CI workflow.
**Serves**: none (project has no written goals); no project design document.
**Design picture?** No change. Nothing here alters how parts fit.
**Dependencies**: none identified; the issue states this is the first repo in rolker/unh_marine_autonomy#415 and depends on no other project package.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Small steps; step back when they stop converging | OK | Two small PRs, each checkable by CI. |
| Test what breaks | Watch | The Rolling CI job is the test that fails without the fix. Make sure it runs on PRs to `rolling` and uses a Rolling image, and that `colcon test` covers all three packages (marine_control_py too). |
| Verify before claiming | Watch | "Builds on Rolling locally and in CI" needs a Rolling environment; a Rolling image must actually be available for the local build. |
| Leave in a project only what it chose to carry | Watch | Plans and progress entries must not be committed to the project's `rolling` or `jazzy` PR branches; keep them on the workspace side. |
| Know whether it works | OK | The cost write-up on the issue is the explicit review of the trial. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0002 Worktree isolation | Yes | Work is in the issue worktree. |
| 0019 repo_pin | Yes | The PR base is `rolling`, then `jazzy`; it should come from `adapter repo_pin`, not the forge default branch (`jazzy`), or the first PR will target the wrong base. |
| Others | No | |

### Consequences

- `ci.yml` triggers (`push`/`pull_request` to `jazzy` only) and its header comment ("on jazzy") change when the Rolling job is added.
- The backport branch must keep the Jazzy CI as it is; the CI change belongs on `rolling` and should not break the backport.
- README, if it states the supported distro, should say Rolling too.

### Recommendations

- Step 1 ("create a `rolling` branch from `jazzy`") is already done: `origin/rolling` exists. Check it points where intended (jazzy tip) rather than creating it again.
- Add the Rolling CI job so it checks out `rolling` and uses `ros:rolling-ros-core` (container image, `/opt/ros/rolling/setup.bash`); consider a matrix so one workflow serves both branches, and say which you chose when you write up the cost.
- Use `target_link_libraries` with the exported targets (`rclcpp::rclcpp`, `${marine_control_interfaces_TARGETS}` or the typesupport target form for generated interfaces). Confirm the interface-library link works on both Jazzy and Rolling, since the issue says the same form must build on Jazzy.
- Check that the package.xml and the `marine_control_py` tests do not hit other Rolling breakage, so "all three packages build and pass" is verified, not assumed.

### Actions

- [ ] Step 1 ("create a `rolling` branch from `jazzy`") is already done: `origin/rolling` exists. Check it points where intended (jazzy tip) rather than creating it again.
- [ ] Add the Rolling CI job so it checks out `rolling` and uses `ros:rolling-ros-core` (container image, `/opt/ros/rolling/setup.bash`); consider a matrix so one workflow serves both branches, and say which you chose when you write up the cost.
- [ ] Use `target_link_libraries` with the exported targets (`rclcpp::rclcpp`, `${marine_control_interfaces_TARGETS}` or the typesupport target form for generated interfaces). Confirm the interface-library link works on both Jazzy and Rolling, since the issue says the same form must build on Jazzy.
- [ ] Check that the package.xml and the `marine_control_py` tests do not hit other Rolling breakage, so "all three packages build and pass" is verified, not assumed.

## Checkpoint
**Status**: complete
**When**: 2026-11-19 09:27 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: owner
**After**: issue-actions
**Decision**: proceed

Asked "Go on to planning with F1 to F4 handed to the planner as input?" (F1 to F4 are the four Actions of the Issue Review above). The owner answered: "Proceed (Recommended)".

Host note for the planner: the first Action is already checked. `origin/rolling` was created by the host at 68118f2, the tip of `jazzy` at that time.

## Plan Authored
**Status**: complete
**When**: 2026-11-19 09:27 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)
**Plan**: `.agent/work-plans/issue-13/plan.md` at `87ca933`

Two parts: part 1 to rolling (target_link_libraries fix plus a jazzy/rolling CI matrix), part 2 a CMake-only backport to jazzy; Jazzy verified by CI only.
