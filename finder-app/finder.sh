#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "finder: required two arguments: <filesdir> <searchstr>"
  exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]; then
  echo "finder: $filesdir is not a directory or check if it exists"
  exit 1
fi

num_files=$(find "$filesdir" -type f | wc -l)
num_string_matches=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_string_matches"

exit 0
