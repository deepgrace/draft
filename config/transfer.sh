#!/bin/bash

if (( ${#} != 2 )); then
      echo "Usage  : ${0} <leader> <server>"
      echo "example: ${0} <a | b | c> <a | b | c>"

      exit 1
fi

source servers.sh
./reconfigure --leader_address=${leader} --target_server=${server}
