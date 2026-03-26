#!/bin/sh

# It is expected that you have the virtual environment for zephyr set up at
# ~/zephyrproject/.venv
#
if [ $0 = "/usr/bin/bash" -o $0 = "/bin/bash" -o $0 = "bash" -o $0 = "-bash" ]; then
    FW_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
elif [ $0 = "sh" ]; then
    FW_DIR=$(realpath $(dirname $0))
else
    FW_DIR=$(realpath $(dirname $0))
fi

FW_DIR=$FW_DIR
source ~/zephyrproject/.venv/bin/activate
# personal zephyr fork to fix anomaly 58 init in the meantime
export ZEPHYR_BASE=~/zephyrproject/zephyr
export BOARD_ROOT=$FW_DIR
export BOARD=midge_badge_nrf52
