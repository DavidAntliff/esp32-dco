# ESP32 Digitally Controlled Oscillator

This software produces a square wave on a specified GPIO at the requested frequency.

This component can use the RMT peripheral or the LED\_PWM peripheral to accurately generate the signal.

The RMT approach works quite well, and can generate frequencies up to tens of megahertz, however it is complex to configure.

The LCD\_PWM approach is much simpler to configure, however it seems limited to frequencies from about 10 - 10,000 Hz. I have also had trouble getting the low-speed timers to work with the LED\_PWM.

Duty cycle can be configured with the LED\_PWM approach.

Choose your method in `app_main.c`.

## License

MIT

## Roadmap

* Runtime determination of optimal clock divider with RMT approach.
* Select duty cycle with RMT approach. 
* Support for runtime variation of frequency, possibly via MQTT command.
* Support for phase inversion.
* Support for multiple channels and phase shifting between channels.
* Investigate low-speed timer problems with LED\_PWM approach.
