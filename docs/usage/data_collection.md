# Testing a single Mingle Midge

A cli utility is provided for this, just run

```Shell
uv run tester
```

The `help` command can tell you about the different functionalities you can stimulate.

# Before recording

1. Collect the MAC adresses of the devices you want to control.

    ```Shell
    (echo -e 'power on\nscan on\n'; sleep 5; echo 'scan off') | \
    bluetoothctl > /dev/null 2>&1 && bluetoothctl devices | \
    grep MingleMidge | awk '{print $2}'
    ```

1. Create the experiment description file. An [example](https://github.com/TUDelft-SPC-Lab/midge-code-neo/blob/main/experiments%2Fexample-test-3midges.yaml) is provided.

# Executing the data collection

## CLI

A program `hub_cli` is provided to run data collection experiments. It only
requires a valid experiment definition file. To run it, execute:

```
uv run hub_cli <experiment definition>.yaml
```

The `help` command provides the list of currently supported commands and when
passed a command name will provide the docstring for that command.

An usual experiment flow would be something like:

1. Start the program
1. Execute `init`
1. Execute `watch_status_start`
1. Execute `start_all`
1. (Experiment execution)
1. Execute `watch_status_stop`
1. Execute `stop_all`
1. Execute `quit`


>  If a Mingle Midge fails to initialize, the experiment is aborted, but the
   experiment folder was already created in some devices. Initializing the
   experiment will fail if the folder is not removed beforehand, and this is
   not automatic given human check might be required to avoid data loss. To
   remove the experiment folder after a failed init, execute the following in
   `hub_cli`:

   ```
   sd_erase_folder /SD:/<experiment_id>
   ```


## GUI

> Not yet supported, support planned

# Data extraction and initial post-processing

## Manually

For each Mingle Midge device:

1. Extract the micro-SD card from the Mingle Midge.

1. Connect the SD card to your PC, get abaolute path to the SD card.

1. Call the file extractor with the path to the SD card

    ```Shell
    uv run extract_data -i <path to sd card> -e <experiment_id> -o <output_folder>
    ```

    This will create a subfolder in `output_folder` based on the assignment data of
    the device and run all parser scripts for raw data.

## OTA

> Not supported at the moment
