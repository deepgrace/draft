#
# Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/deepgrace/draft
#

set(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_CXX_FLAGS "-std=c++23 -Wall -O3 -Os -s -D BOOST_LOG_DYN_LINK")

include(GNUInstallDirs)

include_directories(${PROJECT_SOURCE_DIR}/raft)

function(add_file NAME)
    add_executable("${NAME}" "${NAME}.cpp")
    target_link_libraries("${NAME}" pthread boost_thread boost_log_setup boost_log boost_program_options)

    if ("${NAME}" STREQUAL "config_server")
        install(TARGETS ${NAME} DESTINATION /tmp)
    elseif ("${NAME}" STREQUAL "entry_reader")
        install(TARGETS ${NAME} DESTINATION ${PROJECT_SOURCE_DIR}/config)
    elseif ("${NAME}" STREQUAL "config_client")
        install(PROGRAMS ${PROJECT_BINARY_DIR}/raft/${NAME} DESTINATION ${PROJECT_SOURCE_DIR}/config RENAME reconfigure)
    endif()
endfunction()

file(GLOB SRCS "*.cpp")

foreach(file-path ${SRCS})
    string(REPLACE ".cpp" "" file-path-without-ext ${file-path})
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_file(${file-name})
endforeach()
