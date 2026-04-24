# Iot Project

## Abstract
This repository contains firmware for an IoT sensing node built for the
Internet of Things Algorithms and Services course (Sapienza University of Rome,
A.Y. 2025/2026).

The main idea is simple:

- sample a signal on-device
- process it locally with filtering and FFT
- transmit compact statistics instead of raw data
- adapt sampling behavior to reduce energy and network usage

The project uses FreeRTOS tasks on ESP32 and supports two uplink modes:

- MQTT over WiFi
- LoRaWAN (ABP) on TTN

## What is in this project

There are two firmware targets in `platformio.ini`:

- `nodemcu-32s` or `heltec_wifi_lora_32_V3`: the sensing node (sampler,
  filter, FFT, communication)
- `raspberry-pi-pico`: signal generator + power monitor firmware
  (`src/generator_monitor.cpp`)

## Materials
- 1 ESP32 Heltec V3 Lora, for LoRa functionalities
- 1 ESP32-WROOM-32 
- 1 INA219 
- 1 raspberry pi pico
- 1 small breadboard
- 1/2 100 microOhm resistors
- 1 10 microFarahd condenser
- plenty of wires
- 1 tin cookie box for DIY Faraday cage (doesn't work)

---

## Structure

![Overview](images/MVIMG_20260423_231926.jpg)

We have two main components, and their respective builds can be found in the platformio.ini file:
- the **Esp32**(s) contain the parts you will find in most IoT devices: the **Sampler**, the **Filter**, the **FFT** and the **Communications**
- the **Raspberry Pi Pico** instead works as the **Signal generator** and **Energy monitor**

### Signal generation
Raspberry Pi Pico uses PWM to generate the signal that is later smoothed by the RC low-pass filter.

The waveform is a sum of sinusoids:

`signalRaw(t) = Σ A_i * sin(2π f_i t)`

For the current Pico generator configuration, the values are:

- PWM carrier: `200000 Hz`
- waveform update rate: `20000 Hz`
- sine frequencies: `5.0 Hz`, `14.0 Hz`, `0.005 Hz`
- sine amplitudes: `4.0`, `2.0`, `10.0`

Noise is implemented but currently commented out in the generator loop. The noise model is:

- Gaussian noise: `kNoiseSigma = 0.2`
- Spike probability: `kSpikeProb = 0.02`
- Spike magnitude: uniform in `[5, 15]`
- Spike sign: positive or negative with 50/50 probability

![Signal generated and plotted without impurities](images/segnale_puro.png)

The PWM signal is still a square-wave output at the pin 40, so to make it usable as
an analog input we pass it through an RC low-pass filter before it reaches the
ESP ADC pin 32.

### Pipeline
The ESP32 pipeline is built around three double-buffered sample blocks of 1024
samples each. This lets one task fill the next block while the previous block
is still being processed and the following block is waiting in line.

In practice, each stage consumes the block produced by the stage before it,
processes it, and then hands it off to the next task. Because the buffers are
rotated continuously, sampling, filtering, FFT, and communication can overlap
instead of running strictly one after another.

![Full pipeline](images/pipeline2.jpg)

#### Sampler task and maximum frequency
The sampler uses the built-in i2s hardware of the Esp (only WROOM), which uses DMA to directly sample from the adc pin and put it in memory. I2s is designed for sound, so it can sample higher than 44.100 hz. Then the sampler casts and copies appropriately My experiments showed that without sending data with lora or mqtt, the pipeline easily can withstand frequencies higher than 10.000 hz, also 15.000 hz, but this introduces numerous problems, so we start from an arbitrary 1.000 hz.

#### Filter task
The filter takes the filled buffer given from the sampler and can:
- do nothing
- compute z-score
- compute hampel
because it uses a full block to work on and not a continuous flow of samples, sliding window filters are  penalised as they use the previous samples to calculate, so we use an **history** buffer that saves the last WINDOW_SIZE components of the block, to use in the next one

#### FFT task
This task always runs on the filtered block and is responsible for estimating
which frequency is currently dominant in the signal.

Before running the FFT, it removes the DC component by subtracting the block
mean. It then applies a Hamming window, computes the spectrum, and converts the
result to magnitudes.

From that spectrum, the task looks for the strongest peak and then selects the
highest frequency bin that is still close enough to that peak (at least 70%). That value is
used as the estimated dominant frequency of the input signal.

If self-optimizing mode is enabled, the task uses that estimate to choose the
sampling rate for the next block. The goal is to keep the sampling frequency
comfortably above the detected signal frequency, but without oversampling more
than necessary. A few safety rules are applied to avoid unstable jumps or very
low/high rates.

The FFT stage only takes a few milliseconds, but it runs on every block because
it is what lets the system adapt when the signal changes.

#### Comms task
The filtered buffer is averaged, std over the entire 1024 samples array, so it's 1024/{current sampling frequency} seconds. WE gathered data can be transmitted via WiFi using MQTT (on a private broker) and LoRa (on TTN). A set flag will choose if LoRa or MQTT is used, located in the header file, both WiFi and LoRa can be enabled/disabled to replicate all the experiments.

### Performance
#### Energy
Let' measure the energy while oversampling, powered by usb

![Oversampling consumption](images/energia_mqtt_noopt.png)

As you can see the power consumption is very high, averiging **410 mW** and **76 mA**. The esp never sleeps and performs all the pipeline

Let's see the adaptive sampling

![Adaptive Sampling consumtion](images/energia_mqtt_opt.png)

Now we see an average of **300 mW** and **60mA**, with respectively a 25% and 21% savings

#### RTTs
These are printed snippets from the program

##### LoRa
We use the RadioLib library and connected to LoraWAN with ABP initialization, this requires settings manually your keys taking them from TTN. We send 4 bytes, we have to wait more than 18 seconds before the next packet
[LoRa] Sending packet 1...
[LoRa] Uplink OK (no downlink) | AVG: 1886.99 | Latency: 2871 ms
[LoRa] Waiting duty-cycle window: 18058 ms
[LoRa] Sending packet 2...
[LoRa] Uplink OK (no downlink) | AVG: 1885.00 | Latency: 2863 ms
[LoRa] Sending packet 3...
[LoRa] Uplink OK (no downlink) | AVG: 1885.87 | Latency: 2863 ms


##### MQTT
We use the SubPubLibrary to connect to the Mosquitto Broker installed on my PC (requires a little bit of setup: opening the firewall, allowing anonymous clients, script to send responses on publish), connect to the wifi of my cellphone.
1776943053: Sending PUBLISH to ESP32Client (d0, q0, r0, m0, 'iot_single/response', ... (56 bytes))
1776943053: Received PUBLISH from ESP32Client (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))
1776943053: Sending PUBLISH to auto-856FDC9D-0D2D-3404-8A3F-AA822E8D00C0 (d0, q0, r0, m0, 'iot_single/stats', ... (56 bytes))

**rtt**: 5.273 ms


#### End to End
Due to the array structure it isnt possible to time the exact time from when a sample is extracted and when it's averaged and send, but we can time every iteration for every task, and plot the pipeline in action on the time domain, to see how much time on average each block of 1024 sample is processed by each task

![execution time (no freq optimization)](images/pipeline_noopt.png)

This is an ideal plot that shows best how a task sends the processed buffer to the next one, but its not perfect, block 0 of sampler is never logged.

We can see how every task elaborates a block and passes it to the next one. Here are the average executions:

  sampler       47.55 ms
  filter        49.02 ms
  fft           12.00 ms
  comm           5.71 ms
  sum          114,24 ms

