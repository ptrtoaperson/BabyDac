from __future__ import annotations

import socket
import struct
import time
import errno
from contextlib import contextmanager
from typing import Iterable, Mapping, Optional, Sequence, Tuple, Union

from .config import BoardData, InstrumentData
from .pins import PinObject


PortData = BoardData


class SocketInstrument:
    """Lightweight transport helper supporting socket and optional serial backends."""

    def __init__(self, **kwargs):
        self.connect = bool(kwargs.get("connect", True))
        self.raise_on_connect_error = bool(kwargs.get("raise_on_connect_error", False))
        self.ip_address = kwargs.get("ip_address")
        self.port = kwargs.get("port")
        self.serial_port = kwargs.get("serial_port")
        self.transport = kwargs.get("transport")
        self.baud_rate = int(kwargs.get("baud_rate", 115200))
        self.instr = None
        self.connected = False

        if not self.transport:
            if self.ip_address and self.port:
                self.transport = "socket"
            elif self.serial_port:
                self.transport = "serial"

        if self.connect:
            try:
                retry_connect = getattr(self, "_reconnect_with_retries", None)
                if callable(retry_connect):
                    connected = bool(retry_connect())
                    if not connected:
                        raise OSError(getattr(self, "_last_reconnect_error", "connect failed"))
                else:
                    self.reconnect()
            except Exception as exc:
                self.connected = False
                self.instr = None
                if self.raise_on_connect_error:
                    raise
                print("Initial connection failed: " + str(exc))

    def reconnect(self) -> None:
        self.close()
        if self.transport == "socket":
            self.instr = socket.create_connection((str(self.ip_address), int(self.port)), timeout=2.0)
            self.instr.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.instr.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

            # Make dead-link detection faster on platforms that expose these options.
            if hasattr(self, "tcp_user_timeout_ms") and hasattr(socket, "TCP_USER_TIMEOUT"):
                try:
                    self.instr.setsockopt(socket.IPPROTO_TCP, socket.TCP_USER_TIMEOUT, int(self.tcp_user_timeout_ms))
                except OSError:
                    pass

            if hasattr(socket, "TCP_KEEPIDLE"):
                try:
                    self.instr.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 2)
                    self.instr.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 1)
                    self.instr.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 2)
                except OSError:
                    pass

            self.instr.settimeout(None)
            self.connected = True
            return

        if self.transport == "serial":
            try:
                import serial
            except ImportError as exc:
                raise ImportError("serial transport requires pyserial") from exc

            self.instr = serial.Serial(self.serial_port, baudrate=self.baud_rate, timeout=1)
            self.connected = bool(getattr(self.instr, "is_open", False))

    def close(self) -> None:
        if self.instr is None:
            self.connected = False
            return
        try:
            self.instr.close()
        finally:
            self.instr = None
            self.connected = False

    def _discard_connection_handle(self) -> None:
        """Best-effort close and clear for a broken or stale transport handle."""
        if self.instr is None:
            self.connected = False
            return
        try:
            close_fn = getattr(self.instr, "close", None)
            if callable(close_fn):
                close_fn()
        except Exception:
            pass
        finally:
            self.instr = None
            self.connected = False

    def is_connected(self, probe: bool = False) -> bool:
        if self.instr is None:
            self.connected = False
            return False

        # Fast path: trust existing open handle unless caller asks for an active probe.
        if self.transport == "serial":
            status = bool(getattr(self.instr, "is_open", False))
            self.connected = status
            return status

        if self.transport == "socket":
            try:
                fileno = self.instr.fileno()
                if fileno < 0:
                    self._discard_connection_handle()
                    return False

                if probe:
                    self.instr.getpeername()

                self.connected = True
                return True
            except OSError:
                self._discard_connection_handle()
                return False

        self.connected = self.instr is not None
        return self.connected

    def connection_status(self, probe: bool = True) -> str:
        if self.is_connected(probe=probe):
            return "connected"
        return "disconnected"

    def _write_raw(self, payload: bytes) -> None:
        if self.instr is None:
            raise OSError("No active instrument transport")

        if hasattr(self.instr, "sendall"):
            self.instr.sendall(payload)
        else:
            self.instr.write(payload)


# Backward compatibility alias for existing imports/usages.
PyVisaInstrument = SocketInstrument


