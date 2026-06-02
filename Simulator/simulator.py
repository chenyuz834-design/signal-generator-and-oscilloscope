"""
STM32G474 Signal Processor — Virtual MCU Simulator
===================================================
Simulates the STM32 firmware behaviour:
  • Listens for serial commands from the upper-computer
  • Generates waveform data (sine/square/triangle/sawtooth)
  • Sends ADC sample frames back via serial

Usage:
  python simulator.py --port COM5          (connect to a real/virtual COM port)
  python simulator.py --port COM5 --baud 2000000

Requires: pyserial   (pip install pyserial)
For virtual port pairs on Windows, use com0com or similar tools.
"""

import argparse
import math
import struct
import threading
import time
import sys

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Install it with: pip install pyserial")
    sys.exit(1)

# ═══════════════ Protocol Constants ═══════════════
HEADER = bytes([0xAA, 0x55])
CMD_SET_WAVEFORM   = 0x01
CMD_SET_FREQUENCY  = 0x02
CMD_SET_AMPLITUDE  = 0x03
CMD_SIG_ONOFF      = 0x04
CMD_SET_SAMPLERATE = 0x10
CMD_OSC_ONOFF      = 0x11
CMD_WAVE_DATA      = 0x80
CMD_PARAM_ACK      = 0x81

VREF_MV  = 3300
DAC_MAX  = 4095
LUT_SIZE = 256


# ═══════════════ Frame Builder / Parser ═══════════════
def build_frame(cmd: int, data: bytes = b'') -> bytes:
    length = len(data)
    frame = bytearray([0xAA, 0x55, cmd, (length >> 8) & 0xFF, length & 0xFF])
    frame += data
    xor = 0
    for b in frame[2:]:
        xor ^= b
    frame.append(xor)
    return bytes(frame)


def build_wave_frame(samples: list[int]) -> bytes:
    count = len(samples)
    data = struct.pack('>H', count)
    for s in samples:
        data += struct.pack('>H', s)
    return build_frame(CMD_WAVE_DATA, data)


def build_ack(orig_cmd: int, status: int) -> bytes:
    return build_frame(CMD_PARAM_ACK, bytes([orig_cmd, status]))


class FrameParser:
    """Byte-by-byte state-machine frame parser."""

    def __init__(self):
        self.state = 0
        self.cmd = 0
        self.length = 0
        self.data = bytearray()
        self.idx = 0
        self.xor = 0
        self.frames = []

    def feed(self, buf: bytes):
        for b in buf:
            if self.state == 0:
                if b == 0xAA:
                    self.state = 1
            elif self.state == 1:
                self.state = 2 if b == 0x55 else 0
            elif self.state == 2:
                self.cmd = b
                self.xor = b
                self.state = 3
            elif self.state == 3:
                self.length = b << 8
                self.xor ^= b
                self.state = 4
            elif self.state == 4:
                self.length |= b
                self.xor ^= b
                self.data = bytearray()
                self.idx = 0
                self.state = 6 if self.length == 0 else 5
            elif self.state == 5:
                self.data.append(b)
                self.xor ^= b
                self.idx += 1
                if self.idx >= self.length:
                    self.state = 6
            elif self.state == 6:
                if b == self.xor:
                    self.frames.append((self.cmd, bytes(self.data)))
                self.state = 0

    def get_frames(self):
        result = self.frames
        self.frames = []
        return result


# ═══════════════ Signal Generator (Virtual DAC) ═══════════════
class VirtualSignalGen:
    def __init__(self):
        self.wave_type = 0   # 0=sine, 1=square, 2=tri, 3=saw
        self.frequency = 1000
        self.amplitude_mv = 3300
        self.running = False
        self.lut = [0] * LUT_SIZE
        self._rebuild_lut()

    def _rebuild_lut(self):
        scale = self.amplitude_mv / VREF_MV
        half = DAC_MAX / 2
        for i in range(LUT_SIZE):
            t = i / LUT_SIZE
            if self.wave_type == 0:
                val = math.sin(2 * math.pi * t)
            elif self.wave_type == 1:
                val = 1.0 if t < 0.5 else -1.0
            elif self.wave_type == 2:
                if t < 0.25:
                    val = t * 4
                elif t < 0.75:
                    val = 2 - t * 4
                else:
                    val = t * 4 - 4
            elif self.wave_type == 3:
                val = 2 * t - 1
            else:
                val = 0
            dac_val = half + val * half * scale
            dac_val = max(0, min(DAC_MAX, dac_val))
            self.lut[i] = int(dac_val)

    def set_waveform(self, wtype):
        self.wave_type = wtype
        self._rebuild_lut()

    def set_frequency(self, freq):
        self.frequency = max(1, min(200000, freq))

    def set_amplitude(self, mv):
        self.amplitude_mv = max(0, min(VREF_MV, mv))
        self._rebuild_lut()

    def sample(self, sample_rate: int, count: int) -> list[int]:
        """Generate `count` ADC samples as if DAC output is looped back to ADC."""
        if not self.running:
            return [2048] * count  # DC midpoint when stopped

        samples = []
        for i in range(count):
            t = i / sample_rate
            phase = (t * self.frequency) % 1.0
            lut_idx = int(phase * LUT_SIZE) % LUT_SIZE
            samples.append(self.lut[lut_idx])
        return samples


