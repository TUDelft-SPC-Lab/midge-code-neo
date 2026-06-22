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

while getopts b:l flag
do
    case "${flag}" in
        b) BOARD=${OPTARG};;
        l) list_boards; exit 0;;
        *) echo "Invalid option: -${OPTARG}" >&2; exit 1;;
    esac
done

if [[ -z "${BOARDS[$BOARD]}" ]]
then
    echo "Invalid board: $BOARD"
    list_boards
    exit 1
fi

CHIP=${BOARDS[$BOARD]}

wget -O /tmp/chip.svd https://raw.githubusercontent.com/embassy-rs/nrf-pac/refs/heads/main/svd/${CHIP}.svd;
west build -p auto -b ${BOARD} application
