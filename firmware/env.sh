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
export BOARD=midge_badge_v1

export PATH="$PATH:$HOME/zephyr-sdk-1.0.1/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin"
which openocd > /dev/null
if [ $? -ne 0 ]; then
    echo "OpenOCD not found in PATH. You may need to adjust the script to point to the correct sdk location."
fi

export MM_ARM_TOOLCHAIN=$HOME/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
which $MM_ARM_TOOLCHAIN/arm-zephyr-eabi-gcc > /dev/null
if [ $? -ne 0 ]; then
    echo "arm-zephyr-eabi-gcc not found in $MM_ARM_TOOLCHAIN. You may need to adjust the script to point to the correct location."
fi