class PortInstrument(PinObject):
    def __init__(self,**kwargs):
        self.connected = False
        self.portData = PortData.get(self.__class__.__name__,{}) | kwargs.get('portData',{})
        PinObject.__init__(self,PinData=self.portData,**kwargs)
        self.ports = self.pins
        for port in self.ports:
            port.instr = self
            if hasattr(port, "name"):
                setattr(self, port.name, port)
            if getattr(self,'instr_settings',{}).get('port_settings'):
                port.port_settings = self.instr_settings.get('port_settings',{}).get(port.name,{})
            if hasattr(port.instr,'scpi'):
                if port.instr.scpi:
                    port.scpi = port.instr.scpi.get(port.__class__.__name__,{})
                    
    def dress_ports(self,_pin):
        names = self.generic_instr
        if isinstance(names, str):
            names = [names]
        for instr_name in names:
            setattr(_pin,instr_name,self)
            _pin.instruments[instr_name] = self

class B_DAC(PortInstrument, SocketInstrument):
    """Single-file consolidated B_DAC implementation."""

    ChannelSequenceAssignments = Union[
        Mapping[int, Sequence[float]],
        Iterable[Tuple[int, Sequence[float]]],
    ]

    def __init__(self, **kwargs):
        # Backward-compatible alias used in some scripts/REPL sessions.
        if "serial" in kwargs and "serial_port" not in kwargs:
            kwargs = kwargs | {"serial_port": kwargs.get("serial")}

        # Default to socket transport only when ip/port are actually given;
        # otherwise let serial_port (if provided) select serial transport.
        if "transport" not in kwargs and kwargs.get("ip_address") and kwargs.get("port"):
            kwargs = kwargs | {"transport": "socket"}

        self.transport_backend = kwargs.get("transport") or ("serial" if kwargs.get("serial_port") else "socket")
        # Backward compatibility for older scripts that read this attribute.
        self.visa_backend = self.transport_backend
        self.auto_send_commands = bool(kwargs.get("auto_send_commands", True))
        self.expect_ack = bool(kwargs.get("expect_ack", False))
        self.post_send_delay_ms = float(kwargs.get("post_send_delay_ms", 5.0))
        self.reply_timeout = float(kwargs.get("reply_timeout", 2.0))
        self.tcp_user_timeout_ms = int(kwargs.get("tcp_user_timeout_ms", 300))
        self.verify_connection_before_send = bool(kwargs.get("verify_connection_before_send", True))
        self.auto_reconnect_on_send = bool(kwargs.get("auto_reconnect_on_send", True))
        self.reconnect_attempts = int(kwargs.get("reconnect_attempts", 20))
        self.reconnect_backoff_ms = float(kwargs.get("reconnect_backoff_ms", 300.0))
        self.reconnect_backoff_factor = float(kwargs.get("reconnect_backoff_factor", 1.35))
        self.reconnect_max_backoff_ms = float(kwargs.get("reconnect_max_backoff_ms", 2000.0))
        self.startup_grace_ms = float(kwargs.get("startup_grace_ms", 2500.0))
        self.startup_error_grace_once = bool(kwargs.get("startup_error_grace_once", True))
        self.send_retries_on_connection_error = int(kwargs.get("send_retries_on_connection_error", 3))
        self.send_retry_backoff_ms = float(kwargs.get("send_retry_backoff_ms", 200.0))
        self._last_reconnect_error = ""
        self._batch_depth = 0
        self.instr = None

        instr_data = InstrumentData.get(self.__class__.__name__, {})
        self.MAX_PACKET_BYTES = int(
            instr_data.get(
                "MAX_PACKET_BYTES",
                instr_data.get(" MAX_PACKET_BYTES", 2 * 1024),
            )
        )

        for parentClass in (PortInstrument, SocketInstrument):
            parentClass.__init__(self, **(kwargs | instr_data))

        if (
            getattr(self, "transport", "") == "serial"
            and hasattr(self, "baud_rate")
            and self.instr is not None
        ):
            self.instr.baud_rate = self.baud_rate

        # Keep socket operations bounded when ACK mode is enabled.
        if self.instr is not None and hasattr(self.instr, "settimeout"):
            self.instr.settimeout(self.reply_timeout)

        # Instance-scoped command buffer.
        self._buffer = bytearray()

    def reconnect(self) -> None:
        super().reconnect()
        if self.instr is not None and hasattr(self.instr, "settimeout"):
            self.instr.settimeout(self.reply_timeout)

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

    def _ensure_connection_for_send(self) -> bool:
        if self.instr is None:
            if self.auto_reconnect_on_send:
                return self._reconnect_with_retries()
            return False

        if not self.verify_connection_before_send:
            return True

        if self.is_connected(probe=True):
            return True

        if self.auto_reconnect_on_send:
            return self._reconnect_with_retries()

        return False

    def _reconnect_with_retries(self) -> bool:
        attempts = max(1, int(self.reconnect_attempts))
        base_delay_s = max(0.0, float(self.reconnect_backoff_ms) / 1000.0)
        backoff_factor = max(1.0, float(self.reconnect_backoff_factor))
        max_delay_s = max(base_delay_s, float(self.reconnect_max_backoff_ms) / 1000.0)
        self._last_reconnect_error = ""
        startup_grace_used = False

        for attempt in range(attempts):
            try:
                self.reconnect()
                if self.is_connected(probe=True):
                    return True
            except Exception as exc:
                # Keep retrying until attempts are exhausted.
                self._last_reconnect_error = str(exc)

                # W5500/TCP tasks can need extra warm-up after link flap.
                if (
                    self.startup_error_grace_once
                    and not startup_grace_used
                    and self._is_startup_connect_error(exc)
                ):
                    grace_s = max(0.0, float(self.startup_grace_ms) / 1000.0)
                    if grace_s > 0:
                        time.sleep(grace_s)
                    startup_grace_used = True

            # No need to sleep after the final attempt.
            if attempt + 1 < attempts and base_delay_s > 0:
                delay_s = min(max_delay_s, base_delay_s * (backoff_factor ** attempt))
                time.sleep(delay_s)

        return False

    def _is_startup_connect_error(self, exc: Exception) -> bool:
        if isinstance(exc, ConnectionRefusedError):
            return True

        if isinstance(exc, TimeoutError):
            return True

        if isinstance(exc, OSError):
            err_no = getattr(exc, "errno", None)
            if err_no in (errno.ECONNREFUSED, errno.ETIMEDOUT, errno.EHOSTUNREACH, errno.ENETUNREACH):
                return True

        message = str(exc).lower()
        if "connection refused" in message or "timed out" in message:
            return True

        return False

    def _read_ack_if_needed(self) -> bytes:
        if not self.expect_ack:
            if self.post_send_delay_ms > 0:
                time.sleep(self.post_send_delay_ms / 1000.0)
            return b""

        try:
            if hasattr(self.instr, "read_bytes"):
                return self.instr.read_bytes(2)
            if hasattr(self.instr, "recv"):
                return self.instr.recv(2)
            if hasattr(self.instr, "read"):
                return self.instr.read(2)
            return b""
        except socket.timeout:
            print("No ACK received from B_DAC within " + str(self.reply_timeout) + "s")
            return b""
        except Exception:
            return b""

    def _send_payload_once(self, payload: bytes) -> bytes:
        self._write_raw(payload)
        return self._read_ack_if_needed()

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
        total_send_attempts = max(1, int(self.send_retries_on_connection_error) + 1)
        send_backoff_s = max(0.0, float(self.send_retry_backoff_ms) / 1000.0)
        last_send_error = ""

        try:
            for attempt in range(total_send_attempts):
                if not self._ensure_connection_for_send():
                    if attempt + 1 < total_send_attempts and send_backoff_s > 0:
                        time.sleep(send_backoff_s)
                    continue

                try:
                    response = self._send_payload_once(payload)
                    return response
                except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                    last_send_error = str(exc)
                    self._discard_connection_handle()

                if attempt + 1 < total_send_attempts and send_backoff_s > 0:
                    time.sleep(send_backoff_s)

            if self._last_reconnect_error:
                print(
                    "B_DAC is not connected ("
                    + self._last_reconnect_error
                    + "). Set serial_port (or serial), or ip_address/port, and reconnect before sending commands."
                )
            elif last_send_error:
                print("Connection error: " + last_send_error)
            else:
                print("B_DAC is not connected. Set serial_port (or serial), or ip_address/port, and reconnect before sending commands.")
            return b""
        finally:
            self._buffer.clear()

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


