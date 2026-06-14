# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Boat-side device-control server (Python sibling of marine_control::ControlServer).

Mirrors the C++ contract (ADR-0003 D4/D5/D6): a node declares parameters with
descriptors, binds them with bind_parameter(), and this server

  - publishes a marine_control_interfaces/ControlSet (state, device -> operator)
    on a periodic heartbeat with the bridge-correct QoS (D5: RELIABLE + VOLATILE,
    never TRANSIENT_LOCAL), and
  - applies inbound marine_control_interfaces/ControlValue (change,
    operator -> device) by setting the bound parameter -- whose own on-set
    validation the node already owns -- then re-publishes state so the echo
    confirms the change (D1, fire-and-forget).

No Qt: the operator station renders the published ControlSet.

Threading contract: the heartbeat timer and the change subscription share a
dedicated mutually-exclusive callback group, so they never run concurrently with
each other even under a multi-threaded executor. Call bind_parameter() during
setup, before the node starts spinning -- it mutates the binding table without
locking and is not safe to call concurrently with the running callbacks.
Likewise, drop the server only when the node is not spinning; the timer/sub are
torn down with it, and an in-flight callback on another thread would race.

Step note: a parameter declared with a non-zero descriptor step makes rclpy
reject in-range-but-off-step changes (e.g. 0.63 against step 0.05) rather than
snapping them. Declare step=0.0 for controls that should accept any in-range
value, or have the operator UI snap to the step before sending.

