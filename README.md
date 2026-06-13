# marine_control

Bridgeable device control protocol & libraries for marine robots — a generic,
**bridge-correct** mechanism for viewing and changing device/node settings from
the topside station over `udp_bridge`.

`udp_bridge` carries **only pub/sub topics, not services**, so ROS 2 parameters
and `rqt_reconfigure` are invisible topside. This package set expresses device
control as a **topic-based, bidirectional** contract instead: a `state` topic
(device→operator) and a `change` topic (operator→device).

Design of record: **ADR-0003** in
[`rolker/unh_marine_autonomy`](https://github.com/rolker/unh_marine_autonomy/blob/jazzy/docs/decisions/0003-bridgeable-device-control.md)
(umbrella [#140](https://github.com/rolker/unh_marine_autonomy/issues/140)).

This repository is intentionally **Qt-free** so the boat-side stack never pulls
a UI dependency. The rqt widgets and plugin live in `rqt_operator_tools`.

## Packages

| Package | Role |
|---------|------|
| `marine_control_interfaces` | `ControlSet` / `ControlItem` / `ControlValue` messages |
| `marine_control` (planned) | device-side helper library (publishes `ControlSet` from parameter descriptors, handles `change`, owns QoS) |
