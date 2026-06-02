## Jasp Keyboard Joystick

The Keyboard Joystick solves the WASD movement problem for PC gamers. You can also map it to mouse movement and effectively use your PC with one hand. It's a relatively simple project that requires minimal soldering. I originally designed this back in 2022 and have since been evolving it into a full keypad. This design is far from perfect, but I would love for you to try it and let me know how it works for you.

![3d-printed](https://github.com/multifex/prototypes/blob/main/jasp-keyboard-joystick/img/printed-1.png)

## Build Guide

There is no detailed build guide available at the moment, but you can see the whole build process in [this video](https://www.youtube.com/watch?v=S8SKIpWGIe8&t=1s)

## Wiring Diagram

![Wiring_diagram](https://github.com/multifex/prototypes/blob/main/jasp-keyboard-joystick/ProMicro_Wiring_Diagram.png)


## Firmware
There is a quick and dirty firmware available [here](https://github.com/multifex/prototypes/blob/main/jasp-keyboard-joystick/firmware), it's super basic but it works. If I make something more advanced, I'll be sure to update it. [Installation guide](https://github.com/multifex/prototypes/blob/main/jasp-keyboard-joystick/firmware/Firmware_Installation_Guide.md)

> **Local fork note** — this copy ships an enhanced firmware set (see [firmware/Firmware_Installation_Guide.md](firmware/Firmware_Installation_Guide.md)):
> - [`firmware/sketch_promicro`](firmware/sketch_promicro/sketch_promicro.ino) — main firmware: joystick / mouse / scroll modes, deferred tap-hold button gestures, polar-gate calibration, mode-blink LED, EEPROM-persisted settings, configured live via [`firmware/web_config.html`](firmware/web_config.html) (Web Serial)
> - [`firmware/sketch_promicro_xinput`](firmware/sketch_promicro_xinput/sketch_promicro_xinput.ino) — game build: enumerates as a native Xbox 360 controller (XInput); reuses the main firmware's stored calibration


## License

Files in this repository are released under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

You are free to use, modify, and share these files for **personal and educational use** with attribution. **Commercial use is not permitted** under this license.

For commercial licensing inquiries, please reach out.  


## Follow Along

I document the design and build process of all my projects here: [@JaspMakes](https://youtube.com/@jaspmakes)
