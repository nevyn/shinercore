#!/usr/bin/env python3
# Resets a core through RTS and logs its serial output from the first boot line.
# Usage: test/bootlog.py PORT SECONDS OUTFILE. Stdlib only; see docs/building-and-testing.md.
import os, sys, termios, fcntl, struct, time, select

port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
bits = lambda req, m: fcntl.ioctl(fd, req, struct.pack('I', m))
bits(termios.TIOCMBIC, termios.TIOCM_DTR)   # IO0 high: normal boot
bits(termios.TIOCMBIS, termios.TIOCM_RTS)   # EN low: hold in reset
time.sleep(0.1)
bits(termios.TIOCMBIC, termios.TIOCM_RTS)   # release
end = time.time() + secs
with open(out, 'wb') as f:
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                f.write(os.read(fd, 4096))
                f.flush()
            except BlockingIOError:
                pass
