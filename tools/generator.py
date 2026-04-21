# Use the sounddevice module
# http://python-sounddevice.readthedocs.io/en/0.3.10/

import numpy as np
import sounddevice as sd
import time

# Samples per second
sps = 44100

# Frequency / pitch
freq_hz1 = 0.12
freq_hz2 = 5.0
freq_hz3 = 0.4
# Duration
duration_s = 60.0

# Attenuation so the sound is reasonable
atten = 1.0 # 0.3

# NumpPy magic to calculate the waveform
each_sample_number = np.arange(duration_s * sps)
waveform1 = np.sin(2 * np.pi * each_sample_number * freq_hz1 / sps)
waveform2 = np.sin(2 * np.pi * each_sample_number * freq_hz2 / sps)  # Higher frequency
waveform3 = np.sin(2 * np.pi * each_sample_number * freq_hz3 / sps)  # Lowest frequency
waveform = waveform2  # Combine the waveforms
waveform_quiet = waveform * atten

# Play the waveform out the speakers
sd.play(waveform_quiet, sps)
time.sleep(duration_s)
sd.stop()