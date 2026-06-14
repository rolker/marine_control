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

#include "marine_control/control_server.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/create_publisher.hpp"
#include "rclcpp/create_subscription.hpp"
#include "rclcpp/create_timer.hpp"

namespace marine_control
{

using marine_control_interfaces::msg::ControlItem;
using marine_control_interfaces::msg::ControlSet;
using marine_control_interfaces::msg::ControlValue;

namespace
{
bool parse_bool(const std::string & s, bool & out)
{
  std::string v = s;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {return std::tolower(c);});
  if (v == "true" || v == "1") {out = true; return true;}
  if (v == "false" || v == "0") {out = false; return true;}
  return false;
}
}  // namespace

ControlServer::ControlServer(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
  rclcpp::node_interfaces::NodeParametersInterface::SharedPtr node_parameters,
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics,
  rclcpp::node_interfaces::NodeTimersInterface::SharedPtr node_timers,
  rclcpp::node_interfaces::NodeClockInterface::SharedPtr node_clock,
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging,
  const ControlServerOptions & options)
: node_parameters_(node_parameters),
  node_clock_(node_clock),
  node_logging_(node_logging),
  options_(options)
{
  if (options_.device_name.empty()) {
    options_.device_name = node_base->get_name();
  }

  // D5: state is RELIABLE + VOLATILE with a heartbeat. Never TRANSIENT_LOCAL —
  // a late joiner gets fresh state from the next heartbeat, not a stale latched
  // sample across the bridge.
  rclcpp::QoS state_qos(rclcpp::KeepLast(1));
  state_qos.reliable();
  state_qos.durability_volatile();
  state_pub_ = rclcpp::create_publisher<ControlSet>(
    node_parameters, node_topics, options_.state_topic, state_qos);

  // change: reliable delivery of the request; fire-and-forget at the app level
  // (success is observed via the next state echo, D1). A small queue absorbs
  // bursts of operator edits.
  rclcpp::QoS change_qos(rclcpp::KeepLast(10));
  change_qos.reliable();
  change_qos.durability_volatile();
  change_sub_ = rclcpp::create_subscription<ControlValue>(
    node_parameters, node_topics, options_.change_topic, change_qos,
    std::bind(&ControlServer::on_change, this, std::placeholders::_1));

  const double period = options_.heartbeat_period_s > 0.0 ? options_.heartbeat_period_s : 1.0;
  heartbeat_timer_ = rclcpp::create_timer(
    node_base, node_timers, node_clock->get_clock(),
    rclcpp::Duration::from_seconds(period),
    std::bind(&ControlServer::publish_state, this));
}

void ControlServer::bind_parameter(
  const std::string & name, const std::string & units, const std::string & group)
{
  if (!node_parameters_->has_parameter(name)) {
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: bind_parameter('%s') ignored — parameter not declared. "
      "Declare it (with a descriptor) before binding.", name.c_str());
    return;
  }
  bindings_[name] = Binding{units, group};
}

bool ControlServer::make_item(
  const std::string & name, const Binding & binding, ControlItem & item) const
{
  if (!node_parameters_->has_parameter(name)) {
    return false;
  }
  const auto descriptors = node_parameters_->describe_parameters({name});
  const auto values = node_parameters_->get_parameters({name});
  if (descriptors.empty() || values.empty()) {
    return false;
  }
  const auto & d = descriptors[0];
  const auto & p = values[0];

  item.name = name;
  item.label = name;  // ParameterDescriptor has no label; fall back to the name.
  item.description = d.description;
  item.read_only = d.read_only;
  item.units = binding.units;
  item.group = binding.group;

  switch (p.get_type()) {
    case rclcpp::ParameterType::PARAMETER_DOUBLE:
      item.type = ControlItem::TYPE_FLOAT;
      item.value = std::to_string(p.as_double());
      if (!d.floating_point_range.empty()) {
        item.min_value = d.floating_point_range[0].from_value;
        item.max_value = d.floating_point_range[0].to_value;
        item.step = d.floating_point_range[0].step;
      }
      break;
    case rclcpp::ParameterType::PARAMETER_INTEGER:
      item.type = ControlItem::TYPE_INT;
      item.value = std::to_string(p.as_int());
      if (!d.integer_range.empty()) {
        item.min_value = static_cast<double>(d.integer_range[0].from_value);
        item.max_value = static_cast<double>(d.integer_range[0].to_value);
        item.step = static_cast<double>(d.integer_range[0].step);
      }
      break;
    case rclcpp::ParameterType::PARAMETER_BOOL:
      item.type = ControlItem::TYPE_BOOL;
      item.value = p.as_bool() ? "true" : "false";
      break;
    case rclcpp::ParameterType::PARAMETER_STRING:
      item.type = ControlItem::TYPE_STRING;
      item.value = p.as_string();
      break;
    default:
      // Arrays and unset types are not (yet) renderable as a single control.
      RCLCPP_WARN_ONCE(
        node_logging_->get_logger(),
        "marine_control: parameter '%s' has an unsupported type for a control; skipping.",
        name.c_str());
      return false;
  }
  return true;
}

ControlSet ControlServer::build_control_set() const
{
  ControlSet set;
  set.header.stamp = node_clock_->get_clock()->now();
  set.device_name = options_.device_name;
  for (const auto & [name, binding] : bindings_) {
    ControlItem item;
    if (make_item(name, binding, item)) {
      set.items.push_back(item);
    }
  }
  return set;
}

void ControlServer::publish_state()
{
  if (state_pub_) {
    state_pub_->publish(build_control_set());
  }
}

void ControlServer::on_change(const ControlValue::SharedPtr msg)
{
  const std::string & name = msg->name;
  if (bindings_.find(name) == bindings_.end() || !node_parameters_->has_parameter(name)) {
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: change for unbound/unknown control '%s' ignored.", name.c_str());
    return;
  }

  const auto values = node_parameters_->get_parameters({name});
  if (values.empty()) {
    return;
  }

  rclcpp::Parameter new_param;
  try {
    switch (values[0].get_type()) {
      case rclcpp::ParameterType::PARAMETER_DOUBLE:
        new_param = rclcpp::Parameter(name, std::stod(msg->value));
        break;
      case rclcpp::ParameterType::PARAMETER_INTEGER:
        new_param = rclcpp::Parameter(name, static_cast<int64_t>(std::stoll(msg->value)));
        break;
      case rclcpp::ParameterType::PARAMETER_BOOL: {
          bool b = false;
          if (!parse_bool(msg->value, b)) {
            throw std::invalid_argument("expected true/false");
          }
          new_param = rclcpp::Parameter(name, b);
          break;
        }
      case rclcpp::ParameterType::PARAMETER_STRING:
        new_param = rclcpp::Parameter(name, msg->value);
        break;
      default:
        RCLCPP_WARN(
          node_logging_->get_logger(),
          "marine_control: control '%s' has an unsupported type; change ignored.", name.c_str());
        return;
    }
  } catch (const std::exception & e) {
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: could not parse change value '%s' for '%s': %s",
      msg->value.c_str(), name.c_str(), e.what());
    return;
  }

  // The node's own on-set validation runs here and may reject the value.
  const auto results = node_parameters_->set_parameters({new_param});
  if (!results.empty() && !results[0].successful) {
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: change to '%s' rejected: %s", name.c_str(), results[0].reason.c_str());
  }
  // Echo current state either way (D1): the operator sees the applied value, or
  // the unchanged value if it was rejected.
  publish_state();
}

}  // namespace marine_control
