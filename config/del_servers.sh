#!/bin/bash

source servers.sh
./reconfigure --leader_address=${leader} --del_servers=${server}
