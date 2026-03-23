# Limitations

- Audio samples can be dropped due to resource starvation. The audio medatada
  file lists instances of sample drops. The probability of resource starvation
  depends on

    + Audio channels under use: More channels implies file IO needs to operate
      faster to meet resource usage deadlines.

    + RAM of the hardware platform: The main way to prevent resource starvation
      is  bigger sampling buffers, but the size of these is limited by the RAM
      of the embedded platform. In the 2019 Mingle Midge hardware design, this
      is 64 KB which allows for a maximum

    + SD Card: Depending on the type of SD card, long delays between writes can
      take place, affecting the performance of audio capture. Slow SD cards can
      take around 800ms in the worst case, which leads to sample drop due to
      resource starvation (audio buffer config can vary, but buffering more than
      100ms is dificult.)

- Sample File size: at 20KHz audio, stereo:

  $$
  f=20000\frac{\text{sample}}{s}\cdot\frac{4B}{sample}\cdot\frac{1 GiB}{1024^3 B} \cdot \frac{60s}{1min} \cdot \frac{60min}{1h} = 0.27 \frac{GiB}{h}
  $$

  Given the max file size of FAT32 is 4 GiB, ~14 hours is the most that can be
  recorded in a single audio file

- Experiment Folders: The SD cards are expected to be formatted with a max
  of 32 root directories. This configuration can be changed, but is the default.