# ═══════════════ Main Simulator ═══════════════
class Simulator:
    def __init__(self, port_name: str, baud: int):
        self.ser = serial.Serial(port_name, baud, timeout=0.05)
        self.parser = FrameParser()
        self.siggen = VirtualSignalGen()
        self.sample_rate = 100000
        self.osc_running = False
        self.running = True
        print(f"[SIM] Connected to {port_name} @ {baud} bps")
        print(f"[SIM] Waiting for commands from upper-computer...")

    def run(self):
        last_sample_time = time.time()
        sample_interval = 1024 / self.sample_rate  # seconds per frame

        try:
            while self.running:
                # Read incoming data
                data = self.ser.read(256)
                if data:
                    self.parser.feed(data)
                    for cmd, payload in self.parser.get_frames():
                        self._handle_command(cmd, payload)

                # Generate and send oscilloscope data
                if self.osc_running:
                    now = time.time()
                    if now - last_sample_time >= sample_interval:
                        samples = self.siggen.sample(self.sample_rate, 1024)
                        frame = build_wave_frame(samples)
                        self.ser.write(frame)
                        last_sample_time = now

                if not self.osc_running:
                    time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n[SIM] Stopped by user.")
        except serial.SerialException as e:
            print(f"[SIM] Serial error: {e}")
        finally:
            self.ser.close()
            print("[SIM] Port closed.")

    def _handle_command(self, cmd, data):
        if cmd == CMD_SET_WAVEFORM and len(data) >= 1:
            names = ['Sine', 'Square', 'Triangle', 'Sawtooth']
            self.siggen.set_waveform(data[0])
            print(f"  [CMD] Waveform = {names[data[0]] if data[0] < 4 else '?'}")
            self.ser.write(build_ack(cmd, 0))

        elif cmd == CMD_SET_FREQUENCY and len(data) >= 4:
            freq = struct.unpack('>I', data[:4])[0]
            self.siggen.set_frequency(freq)
            print(f"  [CMD] Frequency = {freq} Hz")
            self.ser.write(build_ack(cmd, 0))

        elif cmd == CMD_SET_AMPLITUDE and len(data) >= 2:
            amp = struct.unpack('>H', data[:2])[0]
            self.siggen.set_amplitude(amp)
            print(f"  [CMD] Amplitude = {amp} mV")
            self.ser.write(build_ack(cmd, 0))

        elif cmd == CMD_SIG_ONOFF and len(data) >= 1:
            self.siggen.running = bool(data[0])
            print(f"  [CMD] Signal Gen = {'ON' if data[0] else 'OFF'}")
            self.ser.write(build_ack(cmd, 0))

        elif cmd == CMD_SET_SAMPLERATE and len(data) >= 4:
            sps = struct.unpack('>I', data[:4])[0]
            self.sample_rate = max(100, min(1000000, sps))
            print(f"  [CMD] Sample Rate = {self.sample_rate} SPS")
            self.ser.write(build_ack(cmd, 0))

        elif cmd == CMD_OSC_ONOFF and len(data) >= 1:
            self.osc_running = bool(data[0])
            print(f"  [CMD] Oscilloscope = {'ON' if data[0] else 'OFF'}")
            self.ser.write(build_ack(cmd, 0))

        else:
            print(f"  [CMD] Unknown cmd=0x{cmd:02X} len={len(data)}")
            self.ser.write(build_ack(cmd, 1))


# ═══════════════ Entry Point ═══════════════
if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='STM32G474 Virtual MCU Simulator')
    ap.add_argument('--port', '-p', required=True, help='Serial port (e.g. COM5)')
    ap.add_argument('--baud', '-b', type=int, default=2000000, help='Baud rate (default 2000000)')
    args = ap.parse_args()

    sim = Simulator(args.port, args.baud)
    sim.run()
