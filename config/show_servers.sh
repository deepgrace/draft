#!/bin/bash

if (( ${#} != 1 )); then
      echo "Usage  : ${0} server"
      echo "example: ${0} <a | b | c>"

      exit 1
fi

source address.sh
leader="${leaders[${1}]}"

if [[ "${leader}" == "" ]]; then
      echo "${1} doesn't exist"
      exit 1
fi

./reconfigure --leader_address=${leader} --show_servers
