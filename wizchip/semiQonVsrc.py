
import struct


total_buffer = b''  # Start with empty bytes


filename = 'command.bin'
def code(volt):
    # Clamp input to safety limits
    if volt > 10.0: volt = 9.999999
    if volt < -10.0: volt =-9.999999
    
    # Offset binary formula: ((Volt - MinVolt) / TotalSpan) * 65536
    # CRITICAL: Clamp to 0xFFFF, do NOT mask (masking wraps 65536 to 0!)
    code_val = int(((volt + 10.0) / 20.0) * 65535)
    if code_val > 0xFFFF:
        code_val = 0xFFFF
    return code_val

def Vsrc(command):
    for cmd_tuple in command:
        global total_buffer
        # '<' = little-endian (STM32 default)
        # B = uint8, H = uint16, 
        ############################### (INST, DAC, VOLTAGE) ###############################
        packed = struct.pack('<B B H',1,*cmd_tuple)
        total_buffer += packed  # append to total buffer

def start_PWM(channel, id, low, high, freq):
        global total_buffer
       # Use 'x' to add one pad byte after the first 3 bytes
        # Now: B B B + x (1 byte) + H H + f = 12 bytes
        packed = struct.pack('<BBBxHHf', 2, channel, id, code(low), code(high), freq)
        total_buffer += packed # append to total buffer

def stop_PWM(channel, id):
    global total_buffer
    Vsrc([(channel,code(0))])
    packed = struct.pack('<B B B', 3, channel, id)
    
    total_buffer += packed

def send_voltage_sequence(dac_channel, trigger_pin, edge, voltages):

    global total_buffer

    num_points = len(voltages)

    # MATCH STM32 EXPECTATION:
    # [ID][PIN][DAC][EDGE][COUNT]
    header = struct.pack(
    '<BBBBH',
    4,
    dac_channel,
    trigger_pin,
    edge,
    num_points
)

    payload = b''

    for v in voltages:
        payload += struct.pack('<H', code(v))

    total_buffer += header + payload

def send_soft_sequence(dac_channel, delay_ms, voltages):
    """
    dac_channel: 0-7
    delay_ms: Time to wait between each voltage update (in milliseconds)
    voltages: List of floats [0.0 to 5.0]
    """
    global total_buffer
    
    num_points = len(voltages)
    
    # Header Format (6 bytes): 
    # B = uint8 (ID=5)
    # B = uint8 (DAC Channel)
    # H = uint16 (Delay in MS)
    # H = uint16 (Number of points)
    # < = Little Endian
    header = struct.pack('<BBHH', 5, dac_channel, delay_ms, num_points)
    
    # Payload: Convert voltages to 16-bit codes
    payload = b''
    for v in voltages:
        payload += struct.pack('<H', code(v))
        
    # Add to the global buffer
    total_buffer += header + payload


def send():
    global total_buffer
    endchar = 'e'
    total_buffer += endchar.encode('ascii')  # add end char at the end
    
    with open(filename, 'wb') as f:
        f.write(total_buffer)
    
    s.sendall(total_buffer)
    print(total_buffer)
    total_buffer = b'' #flush the buffer
    



###execution


import socket
import time
import numpy as np 
# STM32 IP and port
STM32_IP = "192.168.1.50"
STM32_PORT = 5000

connected = False

# Initialize the socket before the loop starts
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(1.0) # 1 second is enough for local network pings

while not connected:
    try:
        print(f"Attempting to connect to {STM32_IP}...")
        s.connect((STM32_IP, STM32_PORT))
        
        # Re-enable Keepalives once connected
        s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        
        # If on Linux/Windows, fine-tune the keepalive frequency
        if hasattr(socket, "TCP_KEEPIDLE"): # Linux
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 30)
        elif hasattr(socket, "SIO_KEEPALIVE_VALS"): # Windows
            s.ioctl(socket.SIO_KEEPALIVE_VALS, (1, 30000, 5000))
            
        connected = True
        print("Successfully connected!")
        
        # Once connected, you usually want to remove the timeout
        # so it doesn't crash during long data transfers
        s.settimeout(None) 
        
    except (socket.timeout, ConnectionRefusedError, OSError) as e:
        print(f"Connection failed: {e}. Retrying in 1 seconds...")
        #time.sleep(1)
        
     
        s.close() # Close the failed one properly
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.04)



#volt_sample = np.arange(-9.69,10,0.1)
#
#V_set = [ (0, code(-5.0)),(1, code(2.0)),  (2, code(2.5)), (3,code(1.5)),(4,code(1.5)), (5,code(1.5)),(6,code(3)), (7, code(2.0))]
V_set = [(1,code(-3.0)), (6, code(-10))]
#print(V_set)
#
Vsrc(V_set)



#start_PWM(channel=0, id=2, low=0, high=3.3, freq=0.5)
#start_PWM(channel=6, id=0, low=-1, high=(5), freq=1)


#stop_PWM(0,2)
#send()

#voltage_2 = np.linspace(0,4.5,20)
#voltages = np.linspace(-5,10,5)
#send_voltage_sequence(dac_channel=0, trigger_pin=3, edge=1, voltages=voltages)
#send_voltage_sequence(dac_channel=6, trigger_pin=2, edge=0, voltages=voltages)
#print(voltages)
#volt_ramp = [ -10, -5.0, 1.0, 2.0, 3.0, 4.0, 5.0,7,10]
#send_soft_sequence(dac_channel=0, delay_ms=1000, voltages=volt_ramp)

#send_soft_sequence(dac_channel=6, delay_ms=1000, voltages=voltages)
send()

s.close()


#for volt_x in volt_sample:
#    V_set = [(0,volt_x,0),(1,volt_x,0),
#                           (2,volt_x,0),(3,volt_x,0),
#                           (4,volt_x,0),(5,volt_x,0),
#                           (6,volt_x,0),(7,volt_x,0),]
#    Vsrc(V_set)
#    print(V_set)

#    time.sleep(0.1) #100ms delay between
#    send()





