
from __future__ import annotations

from typing import Any, Optional, Sequence

from .config import InstrumentData


config_data = InstrumentData


class JsonObject:
    def __init__(self, **kwargs):
        kwargs = config_data.get(self.__class__.__name__, {}) | kwargs
        for attr_name, attr_value in kwargs.items():
            setattr(self, attr_name, attr_value)


class Pin:
    def __init__(self, **kwargs):
        self.pins = kwargs.get("pins", [self])


class Instrument_Port(Pin, JsonObject):
    def __init__(self, **kwargs):
        JsonObject.__init__(self, **kwargs)
        Pin.__init__(self, **kwargs)
        if not hasattr(self, "instruments"):
            self.instruments = {}


class PinObject(JsonObject, Pin):
    def __init__(self, PinData=None, **kwargs):
        JsonObject.__init__(self, **kwargs)
        self.instruments = {}
        pins = []

        pin_data = PinData or {}
        for group in pin_data.values():
            group_type = group.get("type")
            for pin_name, pin_info in group.get("pins", {}).items():
                pin_type = pin_info.get("type", group_type)
                pin_class = globals().get(pin_type)
                if pin_class is None:
                    raise ValueError("Unknown pin class: " + str(pin_type))

                payload = dict(pin_info)
                payload.setdefault("name", pin_name)
                pin_obj = pin_class(**payload)
                setattr(self, pin_name, pin_obj)
                pins.append(pin_obj)

        self.pins = pins



######################################################################

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

    def set_voltage(self, voltage=None, send_immediately=None):
        if voltage is None:
            if hasattr(self, "voltage"):
                voltage = self.voltage
            else:
                raise TypeError("set_voltage(voltage): missing required argument 'voltage' in volts")

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


############################################       
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


 ####################################