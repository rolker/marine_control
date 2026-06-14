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

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rcl_interfaces/msg/floating_point_range.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

#include "marine_control/control_server.hpp"
#include "marine_control_interfaces/msg/control_item.hpp"
#include "marine_control_interfaces/msg/control_value.hpp"

using marine_control_interfaces::msg::ControlItem;
using marine_control_interfaces::msg::ControlValue;

class ControlServerTest : public ::testing::Test
{
public:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

protected:
  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("test_device");

    rcl_interfaces::msg::ParameterDescriptor floor_desc;
    floor_desc.description = "floor";
    rcl_interfaces::msg::FloatingPointRange range;
    range.from_value = 0.0;
    range.to_value = 0.95;
    range.step = 0.05;
    floor_desc.floating_point_range.push_back(range);
    node_->declare_parameter<double>("obstacle_prob_min", 0.6, floor_desc);

    node_->declare_parameter<bool>("enabled", true);
    node_->declare_parameter<std::string>("mode", "balanced");

    rcl_interfaces::msg::ParameterDescriptor count_desc;
    count_desc.description = "telemetry";
    count_desc.read_only = true;
    node_->declare_parameter<int>("count", 7, count_desc);
  }

  const ControlItem * find(
    const marine_control_interfaces::msg::ControlSet & set, const std::string & name)
  {
    for (const auto & i : set.items) {
      if (i.name == name) {return &i;}
    }
    return nullptr;
  }

  // Publish `value` to the change topic and spin for up to `seconds`, breaking
  // early when `done()` is satisfied. Returns whatever the loop left in place.
  template<typename DoneFn>
  void drive_change(
    const std::string & name, const std::string & value, DoneFn done,
    double seconds)
  {
    auto pub = node_->create_publisher<ControlValue>(
      "~/control/change", rclcpp::QoS(10).reliable());
    ControlValue v;
    v.name = name;
    v.value = value;
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node_);
    const auto start = node_->now();
    while ((node_->now() - start).seconds() < seconds) {
      pub->publish(v);
      exec.spin_some();
      rclcpp::sleep_for(std::chrono::milliseconds(20));
      if (done()) {break;}
    }
  }

  std::shared_ptr<rclcpp::Node> node_;
};

TEST_F(ControlServerTest, BuildControlSetMapsDescriptors)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("obstacle_prob_min", "prob", "reflex");
  server.bind_parameter("enabled");
  server.bind_parameter("mode");
  server.bind_parameter("count");

  const auto set = server.build_control_set();
  EXPECT_EQ(set.device_name, "test_device");
  ASSERT_EQ(set.items.size(), 4u);

  const auto * f = find(set, "obstacle_prob_min");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->type, ControlItem::TYPE_FLOAT);
  EXPECT_EQ(f->units, "prob");
  EXPECT_EQ(f->group, "reflex");
  EXPECT_EQ(f->description, "floor");
  EXPECT_DOUBLE_EQ(f->min_value, 0.0);
  EXPECT_DOUBLE_EQ(f->max_value, 0.95);
  EXPECT_DOUBLE_EQ(f->step, 0.05);
  EXPECT_FALSE(f->read_only);

  const auto * e = find(set, "enabled");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->type, ControlItem::TYPE_BOOL);
  EXPECT_EQ(e->value, "true");

  const auto * m = find(set, "mode");
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(m->type, ControlItem::TYPE_STRING);
  EXPECT_EQ(m->value, "balanced");

  const auto * c = find(set, "count");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->type, ControlItem::TYPE_INT);
  EXPECT_EQ(c->value, "7");
  EXPECT_TRUE(c->read_only);
}

TEST_F(ControlServerTest, BindUnknownIsIgnored)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("does_not_exist");
  EXPECT_TRUE(server.build_control_set().items.empty());
}

TEST_F(ControlServerTest, ChangeAppliesParameterAndIsValidated)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("obstacle_prob_min");

  drive_change(
    "obstacle_prob_min", "0.75",
    [&] {return node_->get_parameter("obstacle_prob_min").as_double() > 0.7;}, 3.0);
  EXPECT_NEAR(node_->get_parameter("obstacle_prob_min").as_double(), 0.75, 1e-6);
}

// A value outside the descriptor's range is rejected by the node's own
// validation; the parameter is left unchanged (and state is still echoed).
TEST_F(ControlServerTest, OutOfRangeChangeIsRejectedAndLeavesParamUnchanged)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("obstacle_prob_min");

  // 5.0 is well above the 0.95 ceiling. Spin a fixed window (no early-out — we
  // are asserting the value never moves).
  drive_change("obstacle_prob_min", "5.0", [] {return false;}, 1.0);
  EXPECT_NEAR(node_->get_parameter("obstacle_prob_min").as_double(), 0.6, 1e-6);
}

// Malformed numeric input must be rejected, not silently truncated/partial-parsed.
TEST_F(ControlServerTest, MalformedNumericChangeIsRejected)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("count");        // int, default 7
  server.bind_parameter("obstacle_prob_min");  // double, default 0.6

  // "3.5" into an INT must NOT truncate to 3.
  drive_change("count", "3.5", [] {return false;}, 0.6);
  EXPECT_EQ(node_->get_parameter("count").as_int(), 7);

  // trailing garbage into a DOUBLE must be rejected, not partial-parsed to 0.75.
  drive_change("obstacle_prob_min", "0.75abc", [] {return false;}, 0.6);
  EXPECT_NEAR(node_->get_parameter("obstacle_prob_min").as_double(), 0.6, 1e-6);
}

// A change for a name that was never bound is ignored (no throw, no effect).
TEST_F(ControlServerTest, UnboundChangeIsIgnored)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("obstacle_prob_min");

  drive_change("mode", "aggressive", [] {return false;}, 0.6);
  EXPECT_EQ(node_->get_parameter("mode").as_string(), "balanced");
}

// The published echo (build_control_set) renders a double without precision
// inflation — 0.6 is "0.6", not "0.600000".
TEST_F(ControlServerTest, FloatEchoIsRoundTrippable)
{
  marine_control::ControlServer server(node_.get());
  server.bind_parameter("obstacle_prob_min");
  const auto * f = find(server.build_control_set(), "obstacle_prob_min");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->value, "0.6");
}
