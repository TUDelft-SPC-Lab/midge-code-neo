# Improvements

This page documents possible improvements to be applied to the firmware for
long-term maintenance.

- Write WAV header for audio files to avoid relying on external scripts for
  playback. It's not that hard and it only adds 44 bytes per file, as shown
  [here](https://docs.fileformat.com/audio/wav/)
