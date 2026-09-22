# 1) Ethernet quick chain test (CH0 -> CH1)
from PythonlibVsrc import (
    clear_state,
    close_transport,
    configure_transport,
    connect_transport,
    reset_buffer,
    send,
    send_chained_soft_sequence,
)
import numpy as np

# Connect
configure_transport(transport="ethernet", ip="192.168.1.50", port=5000)
connect_transport()

# Build source/follower waveforms
src = np.linspace(-10,10,20)   # 20 points: -2V to +2V
follower = np.linspace(-5,5,10)                  # 4 points

# Queue commands in one packet
reset_buffer()
clear_state()

# Arm follower first: waits on internal IRQ events
send_chained_soft_sequence(
    dac_channel=2,
    delay_ms=0,
    voltages=follower,
    irq_every_points=0,          # 0 => does not emit further chain events
    next_dac_channel=-1,
    start_on_internal_irq=True
)

# Start source: time-based, emits event every 5 points to CH1
send_chained_soft_sequence(
    dac_channel=0,
    delay_ms=10,
    voltages=src,
    irq_every_points=2,
    next_dac_channel=2,
    start_on_internal_irq=False
)

# Send to device
send()
print("Queued chain test: CH0 drives CH1 every 5 points")
print(f"Source points: {src}")
print(f"Follower points: {follower}")

close_transport()