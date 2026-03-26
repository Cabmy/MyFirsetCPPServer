cmake_minimum_required(VERSION 3.10)
project(MyFirstCPPServer)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O2")

# 查找 MySQL 和 Redis 客户端库
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# 收集 src/ 下所有源文件作为静态库
file(GLOB LIB_SRC src/*.cpp)
add_library(server_lib STATIC ${LIB_SRC})
target_include_directories(server_lib PUBLIC src/include ${MYSQL_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
target_compile_definitions(server_lib PUBLIC USE_REDIS=1)

# 三个可执行目标
add_executable(server test/server.cpp)
target_link_libraries(server server_lib pthread ${MYSQL_LIBRARIES} ${HIREDIS_LIBRARIES})

add_executable(client test/client.cpp)
target_link_libraries(client server_lib pthread)

add_executable(test_stress test/test.cpp)
target_link_libraries(test_stress server_lib pthread)
