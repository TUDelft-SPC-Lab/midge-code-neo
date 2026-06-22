# SPCL - Mingle Midge Badge V1

![Image of the Mingle Midge Badge ](https://github.com/TUDelft-SPC-Lab/spcl_midge_hardware/blob/main/Hardware_Design_v1/Media/overview.jpg?raw=true)

## High level hardware description:

- BLE/Control module: u-blox BMD-300
    - uC: nRF52832 QFAA
- Sensing:
    - Audio: PDM peripheral
        - Microphone: [ST MP34DT05-A](https://www.st.com/resource/en/datasheet/mp34dt05-a.pdf)
    - Proximity: via BLE radio (RSSI)
    - IMU: ICM-29048 via I2C
- Storage:
    - micro-SD card socket provided.
- Battery: 500mAh

## Default Firmware config:

- 5 Audio Buffers
