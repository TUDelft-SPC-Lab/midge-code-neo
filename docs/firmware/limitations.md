# Limitations

## Audio

### Buffered Samples

The `midge-code-neo` firmware uses 4096-byte buffers for audio processing. The
total buffer count is limited to each particular hardware platform, as this
count is limited by available RAM. The length of audio data $t$ that can be
buffered can be expressed as:

$$
t = \frac{4096B}{fCS}
$$

where

- $t$ is the total buffered time
- $B$ is the buffer count ($5$ in Mingle Midge v1)
- $f$ is the sampling frequency.

    > If it's a "LOW" frequency, chances are it is decimated, the calculation
      should still be with the frequency being decimated, e.g., 1250 Hz is
      obtained from decimating 20 kHz x16, so the calculation is with 20 kHz

- $C$ is the channel count ($1$ mono, $2$ stereo)
- $S$ is the sample size in bytes, $2$ for 16-bit pcm

So if we do the math for the Mingle Midge V1, on stereo, at $16$ kHz, 16-bit PCM:

$$
t = \frac{4096(5)}{(16000)(2)(2)} = 320 \text{ms}
$$

If a delay to process a buffer is higher than the maximum time window that can
be buffered, the firmware will drop incoming samples. The metadata file
for each audio capture will log the sample drops, which will be equivalent to
$t/B$ of data.

Depending on the type of SD card, long delays between writes can
take place, affecting the performance of audio capture. Slow SD cards can
take around 800ms in the worst case, which leads to sample drops due to
resource starvation.

### Audio File Size

Sample File size: at 20KHz audio, stereo:

$$
f=20000\frac{\text{sample}}{s}\cdot\frac{4B}{sample}\cdot\frac{1 GiB}{1024^3 B} \cdot \frac{60s}{1min} \cdot \frac{60min}{1h} = 0.27 \frac{GiB}{h}
$$

Given the max file size of FAT32 is 4 GiB, ~14 hours is the most that can be
recorded in a single audio file

## Experiment Folders:

The SD cards are expected to be formatted with a max
of 32 root directories. This configuration can be changed, but is the default.
