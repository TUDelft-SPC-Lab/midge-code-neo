# Zephyr RTOS Setup

> This is not required if you are working with PlatformIO

Follow the official
[Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
guide but omit what comes after building the blinky example.
And modify the setup as follows.

* Use the default `~/zephyrproject/.venv` when following the instructions.
* In the `~/zephyrproject/zephyr/west.yml` set the projects to only what is needed for the midge **before running `west update`**:
    ```yaml
      projects:
        - name: cmsis_6
          repo-path: CMSIS_6
          revision: b2dfbe1a20bbd49c2d2c605073799671074bbb30
          path: modules/hal/cmsis_6
          groups:
            - hal
        - name: fatfs
          revision: f4ead3bf4a6dab3a07d7b5f5315795c073db568d
          path: modules/fs/fatfs
          groups:
            - fs
        - name: hal_nordic
          revision: 18da0cc9726f8759c627dba3180b3ba9294e433c
          path: modules/hal/nordic
          groups:
            - hal
        - name: mcuboot
          revision: 0fae8920c4e5acb792b3fe766c89c668f42be6ee
          path: bootloader/mcuboot
          groups:
            - bootloader
        - name: segger
          revision: 50892fdbcf2f570e67baa72b8894a66b16946f72
          path: modules/debug/segger
          groups:
            - debug
    ```
* On the sdk install only pull the necessary one for the midge
    ```bash
    west sdk install -t arm-zephyr-eabi
    ```

Follow these build steps just to make sure your setup is correct:

```Shell
cd ~/zephyrproject/zephyr
west build -p always -b nrf52dk/nrf52832 samples/basic/blinky
```

After setting up zephyr and testing the build for the blinky example, if nothing
went wrong, you can try to build the Mingle Midge firmware.

```Shell
cd <midge-code-neo repository path>/firmware
source env.sh # sets up environment variables and activates venv
west build -p always application # Build for Mingle Midge v1 by default
```

If you get an error, discard it's a missing tool what's causing it. If the
error is a compilation error, [open an issue](https://github.com/TUDelft-SPC-Lab/midge-code-neo/issues)
The likely reason is a newer version of Zephyr introducing breaking changes.

The last tested zephyr version is documented in the firmware [README.md](https://github.com/TUDelft-SPC-Lab/midge-code-neo/blob/main/firmware/README.md)

## Changing the Zephyr version

If you don't want to wait for the issue requesting support for a newer zephyr
version to be resolved, you can also roll back a zephyr version to the last
documented tested version. Say, if the last version tested is `4.5.0`, then
assuming you installed Zephyr in your home folder:

```Shell
cd ~/zephyrproject/zephyr
git fetch
git checkout origin/v4.5.0
west update
west packages pip --install
west sdk install
```

## Setting up a custom remote

If there is a zephyr feature not in mainline but in a fork of zephyr, you can
add a secondary remote to test that feature. One example is the support for
a basic OSS driver for the ICM-20948 IMU not found in mainline Zephyr but
supported in [this fork](https://github.com/Josfemova/zephyr)

```Shell
cd ~/zephyrproject/zephyr
git remote add josfemova git@github.com:Josfemova/zephyr.git
git fetch josfemova
git checkout josfemova/dev_main # rebased on top of zephyr latest
cd ~/zephyrproject
west update
west packages pip --install
west sdk install
```
