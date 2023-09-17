#!/bin/bash

if (( ${#} != 1 )); then
      echo "Usage  : ${0} server"
      echo "example: ${0} <a | b | c>"

      exit 1
fi

source address.sh

stat_file="${statmap[${1}]}"
ldbs_file="${ldbsmap[${1}]}"

if [[ "${stat_file}" == "" || "${ldbs_file}" == "" ]]; then
      echo "${1} doesn't exist"
      exit 1
fi

./entry_reader ${stat_file} ${ldbs_file}
