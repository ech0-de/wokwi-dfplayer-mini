# DFPlayer Mini Chip

A custom chip for [Wokwi](https://wokwi.com/) that simulates a [DFPlayer Mini](https://picaxe.com/docs/spe033.pdf) MP3 devboard: the UART command/response protocol, volume/EQ/playback-mode state, the BUSY status pin, and the IO1/IO2 trigger buttons.

This project was bootstrapped from Wokwi's [inverter-chip](https://github.com/wokwi/inverter-chip) custom chip template.

The chip source lives in [src/main.c](src/main.c), and the pins/controls are described in [chip.json](chip.json). See [docs/README.md](docs/README.md) for the pinout and what is/isn't simulated.

## Building

The easiest way to build the project is to open it inside a Visual Studio Code dev container, and then run the `make` command.

## Testing

You can test this project using the [Wokwi extension for VS Code](https://marketplace.visualstudio.com/items?itemName=wokwi.wokwi-vscode). Open the project with Visual Studio Code, press "F1" and select "Wokwi: Start Simulator".

If you want to make changes to the test project firmware, edit [test/dfplayer/dfplayer.ino](test/dfplayer/dfplayer.ino), and then run `make test` to rebuild the .hex file. You'll need the [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/), which is already installed in the dev container.

## License

This project is licensed under the MIT license. See the [LICENSE](LICENSE) file for more details.
