# STM32F103 TCP DAC Controller

A minimal STM32F103-based Ethernet TCP controller for I2C DAC (MCP4728) voltage control. Uses ENC28J60 Ethernet controller with uIP TCP/IP stack for remote DAC voltage setting via network commands.

## Communication Interfaces

The STM32F103 DAC controller supports two communication methods:

### 1. Ethernet TCP (Primary)
- **Protocol**: TCP/IP over Ethernet
- **Port**: 1234
- **IP Address**: 192.168.0.30
- **Hardware**: ENC28J60 Ethernet controller
- **Use Case**: Networked operation, remote control

### 2. Serial UART (Alternative)
- **Protocol**: Serial ASCII commands
- **Baud Rate**: 115200
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Hardware**: STM32 UART (TX/RX pins)
- **Use Case**: Direct connection, development, testing

## Serial Communication

If you prefer serial communication instead of Ethernet, you can modify the STM32 firmware to use UART instead of ENC28J60. The command format remains the same.

### Serial Pin Connections
| STM32F103 Pin | Function | Typical Connection |
|---------------|----------|-------------------|
| PA9 (TX)      | UART TX  | Connect to RX of host |
| PA10 (RX)     | UART RX  | Connect to TX of host |
| GND           | Ground   | Common ground |

### Serial Command Format
Same as Ethernet:
```
set voltage(CHANNEL,MILLIVOLTS)\r
```

Example session:
```
Host: set voltage(A,2500)\r
Device: OK\n
```

## Hardware Connections

### ENC28J60 Ethernet Controller
| ENC28J60 Pin | STM32F103 Pin | Function |
|-------------|---------------|----------|
| INT         | PA3           | Interrupt (active low) |
| CS          | PA4           | Chip Select |
| SCK         | PB3           | SPI Clock |
| SI (MOSI)   | PB5           | SPI Master Out Slave In |
| SO (MISO)   | PB4           | SPI Master In Slave Out |
| RST         | 3.3V          | Reset (active low, tie high) |
| LED         | PA5           | Link/Activity LED |

### I2C DAC (MCP4728)
| MCP4728 Pin | STM32F103 Pin | Function |
|-------------|---------------|----------|
| SCL         | PB6           | I2C Clock |
| SDA         | PB7           | I2C Data |
| VDD         | 3.3V          | Power |
| GND         | GND           | Ground |
| A0-A2       | GND           | I2C Address (0x60) |

### Additional Pins
- **PC13**: Status LED (blinks during operation)
- **PA15**: SPI NSS (not used, configured as GPIO)

## Network Configuration

### IP Address Settings
- **Device IP**: `192.168.0.30`
- **Subnet Mask**: `255.255.255.0`
- **Default Gateway**: `192.168.0.1` (assumed)
- **MAC Address**: `08:62:66:D7:3B:AF`

### TCP Port
- **Command Port**: `1234` (TCP)
- **Protocol**: Simple text-based commands

## DAC Commands

Commands are sent via TCP to port 1234. The device responds with "OK" for success or "ERR" for errors.

### Command Format
```
set voltage(CHANNEL, MILLIVOLTS)
```

### Parameters
- **CHANNEL**: 'A' or 'B' (case sensitive)
- **MILLIVOLTS**: Integer value 0-4095 (0-10V range)

### Examples
```bash
# Set channel A to 2.5V (2500mV)
echo "set voltage(A,2500)" | nc 192.168.0.30 1234

# Set channel B to 1.0V (1000mV)
echo "set voltage(B,1000)" | nc 192.168.0.30 1234

# Set channel A to 0V
echo "set voltage(A,0)" | nc 192.168.0.30 1234

# Set channel B to maximum (10V)
echo "set voltage(B,4095)" | nc 192.168.0.30 1234
```

### Command Parser Location
- **File**: `src/tcp_command_app.c`
- **Function**: `parse_command(const char *cmd)`
- **Response**: Sent back via TCP connection

## Software Architecture

### Core Files
- `src/main.c` - Main application, interrupt setup, uIP integration
- `src/tcp_command_app.c` - TCP command parsing and DAC control
- `src/dac.c` - I2C DAC voltage setting functions
- `src/I2C.c` - Direct register I2C communication
- `drivers/enc28j60.c` - ENC28J60 Ethernet driver
- `drivers/ENC_Ethernet_stm32.c` - STM32-specific ENC28J60 interface
- `dac_client.py` - Python TCP client for device communication
- `dac_client_serial.py` - Python serial client for device communication

### Interrupt System
- **EXTI3**: Handles ENC28J60 interrupts (PA3)
- **Interrupts Enabled**: Packet receive, link status, transmit complete, errors
- **ISR**: `EXTI3_IRQHandler()` in `src/main.c`

### Network Stack
- **uIP**: Lightweight TCP/IP implementation
- **ARP**: Address Resolution Protocol support
- **TCP**: Reliable command transport
- **Buffer Size**: Configured for standard Ethernet frames

