#!/bin/bash

# in case build is executed from outside current dir be a gem and change the dir
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"
cd $SCRIPT_DIR

type=$(echo $1 | awk -F '-' '{print $1}')

target=outfw
if [ "$type" != "$1" ]; then
  target=outfw/$type
fi

cp .config.$1 .config
mkdir -p $target
make clean
make
mv out/*000.bin $target
