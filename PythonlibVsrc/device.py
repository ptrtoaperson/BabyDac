from contextlib import contextmanager
import struct
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Optional, Sequence, Tuple, Union

try:
    import pyvisa  # type: ignore[import-not-found]
except ImportError:
    pyvisa = None


# ========================
# Defaults and limits
# ========================
DEFAULT_TRANSPORT = "ethernet"

DEFAULT_STM32_IP = "192.168.1.50"
DEFAULT_STM32_PORT = 5000

DEFAULT_UART_PORT = "/dev/ttyUSB0"
DEFAULT_UART_BAUDRATE = 115200
DEFAULT_UART_TIMEOUT_S = 1.0

DEFAULT_COMMAND_FILE = "command.bin"

LOGICAL_CHANNEL_MIN = 0
LOGICAL_CHANNEL_MAX = 23
TGP_MIN = 0
TGP_MAX = 2
TRIG_MIN = 0
TRIG_MAX = 1
TRIG_V_MIN = -11.55
TRIG_V_MAX = 11.55
SEQ_TRIG_MIN = 0
SEQ_TRIG_MAX = 3
CHAIN_NONE = 0xFF

# Current firmware PWM generator is software-toggled from a 10kHz ISR base.
# With non-static duty (1..99%), minimum realizable period is 2 ISR ticks.
PWM_FREQ_MAX_HZ = 5000.0

PairAssignments = Iterable[Tuple[int, float]]
ChannelSequenceAssignments = Union[
    Mapping[int, Sequence[float]],
    Iterable[Tuple[int, Sequence[float]]],
]
PwmAssignment = Union[
    Tuple[int, int, float, float, float],
    Tuple[int, int, float, float, float, float],
]


def _validate_channel(channel: int) -> None:
    if channel < LOGICAL_CHANNEL_MIN or channel > LOGICAL_CHANNEL_MAX:
        raise ValueError(f"channel must be {LOGICAL_CHANNEL_MIN}-{LOGICAL_CHANNEL_MAX}, got {channel}")


def _validate_tgp(tgp_id: int) -> None:
    if tgp_id < TGP_MIN or tgp_id > TGP_MAX:
        raise ValueError(f"tgp_id must be {TGP_MIN}-{TGP_MAX}, got {tgp_id}")


def _validate_pwm(channel: int, tgp_id: int, freq: float) -> None:
    _validate_channel(channel)
    _validate_tgp(tgp_id)
    if freq <= 0.0:
        raise ValueError(f"freq must be > 0, got {freq}")
    if freq > PWM_FREQ_MAX_HZ:
        raise ValueError(
            f"freq must be <= {PWM_FREQ_MAX_HZ:g} Hz with current firmware timing, got {freq}"
        )


def _validate_pwm_duty(duty_cycle: float) -> int:
    duty = int(round(float(duty_cycle)))
    if duty < 1 or duty > 100:
        raise ValueError(f"duty_cycle must be in 1-100, got {duty_cycle}")
    return duty


def _validate_trig_channel(trig_id: int) -> None:
    if trig_id < TRIG_MIN or trig_id > TRIG_MAX:
        raise ValueError(f"trig_id must be {TRIG_MIN} or {TRIG_MAX}, got {trig_id}")


def _validate_trig_voltage(v_trig: float) -> None:
    if v_trig < TRIG_V_MIN or v_trig > TRIG_V_MAX:
        raise ValueError(f"trigger voltage must be in [{TRIG_V_MIN}, {TRIG_V_MAX}], got {v_trig}")


def _validate_sequence_trigger_pin(trigger_pin: int) -> None:
    if trigger_pin < SEQ_TRIG_MIN or trigger_pin > SEQ_TRIG_MAX:
        raise ValueError(f"trigger_pin must be {SEQ_TRIG_MIN}-{SEQ_TRIG_MAX}, got {trigger_pin}")


def _validate_sequence_edge(edge: int) -> int:
    value = int(edge)
    if value not in (0, 1):
        raise ValueError(f"edge must be 0 (falling) or 1 (rising), got {edge}")
    return value


def _validate_chain_target(channel: int) -> int:
    if channel == -1 or channel == CHAIN_NONE:
        return CHAIN_NONE
    _validate_channel(channel)
    return int(channel)


