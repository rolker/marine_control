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

## Plan Review
**Status**: complete
**When**: 2026-11-19 09:28 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: ready

**Issue**: #13 — Build on ROS 2 Rolling: replace ament_target_dependencies, add a rolling branch and CI
**Plan**: `.agent/work-plans/issue-13/plan.md` at `87ca933`
**Branch**: `feature/issue-13`

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Two small PRs (CMake + CI to `rolling`, CMake-only backport to `jazzy`); three files at most. |
| Issue alignment | Good | Covers all four Work items and the three Done-when lines. All four Issue Review actions are addressed (F1 rolling branch already at 68118f2; F2 matrix with `ros:rolling-ros-core`; F3 `target_link_libraries` with exported targets plus a typesupport fallback; F4 step 3 checks package.xml / py tests). Serves: "None (project has no written goals); no project design document. Part of rolker/unh_marine_autonomy#415" — acceptable, project keeps no goals. |
| File targeting | Good | `marine_control/CMakeLists.txt` lines 25 and 44 are the only `ament_target_dependencies` calls; `marine_control_interfaces/CMakeLists.txt` uses only `rosidl_generate_interfaces` and needs no change. |
| Consequences | Good | ci.yml header, downstream consumers (PUBLIC linkage + `ament_export_dependencies` kept), README, Jazzy CI untouched. |
| Principle alignment | Needs work | Baseline failure first (Test what breaks), Jazzy claimed only after CI (Verify before claiming). The `.agent/` exclusion is asserted but has no mechanism (finding 1). |
| ADR compliance | Good | 0002, 0013, 0019 (PR base from `repo_pin`) addressed. |
| ROS conventions | Good | Modern target linking is the recommended Rolling migration. Checked on /opt/ros/rolling: `ament_cmake_export_targets-extras.cmake` appends to `<pkg>_TARGETS`, so `${marine_control_interfaces_TARGETS}` resolves for a rosidl package found by `find_package` (finding 3). |

### Findings

1. **[Principle alignment]** (Medium) — The plan says `.agent/` stays out of both project PRs ("Leave in a project only what it chose to carry") but names no mechanism, and `feature/issue-13` already differs from `origin/rolling` only by four `.agent/` commits (c53567c, 212c2b5, 87ca933, ae111ec); earlier feature branches on origin carry no `.agent/`. A PR opened from this branch as is would carry the plan and progress files. Resolution: state how the PR head is kept clean (e.g. the implementation commits go on a PR branch cut from `origin/rolling` without the `.agent/` commits, or the host's publish step strips them), and do the same for the part 2 backport branch (cherry-pick only the CMake commit, as step 7 already says).
2. **[Approach]** (Low) — Step 4 says "one workflow then serves both branches (… a `jazzy` push tests Jazzy)", but step 7 leaves `jazzy`'s `ci.yml` unchanged, so the matrix lives only on `rolling`. The real effect is that PRs to `rolling` run both a Jazzy and a Rolling leg — useful early evidence that the same CMake builds on Jazzy before the backport, but a standing cost once `rolling` diverges with Rolling-only code. Resolution: correct the wording, and record in the cost write-up whether the Jazzy leg on `rolling` is kept or dropped; the `jazzy` entries in `rolling`'s branch triggers are then moot and can be omitted or explained in the header comment.
3. **[ROS conventions]** (Info) — Rolling also installs `rosidl_cmake_aggregate_target-extras.cmake`, giving interface packages an aggregate `<pkg>::<pkg>` target (seen for `std_msgs`). Do not use `marine_control_interfaces::marine_control_interfaces`: the same commit must build on Jazzy for the backport. `${marine_control_interfaces_TARGETS}` (or the `__rosidl_typesupport_cpp` fallback) is the form that works on both, as the plan already chooses.

### Summary

Sound, minimal plan that answers the issue and every Issue Review action; the CMake approach checks out against the installed Rolling config. Ready for implementation once the `.agent/` exclusion has a concrete mechanism.

### Recommended Actions

- [ ] Name how the part 1 and part 2 PR branches are kept free of `.agent/` commits (finding 1).
- [ ] Correct step 4's "a `jazzy` push tests Jazzy" wording and decide whether the Jazzy leg stays on `rolling` (finding 2).
- [ ] Keep `${marine_control_interfaces_TARGETS}` rather than Rolling's aggregate target (finding 3).
