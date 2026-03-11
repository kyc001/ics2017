#!/bin/bash

set -e

script_dir="$(cd "$(dirname "$0")" && pwd)"
nemu="$script_dir/build/nemu"
ori_log="$script_dir/build/nemu-log.txt"

# In container/devbox setups DISPLAY may exist but still be unusable.
# Default to SDL's headless backend unless the user explicitly opts out.
if [ -z "${SDL_VIDEODRIVER:-}" ] && [ "${NEMU_USE_HOST_DISPLAY:-0}" != "1" ]; then
  export SDL_VIDEODRIVER=dummy
fi

if make -C "$script_dir" &> /dev/null; then
  echo "NEMU compile OK"
else
  echo "testcases compile error... exit..."
  exit
fi

echo "compiling testcases..."
if make -C $AM_HOME/tests/cputest ARCH=x86-nemu &> /dev/null; then
  echo "testcases compile OK"
else
  echo "testcases compile error... exit..."
  exit
fi

files=`ls $AM_HOME/tests/cputest/build/*-x86-nemu.bin`

for file in $files; do
  base=`basename $file | sed -e 's/-x86-nemu.bin//'`
  printf "[%14s] " $base
  logfile=$base-log.txt
  $nemu -b -l $ori_log $file &> $logfile

  if (grep 'nemu: HIT GOOD TRAP' $logfile > /dev/null) then
    echo -e "\033[1;32mPASS!\033[0m"
    rm $logfile
  else
    echo -e "\033[1;31mFAIL!\033[0m see $logfile for more information"
    if (test -e $ori_log) then
      echo -e "\n\n===== the original log.txt =====\n" >> $logfile
      cat $ori_log >> $logfile
    fi
  fi
done
