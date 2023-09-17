#
# Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/deepgrace/draft
#

SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_CXX_FLAGS "-std=c++23 -Wall -O3 -Os -s -D BOOST_LOG_DYN_LINK")

include(GNUInstallDirs)

include_directories(${PROJECT_SOURCE_DIR}/raft)
include_directories(${PROJECT_SOURCE_DIR}/server)

include_directories(${PROJECT_BINARY_DIR}/server)
include_directories(${PROJECT_SOURCE_DIR}/../arpc/include)

find_package(Protobuf REQUIRED)

set(PROTO ${PROJECT_SOURCE_DIR}/raft/raft.proto)
PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER ${PROTO})

function(add_file NAME)
    add_executable("${NAME}" "${NAME}.cpp" ${PROJECT_BINARY_DIR}/server/raft.pb.cc)
    target_link_libraries("${NAME}" protobuf pthread boost_thread boost_log_setup boost_log boost_program_options)

    install(TARGETS ${NAME} DESTINATION ${PROJECT_SOURCE_DIR}/server)

    foreach(d a b c)
        install(TARGETS ${NAME} DESTINATION ${PROJECT_SOURCE_DIR}/cluster/${d})
    endforeach()
endfunction()

file(GLOB SRCS "*.cpp")

foreach(file-path ${SRCS})
    string(REPLACE ".cpp" "" file-path-without-ext ${file-path})
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_file(${file-name})
endforeach()
