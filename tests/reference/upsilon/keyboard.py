"""POSIX terminal/pipe input, restoring terminal settings on every exit path."""
import os
import select
import sys
import termios
import tty

class Keyboard:
    def __init__(self, stream=None):
        self.stream = stream if stream is not None else sys.stdin
        self.fd = self.stream.fileno()
        self.saved = None
        self.eof = False

    def __enter__(self):
        if os.isatty(self.fd):
            self.saved = termios.tcgetattr(self.fd)
            tty.setcbreak(self.fd)
        return self

    def __exit__(self, *exc):
        if self.saved is not None:
            termios.tcsetattr(self.fd,termios.TCSADRAIN,self.saved)

    def poll(self):
        if self.eof:
            return b''
        if not select.select([self.fd],[],[],0.01)[0]:
            return b''
        data = os.read(self.fd,256)
        if not data:
            self.eof = True
        return data
