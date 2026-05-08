.. zephyr:board:: midge_badge_v1

Overview
********

BMD300 Based Midge Badge, using ICM20948 IMU


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
   :board: midge_badge_v1
   :goals: build flash

Debugging
=========


References
**********

.. target-notes::

.. _nRF52832 Product Specification: https://docs.nordicsemi.com/bundle/ps_nrf52832/page/nrf52832_ps.html
