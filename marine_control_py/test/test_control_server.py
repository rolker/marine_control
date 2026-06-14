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

"""Tests for marine_control_py.ControlServer (mirrors the C++ gtest suite)."""

from marine_control_interfaces.msg import ControlItem, ControlValue

from marine_control_py import ControlServer

import pytest

from rcl_interfaces.msg import FloatingPointRange, IntegerRange, ParameterDescriptor

import rclpy
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy


@pytest.fixture(scope='module', autouse=True)
def rclpy_session():
    """Init/shutdown rclpy once for the whole module."""
    rclpy.init()
    yield
    rclpy.shutdown()


@pytest.fixture
def node():
    """Build a device node with one of each control type (range-bounded float + int)."""
    n = Node('test_device')
    n.declare_parameter(
        'obstacle_prob_min', 0.6,
        ParameterDescriptor(
            description='floor',
            floating_point_range=[
                FloatingPointRange(from_value=0.0, to_value=0.95, step=0.05)]))
    n.declare_parameter('enabled', True)
    n.declare_parameter('mode', 'balanced')
    n.declare_parameter(
        'count', 7, ParameterDescriptor(description='telemetry', read_only=True))
    n.declare_parameter(
        'gain', 5,
        ParameterDescriptor(
            description='gain', integer_range=[IntegerRange(from_value=0, to_value=10, step=1)]))
    yield n
    n.destroy_node()


def _find(control_set, name):
    """Return the ControlItem with the given name, or None."""
    for item in control_set.items:
        if item.name == name:
            return item
    return None


def _drive_change(node, name, value, predicate, max_iters=40):
    """Publish a change and spin until predicate() or max_iters elapse."""
    pub = node.create_publisher(
        ControlValue, '~/control/change',
        QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE))
    msg = ControlValue()
    msg.name = name
    msg.value = value
    executor = SingleThreadedExecutor()
    executor.add_node(node)
    try:
        for _ in range(max_iters):
            pub.publish(msg)
            executor.spin_once(timeout_sec=0.05)
            if predicate():
                break
    finally:
        executor.remove_node(node)


def test_build_control_set_maps_descriptors(node):
    """Each bound parameter becomes a ControlItem carrying its descriptor metadata."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min', 'prob', 'reflex')
    server.bind_parameter('enabled')
    server.bind_parameter('mode')
    server.bind_parameter('count')

    control_set = server.build_control_set()
    assert control_set.device_name == 'test_device'

    f = _find(control_set, 'obstacle_prob_min')
    assert f is not None
    assert f.type == ControlItem.TYPE_FLOAT
    assert f.units == 'prob'
    assert f.group == 'reflex'
    assert f.description == 'floor'
    assert f.min_value == 0.0
    assert f.max_value == 0.95
    assert abs(f.step - 0.05) < 1e-9
    assert not f.read_only

    e = _find(control_set, 'enabled')
    assert e is not None
    assert e.type == ControlItem.TYPE_BOOL
    assert e.value == 'true'

    m = _find(control_set, 'mode')
    assert m is not None
    assert m.type == ControlItem.TYPE_STRING
    assert m.value == 'balanced'

    c = _find(control_set, 'count')
    assert c is not None
    assert c.type == ControlItem.TYPE_INT
    assert c.value == '7'
    assert c.read_only


def test_bind_unknown_is_ignored(node):
    """Binding an undeclared parameter is a no-op."""
    server = ControlServer(node)
    server.bind_parameter('does_not_exist')
    assert server.build_control_set().items == []


def test_change_applies_and_is_validated(node):
    """An in-range change is applied to the bound parameter."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min')
    _drive_change(
        node, 'obstacle_prob_min', '0.75',
        lambda: node.get_parameter('obstacle_prob_min').value > 0.7)
    assert abs(node.get_parameter('obstacle_prob_min').value - 0.75) < 1e-6


def test_out_of_range_change_is_rejected(node):
    """A value past the descriptor range is rejected; the parameter is unchanged."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min')
    _drive_change(node, 'obstacle_prob_min', '5.0', lambda: False, max_iters=12)
    assert abs(node.get_parameter('obstacle_prob_min').value - 0.6) < 1e-6


def test_malformed_numeric_change_is_rejected(node):
    """Malformed numeric input is rejected, not truncated or partial-parsed."""
    server = ControlServer(node)
    server.bind_parameter('gain')
    server.bind_parameter('obstacle_prob_min')

    # "3.5" into an INT must NOT truncate to 3.
    _drive_change(node, 'gain', '3.5', lambda: False, max_iters=12)
    assert node.get_parameter('gain').value == 5

    # trailing garbage into a DOUBLE must be rejected, not parsed to 0.75.
    _drive_change(node, 'obstacle_prob_min', '0.75abc', lambda: False, max_iters=12)
    assert abs(node.get_parameter('obstacle_prob_min').value - 0.6) < 1e-6


def test_off_step_change_is_rejected(node):
    """An in-range but off-step value is rejected by rclpy (declare step=0.0 to allow any)."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min')  # step 0.05
    # 0.63 is inside [0, 0.95] but not a multiple of 0.05 -> rclpy rejects it.
    _drive_change(node, 'obstacle_prob_min', '0.63', lambda: False, max_iters=12)
    assert abs(node.get_parameter('obstacle_prob_min').value - 0.6) < 1e-6


def test_read_only_change_is_rejected(node):
    """A change to a read-only control is rejected; the parameter is unchanged."""
    server = ControlServer(node)
    server.bind_parameter('count')  # declared read_only
    _drive_change(node, 'count', '42', lambda: False, max_iters=12)
    assert node.get_parameter('count').value == 7


def test_unbound_change_is_ignored(node):
    """A change for a name that was never bound has no effect."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min')
    _drive_change(node, 'mode', 'aggressive', lambda: False, max_iters=12)
    assert node.get_parameter('mode').value == 'balanced'


def test_float_echo_is_round_trippable(node):
    """The published float echo is shortest round-trippable ('0.6', not '0.600000')."""
    server = ControlServer(node)
    server.bind_parameter('obstacle_prob_min')
    f = _find(server.build_control_set(), 'obstacle_prob_min')
    assert f is not None
    assert f.value == '0.6'
