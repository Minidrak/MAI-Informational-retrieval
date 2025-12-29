cmake_minimum_required(VERSION 3.14)
project(SearchEngine VERSION 1.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)

add_library(search_lib STATIC
    src/index_format.cpp
    src/tokenizer.cpp
    src/indexer.cpp
    src/query_parser.cpp
    src/searcher.cpp
)

target_include_directories(search_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)


if(TARGET mongo::bsoncxx_static)
    target_link_libraries(search_lib PUBLIC mongo::bsoncxx_static)
elseif(TARGET mongo::bsoncxx_shared)
    target_link_libraries(search_lib PUBLIC mongo::bsoncxx_shared)
else()
    message(FATAL_ERROR "Neither mongo::bsoncxx_static nor mongo::bsoncxx_shared found")
endif()

if(TARGET mongo::mongocxx_static)
    target_link_libraries(search_lib PUBLIC mongo::mongocxx_static)
elseif(TARGET mongo::mongocxx_shared)
    target_link_libraries(search_lib PUBLIC mongo::mongocxx_shared)
else()
    message(FATAL_ERROR "Neither mongo::mongocxx_static nor mongo::mongocxx_shared found")
endif()



add_executable(indexer src/main_indexer.cpp)
target_link_libraries(indexer PRIVATE search_lib)

add_executable(search src/main_search.cpp)
target_link_libraries(search PRIVATE search_lib)

