"""Minimal polled serial port for the reference platform."""
from collections import deque
from .isa import Fault

class SerialConsole:
    BASE = 0xFFFF0000
    SIZE = 8
    DATA = 0
    STATUS = 4
    RX_READY = 1
    TX_READY = 2
    RX_EOF = 4

    def __init__(self, input_bytes=b'', on_output=None, keyboard=None):
        self.input = deque(input_bytes)
        self.output = bytearray()
        self.on_output = on_output
        self.keyboard = keyboard
        self.eof = False

    def feed(self, data):
        self.input.extend(data)

    def check(self, offset, write=False):
        if offset not in (self.DATA, self.STATUS) or write and offset != self.DATA:
            raise Fault(8, 'invalid serial register access', self.BASE + offset, 2 if write else 1)

    def peek(self, offset, consumed=0):
        self.check(offset)
        if self.keyboard is not None and consumed >= len(self.input):
            self.feed(self.keyboard.poll())
            self.eof = self.keyboard.eof
        available = consumed < len(self.input)
        if offset == self.STATUS:
            return self.TX_READY | (self.RX_READY if available else 0) | (self.RX_EOF if self.eof else 0)
        return self.input[consumed] if available else 0

    def receive(self):
        return self.input.popleft() if self.input else 0

    def transmit(self, byte):
        byte &= 255
        self.output.append(byte)
        if self.on_output:
            self.on_output(bytes([byte]))
