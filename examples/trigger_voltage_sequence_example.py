#!/usr/bin/env python3

import numpy as np

from PythonlibVsrc import VoltageSource as vs


def main() -> None:
    # Firmware maps logical trigger pins as:
    # trigger_pin=0 -> EXTI2 (PD2), trigger_pin=1 -> EXTI3 (PC3)
    # edge: 1=rising, 0=falling
    trigger_pin = 1
    edge = 1
    trig_id = 1
    trig_level_v = 3.3

    # Example PWM source for external trigger routing.
    # Use a channel different from sequenced outputs.
    pwm_channel = 0
    pwm_tgp_id = 0
    pwm_low_v = 0.0
    pwm_high_v = 5.0
    pwm_freq_hz = 1.0

    # Few-hundred points example waveform.
    n = 300
    t = np.linspace(0.0, 2.0 * np.pi, n, endpoint=False)

    # Sequenced output on channel 1 (keep channel 0 for PWM trigger source).
    y0 = 5.0 * np.sin(t)

    dev = vs(transport="ethernet", ip="192.168.1.50", port=5000)

    # Configure comparator threshold and PWM trigger source first.
    dev.set_trigger_level(trig_id, trig_level_v)
    dev.start_pwm(pwm_channel, pwm_tgp_id, pwm_low_v, pwm_high_v, pwm_freq_hz)

    # Queue one external-triggered sequence.
    # Each trigger edge advances by exactly one point.
    dev.send_voltage_sequence(1, trigger_pin, edge, y0)

    # Transmit command packet to device.
    dev.send()

    print(f"Loaded trigger sequence on channel 1 with {n} points.")
    print(f"Comparator trigger level set to {trig_level_v:.1f} V on trig_id {trig_id}.")
    print(f"PWM trigger source: ch={pwm_channel}, tgp={pwm_tgp_id}, {pwm_freq_hz:.1f} Hz, {pwm_low_v:.1f}->{pwm_high_v:.1f} V.")
    print(f"Advance with {'rising' if edge else 'falling'} edges on trigger_pin {trigger_pin}.")


if __name__ == "__main__":
    main()