def _validate_ip_address(ip: str) -> list[int]:
    parts = ip.split(".")
    if len(parts) != 4:
        raise ValueError(f"ip must be dotted-quad like '192.168.1.50', got {ip!r}")

    octets = []
    for part in parts:
        try:
            value = int(part)
        except ValueError as exc:
            raise ValueError(f"ip contains non-integer octet {part!r}") from exc
        if value < 0 or value > 255:
            raise ValueError(f"ip octet out of range 0-255: {value}")
        octets.append(value)

    if octets == [0, 0, 0, 0] or octets == [255, 255, 255, 255]:
        raise ValueError(f"ip must not be {ip!r}")

    return octets


def _validate_tcp_port(port: int) -> int:
    value = int(port)
    if value < 1 or value > 65535:
        raise ValueError(f"port must be 1-65535, got {port}")
    return value


def code_from_voltage(volt: float) -> int:
    """Convert volts in [-10, 10] to a 16-bit DAC code."""
    if volt > 10.0:
        volt = 9.999999
    if volt < -10.0:
        volt = -9.999999

    code_val = int(((volt + 10.0) / 20.0) * 65535)
    if code_val > 0xFFFF:
        code_val = 0xFFFF
    return code_val


class VoltageSource:
    """Python API for a 24-channel DAC/voltage-source instrument.

    Commands are queued in an internal buffer and transmitted with send().
    """

    def __init__(
        self,
        transport: Optional[str] = None,
        ip: str = DEFAULT_STM32_IP,
        port: int = DEFAULT_STM32_PORT,
        uart_port: str = DEFAULT_UART_PORT,
        uart_baudrate: int = DEFAULT_UART_BAUDRATE,
        serial: Optional[str] = None,
        baud: Optional[int] = None,
        uart_timeout_s: float = DEFAULT_UART_TIMEOUT_S,
        command_file: Optional[str] = DEFAULT_COMMAND_FILE,
        check_connection_on_init: bool = True,
        init_check_timeout_s: float = 1.0,
        auto_send_commands: bool = True,
    ) -> None:
        inferred_transport = transport
        if inferred_transport is None:
            inferred_transport = "uart" if serial is not None else "ethernet"
        elif inferred_transport == "ethernet" and serial is not None:
            raise ValueError("transport='ethernet' conflicts with serial=...; use transport='uart' or omit transport")

        self.transport = inferred_transport
        self.ip = ip
        self.port = int(port)
        self.uart_port = serial if serial is not None else uart_port
        self.uart_baudrate = int(baud) if baud is not None else int(uart_baudrate)
        self.uart_timeout_s = float(uart_timeout_s)
        self.command_file = command_file
        self.check_connection_on_init = bool(check_connection_on_init)
        self.init_check_timeout_s = float(init_check_timeout_s)
        self.auto_send_commands = bool(auto_send_commands)

        self._buffer = bytearray()
        self._resource_manager = None
        self._transport_handle: Optional[Any] = None
        self._batch_depth = 0

        self._validate_transport_name()
        if self.check_connection_on_init:
            self._probe_connection_once()

    def _ensure_pyvisa(self) -> None:
        if pyvisa is None:
            raise ImportError("pyvisa is required for transport. Install with: pip install pyvisa pyvisa-py")

    def _ethernet_resource_name(self) -> str:
        return f"TCPIP0::{self.ip}::{self.port}::SOCKET"

    def _uart_resource_name(self) -> str:
        # Works with pyvisa-py for paths like /dev/ttyUSB0 and Windows COM ports.
        return f"ASRL{self.uart_port}::INSTR"

    def _build_resource_name(self) -> str:
        if self.transport == "ethernet":
            return self._ethernet_resource_name()
        if self.transport == "uart":
            return self._uart_resource_name()
        raise ValueError(f"Unsupported transport: {self.transport}")

    def _open_pyvisa_resource(self):
        self._ensure_pyvisa()
        if self._resource_manager is None:
            self._resource_manager = pyvisa.ResourceManager("@py")

        resource_name = self._build_resource_name()
        timeout_ms = max(1, int(self.uart_timeout_s * 1000.0))
        resource = self._resource_manager.open_resource(resource_name, timeout=timeout_ms)

        if self.transport == "uart":
            resource.baud_rate = int(self.uart_baudrate)
            resource.timeout = timeout_ms
            resource.write_termination = ""
            resource.read_termination = ""

        return resource

    def _should_send_now(self, send_immediately: Optional[bool]) -> bool:
        if send_immediately is not None:
            return bool(send_immediately)
        return self.auto_send_commands and self._batch_depth == 0

    def _send_if_needed(self, send_immediately: Optional[bool]) -> None:
        if self._should_send_now(send_immediately):
            self.send()

    def begin_batch(self) -> None:
        """Start batch mode: queue commands until send() is called."""
        self._batch_depth += 1

    def end_batch(self) -> None:
        """End batch mode previously started with begin_batch()."""
        if self._batch_depth == 0:
            raise RuntimeError("end_batch called without matching begin_batch")
        self._batch_depth -= 1

    @contextmanager
    def batch(self):
        """Context manager for batch mode where queue calls do not auto-send."""
        self.begin_batch()
        try:
            yield self
        finally:
            self.end_batch()

    def _probe_connection_once(self) -> None:
        """Fast one-shot probe used at instantiation time."""
        resource = None
        try:
            resource = self._open_pyvisa_resource()
        except Exception as exc:
            if self.transport == "ethernet":
                message = f"Connection check failed for {self.ip}:{self.port} ({exc})"
            else:
                message = f"Connection check failed for UART {self.uart_port} @ {self.uart_baudrate} ({exc})"
            raise ConnectionError(message) from exc
        finally:
            if resource is not None:
                resource.close()

    def _validate_transport_name(self) -> None:
        if self.transport not in ("ethernet", "uart"):
            raise ValueError("transport must be 'ethernet' or 'uart'")

    def configure(
        self,
        transport: Optional[str] = None,
        ip: Optional[str] = None,
        port: Optional[int] = None,
        uart_port: Optional[str] = None,
        uart_baudrate: Optional[int] = None,
        serial: Optional[str] = None,
        baud: Optional[int] = None,
        uart_timeout_s: Optional[float] = None,
    ) -> None:
        """Update transport settings at runtime."""
        if transport is not None:
            self.transport = transport
            self._validate_transport_name()
        if ip is not None:
            self.ip = ip
        if port is not None:
            self.port = int(port)
        if uart_port is not None:
            self.uart_port = uart_port
        if uart_baudrate is not None:
            self.uart_baudrate = int(uart_baudrate)
        if serial is not None:
            self.transport = "uart"
            self.uart_port = serial
        if baud is not None:
            self.uart_baudrate = int(baud)
        if uart_timeout_s is not None:
            self.uart_timeout_s = float(uart_timeout_s)

    def connect(
        self,
        ip: Optional[str] = None,
        port: Optional[int] = None,
        serial: Optional[str] = None,
        baud: Optional[int] = None,
    ) -> None:
        """Open selected transport."""
        if self._transport_handle is not None:
            return

        if ip is not None:
            self.transport = "ethernet"
            self.ip = ip
        if port is not None:
            self.transport = "ethernet"
            self.port = int(port)
        if serial is not None:
            self.transport = "uart"
            self.uart_port = serial
        if baud is not None:
            self.transport = "uart"
            self.uart_baudrate = int(baud)

        self._validate_transport_name()
        self._transport_handle = self._open_pyvisa_resource()

    def close(self) -> None:
        if self._transport_handle is None:
            return
        try:
            self._transport_handle.close()
        finally:
            self._transport_handle = None

    def __enter__(self) -> "VoltageSource":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        self.close()
        return False

    def reset_buffer(self) -> None:
        self._buffer.clear()

    def _append_voltage_code(self, channel: int, code_value: int) -> None:
        self._buffer.extend(struct.pack("<B B H", 1, channel, code_value))

    def set_voltage(self, channel: int, voltage: float, send_immediately: Optional[bool] = None) -> None:
        """Queue one voltage setpoint in physical volts."""
        _validate_channel(channel)
        self._append_voltage_code(channel, code_from_voltage(float(voltage)))
        self._send_if_needed(send_immediately)

    def set_voltages(
        self,
        assignments: Optional[Union[PairAssignments, Mapping[int, float]]] = None,
        *,
        channels: Optional[Sequence[int]] = None,
        voltages: Optional[Sequence[float]] = None,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Queue multiple voltages.

        Supported forms:
        - set_voltages({0: 1.0, 1: -2.0})
        - set_voltages([(0, 1.0), (1, -2.0)])
        - set_voltages(channels=[0, 1], voltages=[1.0, -2.0])
        """
        if assignments is not None and (channels is not None or voltages is not None):
            raise ValueError("Use either 'assignments' or ('channels' and 'voltages'), not both")

        if assignments is not None:
            if isinstance(assignments, Mapping):
                items = assignments.items()
            else:
                items = assignments
            for channel, voltage in items:
                self.set_voltage(int(channel), float(voltage), send_immediately=False)
            self._send_if_needed(send_immediately)
            return

        if channels is None or voltages is None:
            raise ValueError("Provide assignments, or both channels and voltages")
        if len(channels) != len(voltages):
            raise ValueError(f"channels and voltages length mismatch: {len(channels)} != {len(voltages)}")

        for channel, voltage in zip(channels, voltages):
            self.set_voltage(int(channel), float(voltage), send_immediately=False)
        self._send_if_needed(send_immediately)

    def start_pwm(
        self,
        channel: int,
        tgp_id: int,
        low: float,
        high: float,
        freq: float,
        duty_cycle: float = 50.0,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Queue PWM start command on any logical channel 0-23."""
        _validate_pwm(channel, tgp_id, float(freq))
        duty = _validate_pwm_duty(duty_cycle)
        packed = struct.pack(
            "<BBBBHHf",
            2,
            channel,
            tgp_id,
            duty,
            code_from_voltage(low),
            code_from_voltage(high),
            float(freq),
        )
        self._buffer.extend(packed)
        self._send_if_needed(send_immediately)

    def stop_pwm(self, channel: int, tgp_id: Optional[int] = None, send_immediately: Optional[bool] = None) -> None:
        _validate_channel(channel)
        if tgp_id is None:
            tgp_value = 0xFF
        else:
            _validate_tgp(tgp_id)
            tgp_value = int(tgp_id)

        self.set_voltage(channel, 0.0, send_immediately=False)
        self._buffer.extend(struct.pack("<B B B", 3, channel, tgp_value))
        self._send_if_needed(send_immediately)

    def assign_pwm(self, assignments: Iterable[PwmAssignment], send_immediately: Optional[bool] = None) -> None:
        """Bulk-assign PWM.

        Each item is either:
        - (channel_0_to_23, tgp_id_0_to_2, low_volt, high_volt, freq_hz)
        - (channel_0_to_23, tgp_id_0_to_2, low_volt, high_volt, freq_hz, duty_cycle)
        """
        for item in assignments:
            if len(item) == 5:
                channel, tgp_id, low, high, freq = item
                duty_cycle = 50.0
            elif len(item) == 6:
                channel, tgp_id, low, high, freq, duty_cycle = item
            else:
                raise ValueError("PWM assignment must have 5 or 6 elements")

            self.start_pwm(
                int(channel),
                int(tgp_id),
                float(low),
                float(high),
                float(freq),
                float(duty_cycle),
                send_immediately=False,
            )
        self._send_if_needed(send_immediately)

    def send_voltage_sequence(
        self,
        dac_channel: int,
        trigger_pin: int,
        edge: int,
        voltages: Sequence[float],
        send_immediately: Optional[bool] = None,
    ) -> None:
        _validate_channel(dac_channel)
        _validate_sequence_trigger_pin(trigger_pin)
        edge_value = _validate_sequence_edge(edge)

        num_points = len(voltages)
        header = struct.pack("<BBBBH", 4, dac_channel, trigger_pin, edge_value, num_points)
        self._buffer.extend(header)
        for v in voltages:
            self._buffer.extend(struct.pack("<H", code_from_voltage(float(v))))
        self._send_if_needed(send_immediately)

    def send_voltage_sequences_on_trigger(
        self,
        trigger_pin: int,
        edge: int,
        assignments: ChannelSequenceAssignments,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Queue multiple external-triggered sequences sharing one trigger input.

        Example assignments forms:
        - {0: [0.0, 1.0, 0.0], 3: [-1.0, 0.0, 1.0]}
        - [(0, [0.0, 1.0, 0.0]), (3, [-1.0, 0.0, 1.0])]
        """
        _validate_sequence_trigger_pin(trigger_pin)
        edge_value = _validate_sequence_edge(edge)

        if isinstance(assignments, Mapping):
            items = assignments.items()
        else:
            items = assignments

        for channel, voltages in items:
            self.send_voltage_sequence(
                int(channel),
                int(trigger_pin),
                edge_value,
                voltages,
                send_immediately=False,
            )

        self._send_if_needed(send_immediately)

    def send_soft_sequence(
        self,
        dac_channel: int,
        delay_ms: int,
        voltages: Sequence[float],
        send_immediately: Optional[bool] = None,
    ) -> None:
        _validate_channel(dac_channel)

        num_points = len(voltages)
        header = struct.pack("<BBHH", 5, dac_channel, int(delay_ms), num_points)
        self._buffer.extend(header)
        for v in voltages:
            self._buffer.extend(struct.pack("<H", code_from_voltage(float(v))))
        self._send_if_needed(send_immediately)

    def send_chained_soft_sequence(
        self,
        dac_channel: int,
        delay_ms: int,
        voltages: Sequence[float],
        *,
        irq_every_points: int,
        next_dac_channel: int = -1,
        start_on_internal_irq: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Queue a chained soft-sequence command.

        The source sequence emits an internal event every irq_every_points samples.
        A downstream sequence can be armed with start_on_internal_irq=True and will
        consume one internal event per sample step.
        """
        _validate_channel(dac_channel)

        if irq_every_points < 0:
            raise ValueError(f"irq_every_points must be >= 0, got {irq_every_points}")

        chain_target = _validate_chain_target(int(next_dac_channel))
        num_points = len(voltages)

        header = struct.pack(
            "<BBHHBBH",
            10,
            int(dac_channel),
            int(delay_ms),
            num_points,
            chain_target,
            1 if start_on_internal_irq else 0,
            int(irq_every_points),
        )
        self._buffer.extend(header)
        for v in voltages:
            self._buffer.extend(struct.pack("<H", code_from_voltage(float(v))))
        self._send_if_needed(send_immediately)


    def set_trigger_level(self, trig_id: int, v_trig: float, send_immediately: Optional[bool] = None) -> None:
        _validate_trig_channel(trig_id)
        _validate_trig_voltage(float(v_trig))

        self._buffer.extend(struct.pack("<BBf", 7, int(trig_id), float(v_trig)))
        self._send_if_needed(send_immediately)

    def set_network_config(self, ip: str, port: int, send_immediately: Optional[bool] = None) -> None:
        """Persist network endpoint (IP + TCP port) to device flash.

        The device applies this immediately and also reloads it on future boots.
        """
        ip_octets = _validate_ip_address(ip)
        port_value = _validate_tcp_port(port)
        self._buffer.extend(struct.pack("<B4BH", 8, *ip_octets, port_value))
        self._send_if_needed(send_immediately)

    def clear_state(self, send_immediately: Optional[bool] = None) -> None:
        """Queue firmware runtime-state clear (PWM/TGP, sequence, trigger engine)."""
        self._buffer.extend(struct.pack("<B", 9))
        self._send_if_needed(send_immediately)

    def stop_sequence_channel(
        self,
        dac_channel: int,
        *,
        zero_output: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Stop and unbind one channel sequence in firmware.

        This removes the channel from trigger slot lists and deactivates
        all sequence modes for that channel.
        """
        _validate_channel(dac_channel)
        flags = 0x01 if zero_output else 0x00
        self._buffer.extend(struct.pack("<BBB", 11, int(dac_channel), flags))
        self._send_if_needed(send_immediately)

    def send(
        self,
        command_file: Optional[str] = None,
        append_end: bool = True,
        echo_bytes: bool = False,
        auto_connect: bool = True,
    ) -> None:
        """Transmit queued buffer through transport.

        If not connected, this will connect automatically by default.
        """
        if self._transport_handle is None:
            if auto_connect:
                self.connect()
            if self._transport_handle is None:
                raise RuntimeError("Transport is not connected. Call connect() first.")

        payload = bytes(self._buffer)
        if append_end:
            payload += b"e"

        resolved_command_file = self.command_file if command_file is None else command_file
        if resolved_command_file:
            Path(resolved_command_file).write_bytes(payload)

        try:
            self._transport_handle.write_raw(payload)
        except Exception:
            # Firmware may close transport after each command; reconnect and retry once.
            self.close()
            self.connect()
            if self._transport_handle is None:
                raise RuntimeError("Transport is not connected")
            self._transport_handle.write_raw(payload)

        if echo_bytes:
            print(payload)

        self._buffer.clear()

        # Firmware currently handles one TCP packet per Ethernet connection,
        # so proactively close to guarantee a fresh reconnect on the next send.
        if self.transport == "ethernet":
            self.close()

    def execute(
        self,
        builder: Callable[["VoltageSource"], None],
        *,
        reset_before: bool = True,
        command_file: Optional[str] = None,
        append_end: bool = True,
        echo_bytes: bool = False,
        auto_connect: bool = True,
    ) -> None:
        """Queue commands via builder and send once."""
        if reset_before:
            self.reset_buffer()

        self.begin_batch()
        try:
            builder(self)
        finally:
            self.end_batch()

        self.send(
            command_file=command_file,
            append_end=append_end,
            echo_bytes=echo_bytes,
            auto_connect=auto_connect,
        )

    def channel(self, dac_channel: int) -> "VoltageSourceChannel":
        """Return a channel-scoped wrapper bound to one DAC channel."""
        _validate_channel(int(dac_channel))
        return VoltageSourceChannel(self, int(dac_channel))

    def trigger(self, trig_id: int) -> "VoltageSourceTriggerChannel":
        """Return a trigger-scoped wrapper bound to one trigger input."""
        _validate_trig_channel(int(trig_id))
        return VoltageSourceTriggerChannel(self, int(trig_id))


class VoltageSourceChannel:
    """Convenience wrapper that binds calls to one logical DAC channel."""

    def __init__(self, instr: VoltageSource, port: int) -> None:
        self.instr = instr
        self.port = int(port)
        _validate_channel(self.port)
        self._sequence_trigger_pin = 1
        self._sequence_trigger_edge = 1

    def _configure_external_trigger(self, trigger_pin: int, edge: int = 1) -> None:
        _validate_sequence_trigger_pin(int(trigger_pin))
        self._sequence_trigger_pin = int(trigger_pin)
        self._sequence_trigger_edge = _validate_sequence_edge(int(edge))

    def unbind_external_trigger(self) -> None:
        """Clear external-trigger mapping for this channel."""
        self._sequence_trigger_pin = 0xFF
        self._sequence_trigger_edge = 1

    def bind_trigger(self, trigger_pin: int, edge: int = 1) -> None:
        """Bind this DAC channel to an external trigger pin and edge."""
        self._configure_external_trigger(trigger_pin=trigger_pin, edge=edge)

    def set_voltage(self, voltage: float, send_immediately: Optional[bool] = None) -> None:
        self.instr.set_voltage(self.port, float(voltage), send_immediately=send_immediately)

    def start_pwm(
        self,
        tgp_id: int,
        low: float,
        high: float,
        freq: float,
        duty_cycle: float = 50.0,
        send_immediately: Optional[bool] = None,
    ) -> None:
        self.instr.start_pwm(
            self.port,
            int(tgp_id),
            float(low),
            float(high),
            float(freq),
            float(duty_cycle),
            send_immediately=send_immediately,
        )

    def stop_pwm(self, tgp_id: Optional[int] = None, send_immediately: Optional[bool] = None) -> None:
        self.instr.stop_pwm(self.port, tgp_id=tgp_id, send_immediately=send_immediately)

    def trigger_sequence(
        self,
        voltages: Sequence[float],
        trigger_pin: Optional[int] = None,
        edge: Optional[int] = None,
        send_immediately: Optional[bool] = None,
    ) -> None:
        use_trigger_pin = self._sequence_trigger_pin if trigger_pin is None else int(trigger_pin)
        if use_trigger_pin == 0xFF:
            raise RuntimeError("Channel is not bound to an external trigger. Call bind_trigger(...) or trigger.bind(...) first.")
        use_edge = self._sequence_trigger_edge if edge is None else int(edge)
        self.instr.send_voltage_sequence(
            self.port,
            use_trigger_pin,
            use_edge,
            voltages,
            send_immediately=send_immediately,
        )


class VoltageSourceTriggerChannel:
    """Convenience wrapper bound to one external trigger input (trig_id 0/1)."""

    def __init__(self, instr: VoltageSource, port: int) -> None:
        self.instr = instr
        self.port = int(port)
        _validate_trig_channel(self.port)
        self.edge = 1

    def _resolve_target_channel(self, target: Union[int, VoltageSourceChannel]) -> VoltageSourceChannel:
        if isinstance(target, VoltageSourceChannel):
            return target
        return self.instr.channel(int(target))

    def bind(self, target: Union[int, VoltageSourceChannel, Sequence[Union[int, VoltageSourceChannel]]], edge: Optional[int] = None) -> None:
        """Bind one or multiple DAC channels to this trigger input."""
        use_edge = self.edge if edge is None else _validate_sequence_edge(int(edge))
        self.edge = use_edge

        if isinstance(target, Sequence) and not isinstance(target, (str, bytes, bytearray)):
            for item in target:
                channel = self._resolve_target_channel(item)
                channel._configure_external_trigger(trigger_pin=self.port, edge=use_edge)
            return

        channel = self._resolve_target_channel(target)
        channel._configure_external_trigger(trigger_pin=self.port, edge=use_edge)

    def unbind(
        self,
        target: Optional[Union[int, VoltageSourceChannel, Sequence[Union[int, VoltageSourceChannel]]]] = None,
        *,
        stop_running: bool = False,
        zero_output: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Unbind external trigger from one channel, many channels, or all channels.

        If stop_running=True, a firmware stop command is queued for each affected
        channel so active execution halts immediately on the device.
        """
        channels: list[VoltageSourceChannel] = []

        if target is None:
            for ch in range(LOGICAL_CHANNEL_MIN, LOGICAL_CHANNEL_MAX + 1):
                channels.append(self.instr.channel(ch))
        elif isinstance(target, Sequence) and not isinstance(target, (str, bytes, bytearray)):
            for item in target:
                channels.append(self._resolve_target_channel(item))
        else:
            channels.append(self._resolve_target_channel(target))

        for channel in channels:
            channel.unbind_external_trigger()
            if stop_running:
                self.instr.stop_sequence_channel(
                    channel.port,
                    zero_output=zero_output,
                    send_immediately=False,
                )

        if stop_running:
            self.instr._send_if_needed(send_immediately)

    def set_level(self, level: float = 3.3, send_immediately: Optional[bool] = None) -> None:
        self.instr.set_trigger_level(self.port, float(level), send_immediately=send_immediately)

    def trigger_sequences(
        self,
        assignments: ChannelSequenceAssignments,
        edge: Optional[int] = None,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Queue multi-channel external-triggered sequences on this trigger input."""
        use_edge = self.edge if edge is None else _validate_sequence_edge(int(edge))
        self.edge = use_edge
        self.instr.send_voltage_sequences_on_trigger(
            trigger_pin=self.port,
            edge=use_edge,
            assignments=assignments,
            send_immediately=send_immediately,
        )

    def soft_sequence(self, delay_ms: int, voltages: Sequence[float], send_immediately: Optional[bool] = None) -> None:
        self.instr.send_soft_sequence(
            self.port,
            int(delay_ms),
            voltages,
            send_immediately=send_immediately,
        )

    def chained_soft_sequence(
        self,
        delay_ms: int,
        voltages: Sequence[float],
        *,
        irq_every_points: int,
        next_dac_channel: int = -1,
        start_on_internal_irq: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        self.instr.send_chained_soft_sequence(
            self.port,
            int(delay_ms),
            voltages,
            irq_every_points=int(irq_every_points),
            next_dac_channel=int(next_dac_channel),
            start_on_internal_irq=bool(start_on_internal_irq),
            send_immediately=send_immediately,
        )

    def enable_irq(
        self,
        delay_ms: int,
        voltages: Sequence[float],
        *,
        irq_every_points: int,
        next_dac_channel: int = -1,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Source role: emit internal IRQ events while running this sequence."""
        self.chained_soft_sequence(
            delay_ms,
            voltages,
            irq_every_points=int(irq_every_points),
            next_dac_channel=int(next_dac_channel),
            start_on_internal_irq=False,
            send_immediately=send_immediately,
        )

    def set_on_irq(
        self,
        delay_ms: int,
        voltages: Sequence[float],
        *,
        irq_every_points: int = 0,
        next_dac_channel: int = -1,
        send_immediately: Optional[bool] = None,
    ) -> None:
        """Sink role: arm this sequence and start it from internal IRQ events."""
        self.chained_soft_sequence(
            delay_ms,
            voltages,
            irq_every_points=int(irq_every_points),
            next_dac_channel=int(next_dac_channel),
            start_on_internal_irq=True,
            send_immediately=send_immediately,
        )

# ========================
# Optional legacy module-level API (singleton)
# ========================
_default_device = VoltageSource(check_connection_on_init=False, auto_send_commands=False)


def configure_transport(
    transport: Optional[str] = None,
    ip: Optional[str] = None,
    port: Optional[int] = None,
    uart_port: Optional[str] = None,
    uart_baudrate: Optional[int] = None,
    serial: Optional[str] = None,
    baud: Optional[int] = None,
    uart_timeout_s: Optional[float] = None,
) -> None:
    _default_device.configure(
        transport=transport,
        ip=ip,
        port=port,
        uart_port=uart_port,
        uart_baudrate=uart_baudrate,
        serial=serial,
        baud=baud,
        uart_timeout_s=uart_timeout_s,
    )


def connect_transport(
    ip: Optional[str] = None,
    port: Optional[int] = None,
    serial: Optional[str] = None,
    baud: Optional[int] = None,
) -> None:
    _default_device.connect(ip=ip, port=port, serial=serial, baud=baud)


def close_transport() -> None:
    _default_device.close()


def reset_buffer() -> None:
    _default_device.reset_buffer()


def set_voltage(channel: int, voltage: float) -> None:
    _default_device.set_voltage(channel, voltage)


def set_voltages(
    assignments: Optional[Union[PairAssignments, Mapping[int, float]]] = None,
    *,
    channels: Optional[Sequence[int]] = None,
    voltages: Optional[Sequence[float]] = None,
) -> None:
    _default_device.set_voltages(assignments=assignments, channels=channels, voltages=voltages)


def start_pwm(channel: int, tgp_id: int, low: float, high: float, freq: float, duty_cycle: float = 50.0) -> None:
    _default_device.start_pwm(channel, tgp_id, low, high, freq, duty_cycle)


def stop_pwm(channel: int, tgp_id: Optional[int] = None) -> None:
    _default_device.stop_pwm(channel, tgp_id)


def assign_pwm(assignments: Iterable[PwmAssignment]) -> None:
    _default_device.assign_pwm(assignments)


def send_voltage_sequence(dac_channel: int, trigger_pin: int, edge: int, voltages: Sequence[float]) -> None:
    _default_device.send_voltage_sequence(dac_channel, trigger_pin, edge, voltages)


def send_voltage_sequences_on_trigger(
    trigger_pin: int,
    edge: int,
    assignments: ChannelSequenceAssignments,
) -> None:
    _default_device.send_voltage_sequences_on_trigger(trigger_pin, edge, assignments)


def send_soft_sequence(dac_channel: int, delay_ms: int, voltages: Sequence[float]) -> None:
    _default_device.send_soft_sequence(dac_channel, delay_ms, voltages)


def send_chained_soft_sequence(
    dac_channel: int,
    delay_ms: int,
    voltages: Sequence[float],
    *,
    irq_every_points: int,
    next_dac_channel: int = -1,
    start_on_internal_irq: bool = False,
) -> None:
    _default_device.send_chained_soft_sequence(
        dac_channel,
        delay_ms,
        voltages,
        irq_every_points=irq_every_points,
        next_dac_channel=next_dac_channel,
        start_on_internal_irq=start_on_internal_irq,
    )


def set_trigger_level(trig_id: int, v_trig: float) -> None:
    _default_device.set_trigger_level(trig_id, v_trig)


def set_network_config(ip: str, port: int) -> None:
    _default_device.set_network_config(ip, port)


def clear_state() -> None:
    _default_device.clear_state()


def stop_sequence_channel(
    dac_channel: int,
    *,
    zero_output: bool = False,
) -> None:
    _default_device.stop_sequence_channel(dac_channel, zero_output=zero_output)


def send(
    command_file: Optional[str] = DEFAULT_COMMAND_FILE,
    append_end: bool = True,
    echo_bytes: bool = False,
    auto_connect: bool = True,
) -> None:
    _default_device.send(
        command_file=command_file,
        append_end=append_end,
        echo_bytes=echo_bytes,
        auto_connect=auto_connect,
    )


def run_default_demo() -> None:
    connect_transport()
    try:
        reset_buffer()
        step = 20.0 / 23.0
        values = [(-10.0 + step * i) for i in range(24)]
        _default_device.set_voltages(channels=list(range(24)), voltages=values)
        send(command_file=DEFAULT_COMMAND_FILE, append_end=True, echo_bytes=False)
    finally:
        close_transport()


