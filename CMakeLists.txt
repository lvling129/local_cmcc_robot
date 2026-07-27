cmake_minimum_required(VERSION 3.10...3.20)
project(robot_avvtn VERSION 1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")

set(CMAKE_BUILD_TYPE "Debug")
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  add_definitions(-DDEBUG)
else()
  add_definitions(-DNDEBUG)
endif()

# 使能 AddressSanitizer
# if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#     add_compile_options(-fsanitize=address -g)
#     add_link_options(-fsanitize=address)
# endif()

# 查找ROS2依赖
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# 查找libcurl
find_package(CURL REQUIRED)

add_subdirectory(src)

# 链接库到目标
target_link_libraries(${PROJECT_NAME} CURL::libcurl)

# 添加ROS2依赖
ament_target_dependencies(${PROJECT_NAME} 
  rclcpp 
  std_msgs
)