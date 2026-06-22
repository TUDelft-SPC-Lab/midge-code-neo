# Firmware Overview

The firmware for `midge-code-neo` is a Zephyr RTOS free-standing application.
The latter means that this is out-of-tree application.

## Using Zephyr

Shawn Hymel has a nice series that covers the basics, if you are new to Zephyr,
you might want to check it out:

<iframe width="560" height="315" src="https://www.youtube.com/embed/videoseries?si=4JhxriTV3VR-cqWB&amp;list=PLEBQazB0HUyTmK2zdwhaf8bLwuEaDH-52" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>

Playlist link: <https://youtube.com/playlist?list=PLEBQazB0HUyTmK2zdwhaf8bLwuEaDH-52&si=YIGYE4d4_PHnz-3N>

## Access to ICM-20948 TDK driver

> This is only required for Mingle Midge V1

Usage of this IMU relies on the TDK closed-source driver. The files are
copyrighted, you need to accept the license agreement before getting access to
the files

1. Accept the license by creating a developer account at InvenSense <https://invensense.tdk.com/developers/register/>
1. Send an email to SPCLab@tudelft.nl asking for access to the driver repository
1. Add the driver repo by running `git submodule update --init`

## Quickstart

1. Check the relevant info for the platform in your hands in
   "Supported Boards", e.g.
   [Mingle Midge V1](boards/midge_v1.md)
1. Check the [Wiring Guide](wiring.md) for guidance on debug probe setup
   for flashing and debugging
1. Follow the instructions for the workflow of your preference, e.g.
   [VSCode](workflows/vscode.md)

## FAQ

+ Isn't info missing regarding setup of tools like openOCD, or cross-compiling
  toolchain?

    R/ Zephyr simplifies dealing with these tools. There is no need to
    download custom toolchains on your own unless you want to experiment with
    something
