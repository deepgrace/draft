#!/bin/bash

source servers.sh
./reconfigure --leader_address=${leader} --add_servers=${server}
