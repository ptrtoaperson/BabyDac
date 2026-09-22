#!/usr/bin/env python3
"""Simple example for the semiQonVsrc module.

What it does:
1) Connects over Ethernet (or UART if you switch config)
2) Sets a few static voltages
3) Starts one PWM channel
4) Waits briefly
5) Stops PWM and returns channel to 0V
"""

import time

import semiQonVsrc as vsrc


def main():
    # Choose one transport configuration.
    vsrc.configure_transport(
        transport="ethernet",
        ip="192.168.1.50",
        port=5000,
    )
    # UART example:
    # vsrc.configure_transport(
    #     transport="uart",
    #     uart_port="/dev/ttyUSB0",
    #     uart_baudrate=115200,
    #     uart_timeout_s=1.0,
    # )

    vsrc.connect_transport()
    try:
        # Batch 1: set three channels and send.
        vsrc.reset_buffer()
        vsrc.set_voltage(0, 0.0)
        vsrc.set_voltage(1, 1.25)
        vsrc.set_voltage(8, -2.0)
        vsrc.send(command_file="example_set.bin")

        # Batch 2: start PWM on logical channel 23 using TGP 2.
        vsrc.reset_buffer()
        vsrc.start_PWM(channel=23, id=2, low=-1.0, high=3.0, freq=2.0)
        vsrc.send(command_file="example_pwm_start.bin")

        time.sleep(2.0)

        # Batch 3: stop PWM and force that channel to 0V.
        vsrc.reset_buffer()
        vsrc.stop_PWM(channel=23, id=2)
        vsrc.send(command_file="example_pwm_stop.bin")

    finally:
        vsrc.close_transport()


if __name__ == "__main__":
    main()
