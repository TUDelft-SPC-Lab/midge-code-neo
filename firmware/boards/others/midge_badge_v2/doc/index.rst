.. zephyr:board:: midge_badge_v2

Overview
********

BMD340 Based Midge Badge (nRF52840), using

- LSM6DSO 6 axis IMU
- AK09918 3 axis magnetometer


Hardware
********


Supported Features
==================

.. zephyr:board-supported-hw::


Connections and IOs
===================

LED
---

* LED0 (orange) = P0.03

Push buttons
------------


External Connectors
-------------------

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Flashing
========


.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: midge_badge_v2
   :goals: build flash

Debugging
=========


References
**********

.. target-notes::

.. _nRF52832 Product Specification: https://docs.nordicsemi.com/bundle/ps_nrf52832/page/nrf52832_ps.html
