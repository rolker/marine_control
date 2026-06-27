cmake_minimum_required(VERSION 3.8)
project(marine_control)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(marine_control_interfaces REQUIRED)

add_library(${PROJECT_NAME} src/control_server.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
# Build position-independent so this (static) archive can be linked into shared
# objects — ROS 2 plugins (.so) such as nav2 controller/costmap-layer adopters,
# not just executables. Without this, linking it into a plugin fails with
# "relocation R_X86_64_PC32 ... can not be used when making a shared object".
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
ament_target_dependencies(${PROJECT_NAME} rclcpp marine_control_interfaces)

install(DIRECTORY include/ DESTINATION include)
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp marine_control_interfaces)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_control_server test/test_control_server.cpp)
  target_link_libraries(test_control_server ${PROJECT_NAME})
  ament_target_dependencies(test_control_server rclcpp marine_control_interfaces)
endif()

ament_package()
