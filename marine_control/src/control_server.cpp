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
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
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
// Trim ASCII whitespace from both ends; the operator may send padded values.
std::string trim(const std::string & s)
{
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {return "";}
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

bool parse_bool(const std::string & s, bool & out)
{
  std::string v = trim(s);
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {return std::tolower(c);});
  if (v == "true" || v == "1") {out = true; return true;}
  if (v == "false" || v == "0") {out = false; return true;}
  return false;
}

// Locale-independent full-string parses. std::from_chars never honors the
// global locale (unlike std::stod/std::stoll) and reports how much it consumed,
// so we can reject trailing garbage ("0.75abc") and integer truncation ("3.5"
// for an INT) instead of silently accepting a partial parse — important for a
// control surface on the boat.
bool parse_double(const std::string & s, double & out)
{
  const std::string v = trim(s);
  if (v.empty()) {return false;}
  const char * begin = v.data();
  const char * end = v.data() + v.size();
  const auto res = std::from_chars(begin, end, out);
  return res.ec == std::errc() && res.ptr == end;
}

bool parse_int(const std::string & s, int64_t & out)
{
  const std::string v = trim(s);
  if (v.empty()) {return false;}
  const char * begin = v.data();
  const char * end = v.data() + v.size();
  const auto res = std::from_chars(begin, end, out);
  return res.ec == std::errc() && res.ptr == end;
}

// Locale-independent, shortest round-trippable rendering of a double. Avoids
// std::to_string's fixed 6-decimal precision ("0.6" -> "0.600000") and locale
// decimal separators, so the published echo matches the stored value exactly.
std::string format_double(double value)
{
  std::array<char, 32> buf{};
  const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  if (res.ec != std::errc()) {return std::to_string(value);}
  return std::string(buf.data(), res.ptr);
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

  // The heartbeat timer (publish_state) and the change subscription (on_change)
  // both read bindings_ and touch the parameters interface. Put them in a single
  // mutually-exclusive callback group so they never run concurrently with each
  // other, regardless of the adopter's executor (single- or multi-threaded).
  // This does NOT serialize against bind_parameter(), which the adopter calls
  // from its own thread — see the "bind before spin" contract in the header.
  callback_group_ = node_base->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);

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
  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = callback_group_;
  rclcpp::QoS change_qos(rclcpp::KeepLast(10));
  change_qos.reliable();
  change_qos.durability_volatile();
  change_sub_ = rclcpp::create_subscription<ControlValue>(
    node_parameters, node_topics, options_.change_topic, change_qos,
    std::bind(&ControlServer::on_change, this, std::placeholders::_1), sub_options);

  const double period = options_.heartbeat_period_s > 0.0 ? options_.heartbeat_period_s : 1.0;
  heartbeat_timer_ = rclcpp::create_timer(
    node_base, node_timers, node_clock->get_clock(),
    rclcpp::Duration::from_seconds(period),
    std::bind(&ControlServer::publish_state, this), callback_group_);
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
      item.value = format_double(p.as_double());
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
  // The value before the change, for the audit log below.
  const std::string old_value = values[0].value_to_string();

  rclcpp::Parameter new_param;
  bool parsed = true;
  switch (values[0].get_type()) {
    case rclcpp::ParameterType::PARAMETER_DOUBLE: {
        double d = 0.0;
        if ((parsed = parse_double(msg->value, d))) {new_param = rclcpp::Parameter(name, d);}
        break;
      }
    case rclcpp::ParameterType::PARAMETER_INTEGER: {
        int64_t i = 0;
        if ((parsed = parse_int(msg->value, i))) {new_param = rclcpp::Parameter(name, i);}
        break;
      }
    case rclcpp::ParameterType::PARAMETER_BOOL: {
        bool b = false;
        if ((parsed = parse_bool(msg->value, b))) {new_param = rclcpp::Parameter(name, b);}
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
  if (!parsed) {
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: could not parse change value '%s' for '%s' (expected %s); ignored.",
      msg->value.c_str(), name.c_str(), rclcpp::to_string(values[0].get_type()).c_str());
    return;
  }

  // The node's own on-set validation runs here and may reject the value.
  const auto results = node_parameters_->set_parameters({new_param});
  if (results.empty() || !results[0].successful) {
    const char * reason = results.empty() ? "no result returned" : results[0].reason.c_str();
    RCLCPP_WARN(
      node_logging_->get_logger(),
      "marine_control: change to '%s' rejected: %s", name.c_str(), reason);
  } else {
    // Audit trail: every applied change is logged (old -> new). Groundwork for
    // the e-stop adopter's stricter change-audit requirement (ADR-0003 D8.3).
    RCLCPP_INFO(
      node_logging_->get_logger(),
      "marine_control: '%s' changed %s -> %s", name.c_str(),
      old_value.c_str(), new_param.value_to_string().c_str());
  }
  // Echo current state either way (D1): the operator sees the applied value, or
  // the unchanged value if it was rejected.
  publish_state();
}

}  // namespace marine_control
