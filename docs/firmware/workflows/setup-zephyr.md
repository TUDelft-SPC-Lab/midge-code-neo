# Zephyr RTOS Setup

> This is not required if you are working with PlatformIO

Follow the official
[Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
guide but omit what comes after building the blinky example.
And modify the setup as follows.

* Use the default `~/zephyrproject/.venv` when following the instructions.
* **Before running `west update`** do the following
    * Checkout version `v4.5.0-rc1` of zephyr, follow the steps in [chaning the zephyr version](#changing-the-zephyr-version)
    * In the `~/zephyrproject/zephyr/west.yml` set the projects to only what is needed for the midge **before running `west update`**:

      ```yaml
      projects:
        - name: cmsis_6
          repo-path: CMSIS_6
          revision: 1c1840af7a7e757d6e2fec3ddb0e5ce0dfcc93c8
          path: modules/hal/cmsis_6
          groups:
            - hal
        - name: fatfs
          revision: 4f60a12c58c3de4246857ad87c40239ecd93349b
          path: modules/fs/fatfs
          groups:
            - fs
        - name: hal_nordic
          revision: 58c060befc11f3029212bf4246fd3ca471328861
          path: modules/hal/nordic
          groups:
            - hal
        - name: hal_st
          revision: ce795a9b0de623022c17d2ff394587c024aec850
          path: modules/hal/st
          groups:
            - hal
        - name: mcuboot
          revision: c85af35b875d244cf91f8ec3caa04168fc7f1ec1
          path: bootloader/mcuboot
          groups:
            - bootloader
        - name: segger
          revision: 50892fdbcf2f570e67baa72b8894a66b16946f72
          path: modules/debug/segger
          groups:
            - debug
      ```
* Instead of `west sdk install`, install only the necessary sdk one for the midge
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
