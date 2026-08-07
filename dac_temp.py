"""Simple PyVISA send example for Ethernet and Serial.

Edit the values in the CONFIG section, then run:
        python dac_temp.py

Packet format sent to STM32:
        [ID=1][channel(uint8)][dac_code(uint16 little-endian)] + 'e'
"""

from __future__ import annotations

import struct

import pyvisa
from pyvisa import errors as visa_errors

LOGICAL_CHANNEL_MIN = 0
LOGICAL_CHANNEL_MAX = 23


def _resource_manager() -> pyvisa.ResourceManager:
    """Create a VISA manager using the pure-Python backend."""
    try:
        return pyvisa.ResourceManager("@py")
    except Exception as exc:
        raise RuntimeError(
            "Could not create PyVISA backend '@py'. Install with: pip install pyvisa pyvisa-py"
        ) from exc


def code_from_voltage(voltage: float) -> int:
    """Convert volts in [-10, 10] to 16-bit offset-binary DAC code."""
    if voltage > 10.0:
        voltage = 9.999999
    if voltage < -10.0:
        voltage = -9.999999

    code_val = int(((voltage + 10.0) / 20.0) * 65535)
    if code_val > 0xFFFF:
        code_val = 0xFFFF
    return code_val


def build_set_voltage_payload(channel: int, voltage: float) -> bytes:
    """Build one set-voltage command and append the required end marker 'e'."""
    if not LOGICAL_CHANNEL_MIN <= channel <= LOGICAL_CHANNEL_MAX:
        raise ValueError(
            f"channel must be {LOGICAL_CHANNEL_MIN}-{LOGICAL_CHANNEL_MAX}, got {channel}"
        )

    payload = bytearray()
    payload += struct.pack("<BBH", 1, channel, code_from_voltage(voltage))
    payload += b"e"
    return bytes(payload)


def send_over_ethernet(ip: str, port: int, payload: bytes, timeout_ms: int = 2000) -> None:
    """Send payload over VISA TCP socket resource."""
    resource = f"TCPIP0::{ip}::{port}::SOCKET"
    rm = _resource_manager()
    try:
        inst = rm.open_resource(resource)
    except (visa_errors.VisaIOError, Exception) as exc:
        rm.close()
        raise RuntimeError(
            f"Could not open Ethernet resource {resource}. Check IP/port/device power/network. Original error: {exc}"
        ) from exc
    try:
        inst.timeout = timeout_ms
        inst.write_termination = ""
        inst.read_termination = ""
        inst.write_raw(payload)
        print(f"Sent {len(payload)} bytes via Ethernet to {resource}")
    finally:
        inst.close()
        rm.close()


def send_over_serial(
    serial_resource: str,
    baudrate: int,
    payload: bytes,
    timeout_ms: int = 2000,
) -> None:
    """Send payload over VISA serial resource."""
    rm = _resource_manager()
    try:
        inst = rm.open_resource(serial_resource)
    except (visa_errors.VisaIOError, Exception) as exc:
        rm.close()
        raise RuntimeError(
            "Could not open serial resource "
            f"{serial_resource}. Try list_resources() and verify port permissions/cable. Original error: {exc}"
        ) from exc
    try:
        inst.timeout = timeout_ms
        inst.baud_rate = baudrate
        inst.write_termination = ""
        inst.read_termination = ""
        inst.write_raw(payload)
        print(f"Sent {len(payload)} bytes via Serial to {serial_resource} @ {baudrate}")
    finally:
        inst.close()
        rm.close()

# -------------------------
# CONFIG: edit these values
# -------------------------
TRANSPORT = "ethernet"  # "ethernet" or "serial"

CHANNEL = 1
VOLTAGE = -3.0

# Ethernet settings
IP = "192.168.1.50"
PORT = 5000

# Serial settings (VISA resource style)
SERIAL_RESOURCE = "ASRL/dev/ttyUSB0::INSTR"
BAUDRATE = 115200


# -------------------------
# Direct example execution
# -------------------------
payload = build_set_voltage_payload(channel=CHANNEL, voltage=VOLTAGE)

if TRANSPORT == "ethernet":
    send_over_ethernet(ip=IP, port=PORT, payload=payload)
elif TRANSPORT == "serial":
    send_over_serial(serial_resource=SERIAL_RESOURCE, baudrate=BAUDRATE, payload=payload)
else:
    raise ValueError("TRANSPORT must be 'ethernet' or 'serial'")
