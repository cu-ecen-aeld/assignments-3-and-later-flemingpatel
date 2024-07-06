#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "writer: required two arguments: <writefile> <writestr>"
  exit 1
fi

writefile=$1

if [ -d "$writefile" ]; then
  echo "writer: $writefile must be a file path not a directory"
  exit 1
fi

writestr=$2

if ! mkdir -p "$(dirname "$writefile")" 2>&1; then
  echo "writer: can not create directory $(dirname "$writefile")"
  exit 1
fi

if ! echo "$writestr" > "$writefile" 2>&1; then
  echo "writer: can not write to a file $writefile"
  exit 1
fi

exit 0
