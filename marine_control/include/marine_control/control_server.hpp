// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Boat-side device-control server (ADR-0003 D4/D5/D6).
//
// A node creates a ControlServer, binds parameters it has already declared
// (with descriptors), and the server:
//   - publishes a marine_control_interfaces/ControlSet (state, device->operator)
//     on a periodic heartbeat with the bridge-correct QoS (D5: RELIABLE +
//     VOLATILE, never TRANSIENT_LOCAL), and
//   - applies inbound marine_control_interfaces/ControlValue (change,
//     operator->device) by setting the bound parameter — whose own on-set
//     validation the node already owns — then re-publishes state so the echo
//     confirms the change (D1, fire-and-forget).
//
// No Qt / rqt: the operator station renders the published ControlSet. The
// server is built on the node *interfaces* so it works with both rclcpp::Node
// and rclcpp_lifecycle::LifecycleNode (the reflex adopter is a LifecycleNode).
// The node must outlive the ControlServer.
//
// Threading contract: the heartbeat timer and the change subscription run in a
// dedicated mutually-exclusive callback group, so they never execute
// concurrently with each other even under a multi-threaded executor. Call
// bind_parameter() during setup, *before* the node starts spinning — it mutates
// the binding table without locking and is not safe to call concurrently with
// the running callbacks. Likewise, destroy the server only when the node is not
// spinning (the destructor tears down the timer/sub, but an in-flight callback
// on another thread would race).
//
// Lifecycle note: the publisher, subscription, and heartbeat timer are created
// in the constructor, so the server is active as soon as it is constructed — it
// is NOT gated on a LifecycleNode's active state. For lifecycle gating,
// construct the ControlServer in on_activate() and reset it in on_deactivate().
// A safety-critical adopter (ADR-0003 D8.3) that needs confirmation/audit
// beyond the fire-and-forget change semantics layers that on top of this
// mechanism; see issue #3 follow-ups.
#ifndef MARINE_CONTROL__CONTROL_SERVER_HPP_
#define MARINE_CONTROL__CONTROL_SERVER_HPP_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "marine_control_interfaces/msg/control_item.hpp"
#include "marine_control_interfaces/msg/control_set.hpp"
#include "marine_control_interfaces/msg/control_value.hpp"

namespace marine_control
{

struct ControlServerOptions
{
  // UI title for the panel. Empty -> the node name is used.
  std::string device_name;
  // state (device->operator) and change (operator->device) topics. Relative
  // names resolve under the node namespace; the platform bridge config wires
  // state boat->operator and change operator->boat (ADR-0003 D7).
  std::string state_topic = "~/control/state";
  std::string change_topic = "~/control/change";
  // Heartbeat republish period for state. A late joiner gets fresh state from
  // the next heartbeat rather than a stale latched sample (D5).
  double heartbeat_period_s = 1.0;
};

class ControlServer
{
public:
  // Convenience constructor: pass any node-like object (rclcpp::Node or
  // rclcpp_lifecycle::LifecycleNode); it is decomposed into the interfaces used
  // below. The node must outlive this server.
  template<typename NodeT>
  explicit ControlServer(
    NodeT * node, const ControlServerOptions & options = ControlServerOptions())
  : ControlServer(
      node->get_node_base_interface(),
      node->get_node_parameters_interface(),
      node->get_node_topics_interface(),
      node->get_node_timers_interface(),
      node->get_node_clock_interface(),
      node->get_node_logging_interface(),
      options)
  {
  }

  // Interface constructor. Prefer the convenience constructor above.
  ControlServer(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
    rclcpp::node_interfaces::NodeParametersInterface::SharedPtr node_parameters,
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics,
    rclcpp::node_interfaces::NodeTimersInterface::SharedPtr node_timers,
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr node_clock,
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging,
    const ControlServerOptions & options = ControlServerOptions());

  // Expose an already-declared parameter as a control. The parameter's current
  // value and ParameterDescriptor (type, range, step, description, read_only)
  // populate the ControlItem; `units` and `group` have no ParameterDescriptor
  // equivalent, so the adopter supplies them here (D6). Re-binding the same name
  // updates its units/group. Unknown (undeclared) names are ignored with a warn.
  void bind_parameter(
    const std::string & name, const std::string & units = "", const std::string & group = "");

  // Build the current ControlSet from the bound parameters (testable without a
  // running executor). header.stamp is set to the node clock's "now".
  marine_control_interfaces::msg::ControlSet build_control_set() const;

  // Publish the current ControlSet immediately (in addition to the heartbeat).
  void publish_state();

private:
  struct Binding
  {
    std::string units;
    std::string group;
  };

  // Translate one declared parameter (by name) into a ControlItem, or return
  // false if it is not declared.
  bool make_item(
    const std::string & name, const Binding & binding,
    marine_control_interfaces::msg::ControlItem & item) const;

  void on_change(const marine_control_interfaces::msg::ControlValue::SharedPtr msg);

  rclcpp::node_interfaces::NodeParametersInterface::SharedPtr node_parameters_;
  rclcpp::node_interfaces::NodeClockInterface::SharedPtr node_clock_;
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging_;

  ControlServerOptions options_;
  // Insertion-ordered would be nicer for UI stability; std::map keeps it
  // deterministic (alphabetical) which is good enough and stable.
  std::map<std::string, Binding> bindings_;

  // Dedicated mutually-exclusive group: the timer and change sub never run
  // concurrently with each other (see the threading contract above).
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  rclcpp::Publisher<marine_control_interfaces::msg::ControlSet>::SharedPtr state_pub_;
  rclcpp::Subscription<marine_control_interfaces::msg::ControlValue>::SharedPtr change_sub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

}  // namespace marine_control

#endif  // MARINE_CONTROL__CONTROL_SERVER_HPP_
