cmake_minimum_required(VERSION 3.16)
project(rgbtocmyk CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED lcms2)
pkg_check_modules(JPEG  REQUIRED libjpeg)

add_executable(rgbtocmyk
    src/main.cpp
    src/converter.cpp
    src/stats.cpp
)

target_compile_options(rgbtocmyk PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_include_directories(rgbtocmyk PRIVATE
    ${LCMS2_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
)

target_link_libraries(rgbtocmyk PRIVATE
    ${LCMS2_LIBRARIES}
    ${JPEG_LIBRARIES}
)

target_link_options(rgbtocmyk PRIVATE
    ${LCMS2_LDFLAGS}
    ${JPEG_LDFLAGS}
)
