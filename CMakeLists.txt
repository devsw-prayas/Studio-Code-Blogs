cmake_minimum_required(VERSION 3.20)
project(Studio-Code-Blogs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE Studio-Code-Blogs_HEADERS  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Public/*.h")
file(GLOB_RECURSE Studio-Code-Blogs_SOURCE   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Private/*.cpp")
file(GLOB_RECURSE Studio-Code-Blogs_INL      CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Public/*.inl")

add_executable(Studio-Code-Blogs
    ${Studio-Code-Blogs_HEADERS}
    ${Studio-Code-Blogs_SOURCE}
    ${Studio-Code-Blogs_INL}
)

target_include_directories(Studio-Code-Blogs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Public
)

target_precompile_headers(Studio-Code-Blogs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/Public/Studio-Code-Blogs.h)

target_compile_options(Studio-Code-Blogs PRIVATE
)
