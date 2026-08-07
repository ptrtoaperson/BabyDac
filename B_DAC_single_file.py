from contextlib import contextmanager
import struct
from typing import Any, Iterable, Mapping, Optional, Sequence, Tuple, Union


class B_DAC(PortInstrument, PyVisaInstrument):
    """Single-file consolidated B_DAC implementation."""

    ChannelSequenceAssignments = Union[
        Mapping[int, Sequence[float]],
        Iterable[Tuple[int, Sequence[float]]],
    ]

    def __init__(self, **kwargs):
        self.visa_backend = "@py"
        self.auto_send_commands = bool(kwargs.get("auto_send_commands", True))
        self._batch_depth = 0

        instr_data = InstrumentData.get(self.__class__.__name__, {})
        self.MAX_PACKET_BYTES = int(
            instr_data.get(
                "MAX_PACKET_BYTES",
                instr_data.get(" MAX_PACKET_BYTES", 2 * 1024),
            )
        )

        for parentClass in (PortInstrument, PyVisaInstrument):
            parentClass.__init__(self, **(kwargs | instr_data))

        if getattr(self, "transport", "") == "serial" and hasattr(self, "baud_rate"):
            self.instr.baud_rate = self.baud_rate

        # Instance-scoped command buffer.
        self._buffer = bytearray()

    def begin_batch(self) -> None:
        self._batch_depth += 1

    def end_batch(self) -> None:
        if self._batch_depth == 0:
            raise RuntimeError("end_batch called without matching begin_batch")
        self._batch_depth -= 1

    @contextmanager
    def batch(self):
        self.begin_batch()
        try:
            yield self
        finally:
            self.end_batch()

    def _should_send_now(self, send_immediately: Optional[bool]) -> bool:
        if send_immediately is not None:
            return bool(send_immediately)
        return self.auto_send_commands and self._batch_depth == 0

    def _send_if_needed(self, send_immediately: Optional[bool]) -> None:
        if self._should_send_now(send_immediately):
            self.send()

    def send(self, append_end: bool = True) -> bytes:
        if not self._buffer:
            return b""

        payload = bytes(self._buffer)
        if append_end:
            payload += b"e"

        if len(payload) > self.MAX_PACKET_BYTES:
            print(
                "Packet too large ("
                + str(len(payload))
                + " bytes). Maximum allowed is "
                + str(self.MAX_PACKET_BYTES)
                + " bytes. Not sending."
            )
            self._buffer.clear()
            return b""

        response = b""
        try:
            self._write_raw(payload)
            try:
                response = self.instr.read_bytes(2)
            except Exception:
                response = b""
        except (BrokenPipeError, ConnectionResetError, OSError):
            print("Connection error")
            return response
        finally:
            self._buffer.clear()

        return response

    def _validate_ip_address(self, ip_address: str) -> list[int]:
        parts = ip_address.split(".")
        if len(parts) != 4:
            raise ValueError("ip must be dotted-quad like '192.168.1.50'")

        octets = []
        for part in parts:
            try:
                value = int(part)
            except ValueError as exc:
                raise ValueError("ip contains non-integer octet") from exc
            if value < 0 or value > 255:
                raise ValueError("ip octet out of range 0-255")
            octets.append(value)

        if octets == [0, 0, 0, 0] or octets == [255, 255, 255, 255]:
            raise ValueError("ip must not be 0.0.0.0 or 255.255.255.255")

        return octets

    def _validate_tcp_port(self, port: Union[str, int]) -> int:
        try:
            value = int(port)
        except (ValueError, TypeError):
            raise ValueError("port must be a valid integer")

        if value < 1 or value > 65535:
            raise ValueError("port must be 1-65535")
        return value

    def set_network_config(self, ip_address: str, port: int, send_immediately: Optional[bool] = None) -> None:
        ip_octets = self._validate_ip_address(ip_address)
        port_value = self._validate_tcp_port(port)
        print("network setting changed to " + ip_address + ":" + str(port_value))
        print("reinitialization of the device is required for this to take effect")
        self._buffer.extend(struct.pack("<B4BH", 8, *ip_octets, port_value))
        self._send_if_needed(send_immediately)

    def code_from_voltage(self, volt: float) -> int:
        if volt > 10.0:
            volt = 9.999999
            print("voltages should be in the range of -10 to 10 volts, setting voltage to 10V")
        if volt < -10.0:
            volt = -9.999999
            print("voltages should be in the range of -10 to 10 volts, setting voltage to -10V")

        code_val = int(((volt + 10.0) / 20.0) * 65535)
        if code_val > 0xFFFF:
            code_val = 0xFFFF
        return code_val

    def _clear_host_sequence_bindings(self) -> None:
        for port in getattr(self, "ports", []):
            # DAC channel wrappers cache internal and external bind state locally.
            if hasattr(port, "_irq_every_points"):
                port._irq_every_points = None
            if hasattr(port, "_irq_next_dac_channel"):
                port._irq_next_dac_channel = -1
            if hasattr(port, "_irq_next_dac_channels"):
                port._irq_next_dac_channels = []
            if hasattr(port, "_irq_fanout"):
                port._irq_fanout = []
            if hasattr(port, "_sequence_trigger_pin"):
                port._sequence_trigger_pin = 0xFF
            if hasattr(port, "_sequence_trigger_edge"):
                port._sequence_trigger_edge = 1

    def clear_state(
        self,
        send_immediately: Optional[bool] = None,
        *,
        clear_host_bindings: bool = True,
    ) -> None:
        self._buffer.extend(struct.pack("<B", 9))
        if clear_host_bindings:
            self._clear_host_sequence_bindings()
        self._send_if_needed(send_immediately)

    def stop_sequence_channel(
        self,
        dac_channel: int,
        *,
        zero_output: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        flags = 0x01 if zero_output else 0x00
        self._buffer.extend(struct.pack("<BBB", 11, int(dac_channel), flags))
        self._send_if_needed(send_immediately)

    def _append_voltage_code(self, channel: int, code_value: int) -> None:
        self._buffer.extend(struct.pack("<BBH", 1, int(channel), int(code_value)))

    def set_voltage(self, channel: int, voltage: float, send_immediately: Optional[bool] = None) -> None:
        self._append_voltage_code(channel, self.code_from_voltage(float(voltage)))
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
        if tgp_id < self.TGP_MIN or tgp_id > self.TGP_MAX:
            raise ValueError(f"tgp_id must be in the range of {self.TGP_MIN} to {self.TGP_MAX}")
        if freq <= 0.0:
            raise ValueError("freq must be > 0")

        duty = int(round(float(duty_cycle)))
        if duty < 1 or duty > 100:
            raise ValueError("duty_cycle must be in 1-100")

        packed = struct.pack(
            "<BBBBHHf",
            2,
            int(channel),
            int(tgp_id),
            duty,
            self.code_from_voltage(low),
            self.code_from_voltage(high),
            float(freq),
        )
        self._buffer.extend(packed)
        self._send_if_needed(send_immediately)

    def stop_pwm(self, channel: int, tgp_id: Optional[int] = None, send_immediately: Optional[bool] = None) -> None:
        if tgp_id is None:
            tgp_value = self.CHAIN_NONE
        else:
            if tgp_id < self.TGP_MIN or tgp_id > self.TGP_MAX:
                raise ValueError(f"tgp_id must be in the range of {self.TGP_MIN} to {self.TGP_MAX}")
            tgp_value = int(tgp_id)

        self.set_voltage(channel, 0.0, send_immediately=False)
        self._buffer.extend(struct.pack("<BBB", 3, int(channel), int(tgp_value)))
        self._send_if_needed(send_immediately)

    def send_soft_sequence(
        self,
        dac_channel: int,
        delay_ms: int,
        voltages: Sequence[float],
        send_immediately: Optional[bool] = None,
    ) -> None:
        num_points = len(voltages)
        self._buffer.extend(struct.pack("<BBHH", 5, int(dac_channel), int(delay_ms), num_points))
        for v in voltages:
            self._buffer.extend(struct.pack("<H", self.code_from_voltage(float(v))))
        self._send_if_needed(send_immediately)

    def send_chained_soft_sequence(
        self,
        dac_channel: int,
        delay_ms: int,
        voltages: Sequence[float],
        *,
        irq_every_points: int,
        next_dac_channel: int = -1,
        next_dac_channels: Optional[Sequence[int]] = None,
        next_dac_fanout: Optional[Sequence[Tuple[int, int]]] = None,
        start_on_internal_irq: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        if irq_every_points < 0:
            raise ValueError("irq_every_points must be >= 0")

        chain_targets: list[int] = []
        if next_dac_channels is not None:
            for raw in next_dac_channels:
                tgt = int(raw)
                if tgt == -1 or tgt == self.CHAIN_NONE:
                    continue
                if tgt < 0 or tgt > 23:
                    raise ValueError("all next_dac_channels must be in 0-23, or CHAIN_NONE/-1")
                if tgt not in chain_targets:
                    chain_targets.append(tgt)
        else:
            chain_target = int(next_dac_channel)
            if chain_target != -1 and chain_target != self.CHAIN_NONE:
                if chain_target < 0 or chain_target > 23:
                    raise ValueError("next_dac_channel must be in 0-23, or CHAIN_NONE/-1")
                chain_targets.append(chain_target)

        chain_target = chain_targets[0] if chain_targets else self.CHAIN_NONE

        num_points = len(voltages)
        self._buffer.extend(
            struct.pack(
                "<BBHHBBH",
                10,
                int(dac_channel),
                int(delay_ms),
                num_points,
                chain_target,
                1 if start_on_internal_irq else 0,
                int(irq_every_points),
            )
        )
        for v in voltages:
            self._buffer.extend(struct.pack("<H", self.code_from_voltage(float(v))))

        fanout_entries: list[tuple[int, int]] = []
        if next_dac_fanout:
            for raw_target, raw_irq in next_dac_fanout:
                tgt = int(raw_target)
                every = int(raw_irq)

                if tgt == -1 or tgt == self.CHAIN_NONE:
                    continue
                if tgt < 0 or tgt > 23:
                    raise ValueError("all fanout targets must be in 0-23, or CHAIN_NONE/-1")
                if every <= 0:
                    raise ValueError("all fanout irq_every_points values must be > 0")

                duplicate_idx = None
                for idx, (existing_tgt, _) in enumerate(fanout_entries):
                    if existing_tgt == tgt:
                        duplicate_idx = idx
                        break

                if duplicate_idx is not None:
                    fanout_entries[duplicate_idx] = (tgt, every)
                else:
                    fanout_entries.append((tgt, every))

            if fanout_entries:
                self._buffer.extend(struct.pack("<BBB", 13, int(dac_channel), len(fanout_entries)))
                for tgt, every in fanout_entries:
                    self._buffer.extend(struct.pack("<BH", tgt, every))

        # Do not send command 12 when per-follower IRQ fanout (command 13) is present,
        # because command 12 would reset per-follower divisors on the firmware side.
        if len(chain_targets) > 1 and not fanout_entries:
            self._buffer.extend(struct.pack("<BBB", 12, int(dac_channel), len(chain_targets)))
            self._buffer.extend(struct.pack("<" + "B" * len(chain_targets), *chain_targets))

        self._send_if_needed(send_immediately)

    def _validate_sequence_edge(self, edge: int) -> int:
        value = int(edge)
        if value not in (0, 1):
            raise ValueError("edge must be 0 (falling) or 1 (rising)")
        return value

    def _validate_trigger_pin(self, trigger_pin: int) -> int:
        pin = int(trigger_pin)
        if pin < 0 or pin > 3:
            raise ValueError("trigger_pin must be in 0-3")
        return pin

    def _validate_trig_id(self, trig_id: int) -> int:
        value = int(trig_id)
        if value not in (0, 1):
            raise ValueError("trig_id must be 0 or 1")
        return value

    def set_trigger_level(self, trig_id: int, v_trig: float, send_immediately: Optional[bool] = None) -> None:
        tid = self._validate_trig_id(trig_id)
        if v_trig < self.TRIG_V_MIN or v_trig > self.TRIG_V_MAX:
            raise ValueError("trigger voltage must be in the range limits")
        self._buffer.extend(struct.pack("<BBf", 7, tid, float(v_trig)))
        self._send_if_needed(send_immediately)

    def send_voltage_sequence(
        self,
        dac_channel: int,
        trigger_pin: int,
        edge: int,
        voltages: Sequence[float],
        send_immediately: Optional[bool] = None,
    ) -> None:
        pin = self._validate_trigger_pin(trigger_pin)
        edge_value = self._validate_sequence_edge(edge)
        num_points = len(voltages)

        self._buffer.extend(struct.pack("<BBBBH", 4, int(dac_channel), pin, edge_value, num_points))
        for v in voltages:
            self._buffer.extend(struct.pack("<H", self.code_from_voltage(float(v))))
        self._send_if_needed(send_immediately)

    def send_voltage_sequences_on_trigger(
        self,
        trigger_pin: int,
        edge: int,
        assignments: ChannelSequenceAssignments,
        send_immediately: Optional[bool] = None,
    ) -> None:
        pin = self._validate_trigger_pin(trigger_pin)
        edge_value = self._validate_sequence_edge(edge)

        items = assignments.items() if isinstance(assignments, Mapping) else assignments
        for ch, volts in items:
            self.send_voltage_sequence(int(ch), pin, edge_value, volts, send_immediately=False)

        self._send_if_needed(send_immediately)


class B_DAC_Channel(Instrument_Port):
    generic_instr = "voltage_source"

    def __init__(self, **kwargs):
        Instrument_Port.__init__(self, **kwargs)
        self._irq_every_points: Optional[int] = None
        self._irq_next_dac_channel: int = -1
        self._irq_next_dac_channels: list[int] = []
        self._irq_fanout: list[tuple[int, int]] = []
        self._sequence_trigger_pin: int = 0xFF
        self._sequence_trigger_edge: int = 1

    def _resolve_chain_target(self, next_dac_channel) -> int:
        if hasattr(next_dac_channel, "port"):
            value = int(next_dac_channel.port)
            if 0 <= value <= 23:
                return value
            raise ValueError("next_dac_channel.port must be 0-23 (logical channel)")

        value = int(next_dac_channel)
        if value == -1:
            return -1
        if 1 <= value <= 24:
            return value - 1
        if 0 <= value <= 23:
            return value
        raise ValueError("next_dac_channel must be -1, 0-23 (logical), or 1-24 (dac_xx numbering)")

    def _resolve_chain_targets(self, next_dac_channel) -> list[int]:
        if isinstance(next_dac_channel, Sequence) and not isinstance(next_dac_channel, (str, bytes, bytearray)):
            resolved: list[int] = []
            for item in next_dac_channel:
                ch = self._resolve_chain_target(item)
                if ch == -1:
                    continue
                if ch not in resolved:
                    resolved.append(ch)
            return resolved

        ch = self._resolve_chain_target(next_dac_channel)
        return [] if ch == -1 else [ch]

    def set_voltage(self, voltage, send_immediately=None):
        self.instr.set_voltage(int(self.port), float(voltage), send_immediately=send_immediately)
        self.voltage = voltage

    def effectuate(self):
        self.set_voltage(self.voltage)

    def start_pwm(self, tgp_id, low, high, freq, duty_cycle=50.0, send_immediately=None):
        self.instr.start_pwm(
            int(self.port),
            int(tgp_id),
            float(low),
            float(high),
            float(freq),
            float(duty_cycle),
            send_immediately=send_immediately,
        )

    def stop_pwm(self, tgp_id=None, send_immediately=None):
        self.instr.stop_pwm(int(self.port), tgp_id=tgp_id, send_immediately=send_immediately)

    def _configure_external_trigger(self, trigger_pin: int, edge: int = 1) -> None:
        self._sequence_trigger_pin = int(trigger_pin)
        self._sequence_trigger_edge = self.instr._validate_sequence_edge(int(edge))

    def unbind_external_trigger(self) -> None:
        self._sequence_trigger_pin = 0xFF
        self._sequence_trigger_edge = 1

    def trigger_sequence(
        self,
        voltages: Sequence[float],
        send_immediately: Optional[bool] = None,
        *,
        trigger_pin: Optional[int] = None,
        edge: Optional[int] = None,
    ):
        use_pin = self._sequence_trigger_pin if trigger_pin is None else int(trigger_pin)
        if use_pin == 0xFF:
            raise RuntimeError(
                "Channel is not bound to an external trigger. Call bind(...) first or pass trigger_pin explicitly."
            )
        use_edge = self._sequence_trigger_edge if edge is None else self.instr._validate_sequence_edge(int(edge))

        self.instr.send_voltage_sequence(
            int(self.port),
            use_pin,
            use_edge,
            voltages,
            send_immediately=send_immediately,
        )

    def soft_sequence(self, delay_ms, voltages, send_immediately=None):
        if self._irq_every_points is None:
            self.instr.send_soft_sequence(
                int(self.port),
                int(delay_ms),
                voltages,
                send_immediately=send_immediately,
            )
            return

        self.instr.send_chained_soft_sequence(
            int(self.port),
            int(delay_ms),
            voltages,
            irq_every_points=int(self._irq_every_points),
            next_dac_channel=int(self._irq_next_dac_channel),
            next_dac_channels=self._irq_next_dac_channels,
            next_dac_fanout=self._irq_fanout,
            start_on_internal_irq=False,
            send_immediately=send_immediately,
        )

    def set_on_trigger(
        self,
        voltages: Sequence[float],
        *,
        irq_every_points: int = 0,
        next_dac_channel: int = -1,
        next_dac_channels: Optional[Sequence[int]] = None,
        start_on_internal_irq: bool = True,
        send_immediately: Optional[bool] = None,
    ) -> None:
        resolved_chain_targets = (
            self._resolve_chain_targets(next_dac_channels)
            if next_dac_channels is not None
            else self._resolve_chain_targets(next_dac_channel)
        )
        resolved_chain_target = resolved_chain_targets[0] if resolved_chain_targets else -1
        self.instr.send_chained_soft_sequence(
            int(self.port),
            int(0),
            voltages,
            irq_every_points=int(irq_every_points),
            next_dac_channel=resolved_chain_target,
            next_dac_channels=resolved_chain_targets,
            start_on_internal_irq=bool(start_on_internal_irq),
            send_immediately=send_immediately,
        )

    def bind(self, next_dac_channel, irq_every_points: Optional[int] = None) -> None:
        resolved: list[int]
        resolved_irq = irq_every_points
        fanout: list[tuple[int, int]] = []

        if isinstance(next_dac_channel, Sequence) and not isinstance(next_dac_channel, (str, bytes, bytearray)):
            normalized_targets = []
            tuple_irqs: list[int] = []

            for item in next_dac_channel:
                if isinstance(item, tuple) and len(item) == 2:
                    raw_target, raw_irq = item
                    tuple_irqs.append(int(raw_irq))
                    raw = raw_target
                    fanout.append((self._resolve_chain_target(raw), int(raw_irq)))
                else:
                    raw = item
                normalized_targets.append(raw)

            resolved = self._resolve_chain_targets(normalized_targets)

            if tuple_irqs:
                unique_irqs = set(tuple_irqs)
                if len(unique_irqs) == 1:
                    tuple_irq = tuple_irqs[0]
                    if resolved_irq is None:
                        resolved_irq = tuple_irq
                    elif int(resolved_irq) != tuple_irq:
                        raise ValueError("irq_every_points conflicts with per-target tuple values")
                elif resolved_irq is None:
                    resolved_irq = min(unique_irqs)
        else:
            raw = next_dac_channel
            resolved = self._resolve_chain_targets(raw)

        if resolved_irq is None:
            raise ValueError("irq_every_points is required")

        if fanout:
            dedup_fanout: list[tuple[int, int]] = []
            for target, every in fanout:
                if target == -1:
                    continue
                if every <= 0:
                    raise ValueError("irq_every_points in (target, irq_every_points) tuples must be > 0")

                replaced = False
                for idx, (existing_target, _) in enumerate(dedup_fanout):
                    if existing_target == target:
                        dedup_fanout[idx] = (target, every)
                        replaced = True
                        break
                if not replaced:
                    dedup_fanout.append((target, every))
            fanout = dedup_fanout

        self._irq_next_dac_channels = resolved
        self._irq_next_dac_channel = resolved[0] if resolved else -1
        self._irq_every_points = int(resolved_irq)
        self._irq_fanout = fanout

    def unbind(self) -> None:
        self._irq_every_points = None
        self._irq_next_dac_channel = -1
        self._irq_next_dac_channels = []
        self._irq_fanout = []


class B_DAC_TrigChannel(Instrument_Port):
    def __init__(self, **kwargs):
        Instrument_Port.__init__(self, **kwargs)
        self.edge = 1

    def _resolve_target_dac(self, dac_channel):
        if isinstance(dac_channel, B_DAC_Channel):
            return dac_channel

        if hasattr(dac_channel, "port"):
            dac_channel = dac_channel.port

        value = int(dac_channel)
        if 1 <= value <= 24:
            logical_channel = value - 1
        elif 0 <= value <= 23:
            logical_channel = value
        else:
            raise ValueError("dac_channel must be 1-24 (dac_xx numbering) or 0-23 (logical channel)")

        channel_name = f"dac_{logical_channel + 1:02d}"
        target = getattr(self.instr, channel_name, None)
        if isinstance(target, B_DAC_Channel):
            return target

        for port in getattr(self.instr, "ports", []):
            if isinstance(port, B_DAC_Channel) and int(port.port) == logical_channel:
                return port

        raise ValueError("Unable to locate DAC channel " + str(dac_channel))

    def __enable_irq(self, dac_channel, edge: Optional[int] = None) -> None:
        use_edge = self.edge if edge is None else self.instr._validate_sequence_edge(int(edge))
        self.edge = use_edge
        target = self._resolve_target_dac(dac_channel)
        target._configure_external_trigger(trigger_pin=int(self.port), edge=use_edge)

    def bind(self, target, edge: Optional[int] = None):
        default_edge = self.edge if edge is None else self.instr._validate_sequence_edge(int(edge))
        self.edge = default_edge

        if isinstance(target, Sequence) and not isinstance(target, (str, bytes, bytearray)):
            for item in target:
                if isinstance(item, tuple) and len(item) == 2:
                    raw_target, raw_edge = item
                    use_edge = self.instr._validate_sequence_edge(int(raw_edge))
                else:
                    raw_target = item
                    use_edge = default_edge

                self.__enable_irq(raw_target, edge=use_edge)
            return

        self.__enable_irq(target, edge=default_edge)

    def unbind(
        self,
        target: Optional[Any] = None,
        *,
        stop_running: bool = True,
        zero_output: bool = False,
        send_immediately: Optional[bool] = None,
    ) -> None:
        channels: list[B_DAC_Channel] = []

        if target is None:
            for port in getattr(self.instr, "ports", []):
                if isinstance(port, B_DAC_Channel):
                    channels.append(port)
        elif isinstance(target, Sequence) and not isinstance(target, (str, bytes, bytearray)):
            for item in target:
                channels.append(self._resolve_target_dac(item))
        else:
            channels.append(self._resolve_target_dac(target))

        for ch in channels:
            ch.unbind_external_trigger()
            if stop_running:
                self.instr.stop_sequence_channel(
                    int(ch.port),
                    zero_output=zero_output,
                    send_immediately=False,
                )

        if stop_running:
            self.instr._send_if_needed(send_immediately)

    def set_level(self, level=float(3.3), send_immediately: Optional[bool] = None):
        self.instr.set_trigger_level(int(self.port), float(level), send_immediately=send_immediately)