## Build Instructions

### Prerequisites
- `arm-none-eabi-gcc` toolchain
- `make`
- VS Code with Cortex-Debug extension (recommended)
- **Python 3.6+** (for client script)

### Python Client Dependencies
The `dac_client.py` script requires only Python standard library (no external dependencies).

### Build Commands
```bash
# Build firmware
make

# Clean build
make clean

# Build and clean
make clean && make
```

### Output Files
- `bin/firmware.bin` - Binary for flashing
- `bin/firmware.hex` - Intel HEX format
- `bin/firmware.elf` - Debuggable ELF file

## Flashing Instructions

### Using VS Code Tasks
1. Connect ST-Link to STM32F103
2. Run task `flash` (Ctrl+Shift+F)
3. Firmware loads to address `0x8000000`

### Using Command Line
```bash
# Using st-flash (if installed)
st-flash --reset write bin/firmware.bin 0x8000000

# Using OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program bin/firmware.elf verify reset exit"
```

## Testing and Usage

### Network Connection
1. Connect ENC28J60 to Ethernet network
2. Power on STM32F103
3. Device should be reachable at `192.168.0.30`

### Testing Commands

#### Using Netcat
```bash
# Test connectivity
ping 192.168.0.30

# Send DAC command
echo "set voltage(A,2500)" | nc 192.168.0.30 1234

# Expected response: "OK"
```

#### Using Python Client
A Python client script is provided for easy communication:

```bash
# Set channel A to 2.5V
python dac_client.py A 2500

# Set channel B to 1.0V
python dac_client.py B 1000

# Voltage sweep from 0V to 5V on channel A
python dac_client.py --sweep --start 0 --end 5 A 0

# Custom IP address
python dac_client.py --host 192.168.1.100 A 3000
```

### Python Client Features
- **Dual interfaces**: TCP and Serial clients
- **Auto port detection** for serial communication
- **Voltage sweep mode** for testing DAC range
- **Error handling** for connection issues
- **Response verification** (OK/ERR status)
- **Flexible configuration** (IP, port, baud rate, serial ports)

### Python Client Usage
```bash
# Basic usage
python dac_client.py CHANNEL MILLIVOLTS

# Examples
python dac_client.py A 2500    # 2.5V on channel A
python dac_client.py B 1000    # 1.0V on channel B
python dac_client.py A 0       # 0V on channel A
python dac_client.py B 4095    # 10V on channel B

# Sweep mode
python dac_client.py --sweep --start 0 --end 10 --step 0.5 A 0

# Custom connection
python dac_client.py --host 192.168.1.50 --port 8080 A 2000
```

### Serial Client Usage
```bash
# List available ports
python3 dac_client_serial.py --list-ports

# Basic usage
python3 dac_client_serial.py CHANNEL MILLIVOLTS

# Examples
python3 dac_client_serial.py A 2500    # 2.5V on channel A
python3 dac_client_serial.py B 1000    # 1.0V on channel B

# Query device
python3 dac_client_serial.py --query

# Sweep mode
python3 dac_client_serial.py --sweep --start 0 --end 5 --step 0.5 A 0

# Custom port
python3 dac_client_serial.py --port /dev/ttyUSB0 A 2000
```

### Debug Information
- Run task `info` to get STM32 device details
- Use `debug` task (F5) for GDB debugging
- Check link LED (PA5) for Ethernet activity

## Configuration Options

### Changing IP Address
Edit `src/main.c` lines 76-78:
```c
uip_ipaddr(ipaddr, 192,168,0,30);  // Change IP here
uip_sethostaddr(ipaddr);
```

### Changing TCP Port
Edit `src/main.c` line 83:
```c
uip_listen(HTONS(1234));  // Change port here
```

### Changing MAC Address
Edit `drivers/ENC_Ethernet_stm32.c` line 6:
```c
uint8_t MAC[6] = {0x08,0x62,0x66,0xD7,0x3B,0xAF};  // Change MAC here
```

## Troubleshooting

### No Network Response
- Check Ethernet cable connection
- Verify IP address doesn't conflict
- Check link LED activity
- Use Wireshark to monitor network traffic

### DAC Not Working
- Verify I2C connections (PB6/PB7)
- Check MCP4728 power and address pins
- Use oscilloscope to verify I2C signals

### Build Errors
- Ensure `arm-none-eabi-gcc` is in PATH
- Check for missing dependencies
- Verify all source files are present

## Performance Characteristics

- **TCP Latency**: < 1ms (interrupt-driven)
- **DAC Settling**: < 10μs
- **Network Throughput**: Sufficient for command interface
- **Power Consumption**: ~50mA active, lower in idle
- **Memory Usage**: ~11KB flash, ~3KB RAM

## Future Enhancements

- Multiple client connections
- UDP support for broadcast commands
- Voltage monitoring/feedback
- Configuration via network
- Web interface for control

