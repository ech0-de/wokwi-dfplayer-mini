# Wokwi DFPlayer Mini Chip

This is a custom chip for [Wokwi](https://wokwi.com/) that simulates a [DFPlayer Mini](https://picaxe.com/docs/spe033.pdf) devboard: a serial-controlled MP3 player module. It implements the module's 10-byte UART command/response protocol closely enough to drive real host code (e.g. the `DFRobotDFPlayerMini` Arduino library, or hand-rolled frames), tracks playback/volume/EQ state, and drives the BUSY pin.

There is no audio backend in the simulator, so tracks don't actually play sound: instead, a "track" runs for a configurable duration and then reports as finished, which is enough to test the control logic of a real project.

## Pin names

| Name     | Description                                    |
| -------- | ----------------------------------------------- |
| VCC      | Supply voltage                                  |
| GND      | Ground                                          |
| RX       | UART serial input (connect to host TX)          |
| TX       | UART serial output (connect to host RX)         |
| BUSY     | Playback status: LOW while playing, HIGH idle   |
| IO1      | Trigger port 1: short press = previous, long press = volume down |
| IO2      | Trigger port 2: short press = next, long press = volume up |
| DAC_R    | Audio output, right channel (not simulated)     |
| DAC_L    | Audio output, left channel (not simulated)      |
| SPK1     | Speaker output + (not simulated)                |
| SPK2     | Speaker output - (not simulated)                |
| ADKEY1   | AD key port 1 (not simulated)                   |
| ADKEY2   | AD key port 2 (not simulated)                   |
| USB_DP   | USB D+ (not simulated)                          |
| USB_DM   | USB D- (not simulated)                          |

`IO1`/`IO2` are active-low buttons: wire a pushbutton between the pin and `GND`.

## Controls

The chip exposes two sliders in the Wokwi UI (backed by `chip.json` `controls`, readable in the diagram's `attrs`):

| Control            | Description                                              | Default |
| ------------------- | --------------------------------------------------------- | ------- |
| `trackCount`        | Number of tracks on the simulated TF card                | 999     |
| `trackDurationMs`   | How long a simulated track "plays" before finishing, in ms | 3000    |

## UART protocol

Default baud rate is 9600. Frames are the standard DFPlayer Mini format:

```
7E VER LEN CMD FEEDBACK PARAM_H PARAM_L CHECKSUM_H CHECKSUM_L EF
```

Supported commands: next/previous (`0x01`/`0x02`), specify track (`0x03`), volume +/- (`0x04`/`0x05`), specify volume (`0x06`), specify EQ (`0x07`), specify playback mode (`0x08`), specify source (`0x09`), standby/normal (`0x0A`/`0x0B`), reset (`0x0C`), play/pause (`0x0D`/`0x0E`), specify folder/track (`0x0F`), repeat play (`0x11`), and the status/volume/EQ/mode/version/file-count/current-track queries (`0x42`-`0x4D`). Commands with `FEEDBACK = 0x01` get an ACK (`0x41`) reply. On power-on and after a reset command, the chip sends the "device online" announcement (`0x3F`) reporting the TF card as the active source. When a simulated track finishes, it sends the "finished playing" frame (`0x3D`).

## Usage

To use this chip in your project, include it as a dependency in your `diagram.json` file:

```json
  "dependencies": {
    "chip-dfplayer-mini": "github:ech0-de/wokwi-dfplayer-mini@1.0.0"
  }
```

Then, add the chip to your circuit by adding a `chip-dfplayer-mini` item to the `parts` section of diagram.json:

```json
  "parts": {
    ...,
    { "type": "chip-dfplayer-mini", "id": "chip1" }
  },
```

For a complete example, see [test/dfplayer/dfplayer.ino](../test/dfplayer/dfplayer.ino) and [diagram.json](../diagram.json), which wire the chip's `RX`/`TX` to an Arduino Uno's `SoftwareSerial` on pins 10/11 and mirror `BUSY` onto an LED.
