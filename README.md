# Tufty 2040 Badge

Display applications for the Pimoroni Tufty 2040 (Raspberry Pi Pico-based) badge. Features a PNG slideshow, name badge display, and Conway's Game of Life.

## Implementations

### MicroPython (`main.py`)
- PNG image slideshow from on-device `pics/` directory
- Button A: Next image
- Button B: Name badge (60 seconds)
- Button C: Conway's Game of Life

### C++ (`tufty-cpp/`)
- Native RP2040 firmware for better performance
- ST7789 display driver with RGB565 color
- LittleFS filesystem for flash storage
- Game of Life: 106x80 grid, double-buffered rendering

## MicroPython Setup

1. Flash MicroPython firmware to Tufty 2040
2. Copy `main.py` and `pics/` directory to device

## C++ Build

```bash
cd tufty-cpp
git clone https://github.com/raspberrypi/pico-sdk.git
git clone https://github.com/pimoroni/pimoroni-pico.git
mkdir build && cd build
cmake ..
make
```

Flash the resulting `.uf2` file to the Tufty 2040.

## Hardware

- Pimoroni Tufty 2040 (RP2040 + ST7789 display)
- 5 buttons (A, B, C, Up, Down)
- 8MB flash with LittleFS

## License

MIT
