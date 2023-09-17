#!/bin/bash

if (( ${#} < 2 )); then
      echo "Usage  : ${0} <leader> <log>"
      echo "example: ${0} <a | b | c> <set C++ 2026>"

      exit 1
fi

source address.sh
leader="${leaders[${1}]}"

if [[ "${leader}" == "" ]]; then
      echo "${1} doesn't exist"
      exit 1
fi

shift
log="${@}"

if [[ "${log,,}" == *"get"* ]]; then
      echo skip get command
      exit 1
fi

if [[ "${log}" == "" ]]; then
      echo log is empty
      exit 1
fi

./reconfigure --leader_address=${leader} --append="${log}"