Lifecycle note: the publisher, subscription, and timer are created on
construction, so the server is active as soon as it is constructed. Construct it
when you want it active. A safety-critical adopter (ADR-0003 D8.3) that needs
confirmation/audit beyond fire-and-forget layers that on top of this mechanism.
"""

import copy
from dataclasses import dataclass

from marine_control_interfaces.msg import ControlItem, ControlSet, ControlValue

from rclpy.callback_groups import MutuallyExclusiveCallbackGroup
from rclpy.parameter import Parameter
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy


@dataclass
class ControlServerOptions:
    """Configuration for a ControlServer (mirrors the C++ ControlServerOptions)."""

    # UI title for the panel. Empty -> the node name is used.
    device_name: str = ''
    # state (device->operator) and change (operator->device) topics. Relative
    # names resolve under the node namespace; the platform bridge config wires
    # state boat->operator and change operator->boat (ADR-0003 D7).
    state_topic: str = '~/control/state'
    change_topic: str = '~/control/change'
    # Heartbeat republish period (s) for state. A late joiner gets fresh state
    # from the next heartbeat rather than a stale latched sample (D5).
    heartbeat_period_s: float = 1.0


def _parse_bool(text):
    """Parse a bool from true/false/1/0 (case-insensitive); raise ValueError otherwise."""
    value = text.strip().lower()
    if value in ('true', '1'):
        return True
    if value in ('false', '0'):
        return False
    raise ValueError("expected true/false, got '{}'".format(text))


class ControlServer:
    """Publishes a node's bound parameters as a ControlSet and applies changes."""

    def __init__(self, node, options=None):
        """Bind to an rclpy node (Node or LifecycleNode); it must outlive the server."""
        self._node = node
        # Copy so resolving device_name below never mutates a caller-shared
        # options instance (the C++ sibling takes options by value).
        self._options = copy.copy(options) if options is not None else ControlServerOptions()
        if not self._options.device_name:
            self._options.device_name = node.get_name()
        self._logger = node.get_logger()
        # name -> (units, group). Mutated only by bind_parameter (see contract).
        self._bindings = {}
        # Names already warned about as unsupported, so build_control_set does
        # not spam the log once per heartbeat (mirrors C++ RCLCPP_WARN_ONCE).
        self._unsupported_warned = set()

        # The heartbeat timer (publish_state) and the change subscription
        # (on_change) both read _bindings and touch parameters; a single
        # mutually-exclusive group serializes them regardless of the executor.
        self._callback_group = MutuallyExclusiveCallbackGroup()

        # D5: state is RELIABLE + VOLATILE with a heartbeat. Never
        # TRANSIENT_LOCAL -- a late joiner gets fresh state from the next
        # heartbeat, not a stale latched sample across the bridge.
        state_qos = QoSProfile(
            depth=1,
            history=HistoryPolicy.KEEP_LAST,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE)
        self._state_pub = node.create_publisher(
            ControlSet, self._options.state_topic, state_qos)

        # change: reliable delivery of the request; fire-and-forget at the app
        # level (success is observed via the next state echo, D1).
        change_qos = QoSProfile(
            depth=10,
            history=HistoryPolicy.KEEP_LAST,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE)
        self._change_sub = node.create_subscription(
            ControlValue, self._options.change_topic, self._on_change, change_qos,
            callback_group=self._callback_group)

        period = self._options.heartbeat_period_s
        if period <= 0.0:
            period = 1.0
        self._timer = node.create_timer(
            period, self.publish_state, callback_group=self._callback_group)

    def bind_parameter(self, name, units='', group=''):
        """Expose an already-declared parameter as a control (descriptor populates the item)."""
        if not self._node.has_parameter(name):
            self._logger.warn(
                "marine_control: bind_parameter('{}') ignored -- parameter not "
                'declared. Declare it (with a descriptor) before binding.'.format(name))
            return
        self._bindings[name] = (units, group)

    def build_control_set(self):
        """Assemble the current ControlSet from the bound parameters (stamp = now)."""
        msg = ControlSet()
        msg.header.stamp = self._node.get_clock().now().to_msg()
        msg.device_name = self._options.device_name
        for name in sorted(self._bindings):
            item = self._make_item(name)
            if item is not None:
                msg.items.append(item)
        return msg

    def publish_state(self):
        """Publish the current ControlSet immediately (also called on the heartbeat)."""
        self._state_pub.publish(self.build_control_set())

    def _make_item(self, name):
        """Translate one declared parameter into a ControlItem, or None if not renderable."""
        if not self._node.has_parameter(name):
            return None
        descriptor = self._node.describe_parameter(name)
        param = self._node.get_parameter(name)
        units, group = self._bindings[name]

        item = ControlItem()
        item.name = name
        item.label = name  # ParameterDescriptor has no label; fall back to the name.
        item.description = descriptor.description
        item.read_only = descriptor.read_only
        item.units = units
        item.group = group

        ptype = param.type_
        if ptype == Parameter.Type.DOUBLE:
            item.type = ControlItem.TYPE_FLOAT
            item.value = repr(float(param.value))  # shortest round-trippable
            if descriptor.floating_point_range:
                rng = descriptor.floating_point_range[0]
                item.min_value = rng.from_value
                item.max_value = rng.to_value
                item.step = rng.step
        elif ptype == Parameter.Type.INTEGER:
            item.type = ControlItem.TYPE_INT
            item.value = str(int(param.value))
            if descriptor.integer_range:
                rng = descriptor.integer_range[0]
                item.min_value = float(rng.from_value)
                item.max_value = float(rng.to_value)
                item.step = float(rng.step)
        elif ptype == Parameter.Type.BOOL:
            item.type = ControlItem.TYPE_BOOL
            item.value = 'true' if param.value else 'false'
        elif ptype == Parameter.Type.STRING:
            item.type = ControlItem.TYPE_STRING
            item.value = param.value
        else:
            # Arrays and unset types are not (yet) renderable as a single
            # control. Warn once per name -- build_control_set runs every
            # heartbeat, so an unconditional warn would spam the log.
            if name not in self._unsupported_warned:
                self._unsupported_warned.add(name)
                self._logger.warn(
                    "marine_control: parameter '{}' has an unsupported type for "
                    'a control; skipping.'.format(name))
            return None
        return item

    def _on_change(self, msg):
        """Apply one ControlValue to the bound parameter, then echo state (D1)."""
        name = msg.name
        if name not in self._bindings or not self._node.has_parameter(name):
            self._logger.warn(
                "marine_control: change for unbound/unknown control '{}' "
                'ignored.'.format(name))
            return

        current = self._node.get_parameter(name)
        old_value = str(current.value)
        ptype = current.type_

        # Python int()/float() already reject trailing garbage ("0.75abc") and
        # integer truncation ("3.5") by raising ValueError -- the parse hardening
        # the C++ sibling needed std::from_chars for is free here.
        try:
            if ptype == Parameter.Type.DOUBLE:
                new_param = Parameter(name, Parameter.Type.DOUBLE, float(msg.value.strip()))
            elif ptype == Parameter.Type.INTEGER:
                new_param = Parameter(name, Parameter.Type.INTEGER, int(msg.value.strip()))
            elif ptype == Parameter.Type.BOOL:
                new_param = Parameter(name, Parameter.Type.BOOL, _parse_bool(msg.value))
            elif ptype == Parameter.Type.STRING:
                new_param = Parameter(name, Parameter.Type.STRING, msg.value)
            else:
                self._logger.warn(
                    "marine_control: control '{}' has an unsupported type; "
                    'change ignored.'.format(name))
                return
        except ValueError:
            self._logger.warn(
                "marine_control: could not parse change value '{}' for '{}' "
                '(expected {}); ignored.'.format(msg.value, name, ptype.name))
            return

        # The node's own on-set validation runs here and may reject the value.
        results = self._node.set_parameters([new_param])
        if not results or not results[0].successful:
            reason = results[0].reason if results else 'no result returned'
            self._logger.warn(
                "marine_control: change to '{}' rejected: {}".format(name, reason))
        else:
            # Audit trail: every applied change is logged old -> new. Groundwork
            # for the e-stop adopter's stricter change-audit (ADR-0003 D8.3).
            self._logger.info(
                "marine_control: '{}' changed {} -> {}".format(
                    name, old_value, new_param.value))
        # Echo current state either way (D1): the operator sees the applied
        # value, or the unchanged value if it was rejected.
        self.publish_state()
