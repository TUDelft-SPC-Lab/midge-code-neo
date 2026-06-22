# About

"Vanilla" is how we refer to a workflow that does not depend on the usage of
any IDE or IDE-like tools.

# Setup

1. [Setup Zephyr RTOS](setup-zephyr.md)
1. Source the environment script:

    ```Shell
    cd <midge-code-neo root>/firmware
    source env.sh
    ```


# Building

```Shell
west build -p auto -b <board_name> application
```

> `board_name` corresponds to any subfolder of `firmware/boards`, e.g.
  `midge_badge_v1`

# Flashing

After building

```Shell
west flash #--runner jlink if using jlink
```

# Debugging

```Shell
west debug #--runner jlink if using jlink
```

# Getting the RTT output log

This can be done in parallel with `west debug` but must be executed _after_
the GDB server is already initialized.

```Shell
west rtt #--runner openocd if using DAP-Link
```
