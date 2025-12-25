cmake_minimum_required(VERSION 3.14)
project(SearchEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -pthread")

find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)

add_library(search_lib STATIC
    src/index_format.cpp
    src/tokenizer.cpp
    src/indexer.cpp
    src/query_parser.cpp
    src/searcher.cpp
    src/web_server.cpp
)

target_include_directories(search_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(search_lib PUBLIC
    mongo::mongocxx_shared
    mongo::bsoncxx_shared
    pthread
)

add_executable(indexer src/main_indexer.cpp)
target_link_libraries(indexer PRIVATE search_lib)

add_executable(search src/main_search.cpp)
target_link_libraries(search PRIVATE search_lib)

add_executable(web_server src/main_web.cpp)
target_link_libraries(web_server PRIVATE search_lib)