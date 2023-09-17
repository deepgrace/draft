#!/bin/bash

path=../config

g++ -std=c++23 -Wall -O3 -Os -s -I . -D BOOST_LOG_DYN_LINK -l pthread \
-l boost_thread -l boost_log_setup -l boost_log entry_reader.cpp -o ${path}/entry_reader

g++ -std=c++23 -Wall -O3 -Os -s -I . config_server.cpp -o /tmp/config_server
g++ -std=c++23 -Wall -O3 -Os -s -I . -l boost_program_options config_client.cpp -o ${path}/reconfigure

strip ${path}/entry_reader
strip ${path}/reconfigure
