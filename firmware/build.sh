#! /bin/bash


declare -A BOARDS

BOARDS["midge_badge_v1"]="nrf52832"
BOARDS["midge_badge_v2"]="nrf52840"

list_boards() {
    echo "Available boards:"
    for board in "${!BOARDS[@]}"
    do
        echo "  - $board (${BOARDS[$board]})"
    done
}

while getopts b:t:l flag
do
    case "${flag}" in
        b) BOARD=${OPTARG};;
        t) TYPE=${OPTARG};;
        l) list_boards; exit 0;;
        *) echo "Invalid option: -${OPTARG}" >&2; exit 1;;
    esac
done

if [[ -z "$TYPE" ]]
then
    echo "No release type specified, using default: debug"
    TYPE="debug"
fi

if [[ -z "${BOARDS[$BOARD]}" ]]
then
    echo "Invalid board: $BOARD"
    list_boards
    exit 1
fi

CHIP=${BOARDS[$BOARD]}

if [[ "$TYPE" != "release" && "$TYPE" != "debug" ]]
then
    echo "Invalid build type: $TYPE"
    exit 1
else
    echo "Using build type: $TYPE"
fi

CONF_FILE=""
if [[ "$TYPE" == "release" ]]
then
    CONF_FILE="prj_release.conf"
else
    CONF_FILE="prj_debug.conf"
fi
echo CONF_FILE=$CONF_FILE

wget -O /tmp/chip.svd https://raw.githubusercontent.com/embassy-rs/nrf-pac/refs/heads/main/svd/${CHIP}.svd;
west build -p auto -b ${BOARD} application -- -DEXTRA_CONF_FILE=${CONF_FLAGS}
