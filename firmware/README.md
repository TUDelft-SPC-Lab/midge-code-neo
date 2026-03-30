# Firmware

## Zephyr Fork

Currently, some features are not available in mainline. To set up the zephyr SDK
you'll have to adjust the
[get started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
a bit. After setting up zephyr RTOS (assuming you used `~/zephyrproject` as
install directory) you'll want to add a custom remote:

```Shell
cd ~/zephyrproject/zephyr
git remote add josfemova git@github.com:Josfemova/zephyr.git
git fetch josfemova
git checkout josfemova/dev_main
cd ~/zephyrproject
west update
```

This custom remote is used to add changes which are still not in mainline.
The `dev_main` branch is intended to be the latest zephyr changes with custom
modifications rebased on top, until these modifications are merged to
mainline
