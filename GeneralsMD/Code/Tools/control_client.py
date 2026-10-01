"""Drive a running game over its -control WebSocket.

Start the game with -control (default port 8787) and then, from here:

    from control_client import Control
    with Control() as game:
        print(game.send("status"))
        game.send("spawn 0 GLAInfantryAngryMobNexus 8 760 920 60")
        game.send("attackmove 0 GLAInfantryAngryMobNexus 1400 1400")
        game.send("screenshot")

World commands are the scenario-file grammar with the leading frame number left off, so a line
that works in Run/Scenarios/*.txt works here. The rest are ping, status, screenshot, quit, units
(read what a seat owns) and step (run n logic frames, after which the game waits for the next step).

No dependencies on purpose: this speaks enough of RFC 6455 to talk to one server on loopback, and
anybody who wants to poke the game should not first have to install anything.

Run it directly for a smoke test:

    python control_client.py            # ping, status
    python control_client.py 8787 "spawn 0 AmericaVehicleHumvee 4 800 900"
"""

import base64
import hashlib
import json
import os
import socket
import struct
import sys

DEFAULT_PORT = 8787
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
OPCODE_TEXT = 0x1
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA


class ControlError(RuntimeError):
    pass


class Control(object):
    """One connection to one running game."""

    def __init__(self, port=DEFAULT_PORT, host="127.0.0.1", timeout=10.0):
        self.socket = socket.create_connection((host, port), timeout)
        self.socket.settimeout(timeout)
        self._buffer = b""
        self._handshake(host, port)

    # -- lifetime ---------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self.socket is None:
            return
        try:
            self._send_frame(OPCODE_CLOSE, b"")
        except OSError:
            pass
        self.socket.close()
        self.socket = None

    # -- the handshake ----------------------------------------------------

    def _handshake(self, host, port):
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            "GET / HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (host, port, key)
        )
        self.socket.sendall(request.encode("ascii"))

        while b"\r\n\r\n" not in self._buffer:
            self._fill()

        head, _, rest = self._buffer.partition(b"\r\n\r\n")
        self._buffer = rest
        head = head.decode("ascii", "replace")

        if "101" not in head.split("\r\n")[0]:
            raise ControlError("server refused the upgrade: %s" % head.split("\r\n")[0])

        expected = base64.b64encode(
            hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
        ).decode("ascii")
        for line in head.split("\r\n"):
            name, _, value = line.partition(":")
            if name.strip().lower() == "sec-websocket-accept":
                if value.strip() != expected:
                    raise ControlError("wrong accept key: %r" % value.strip())
                return
        raise ControlError("no Sec-WebSocket-Accept in the reply")

    # -- framing ----------------------------------------------------------

    def _fill(self):
        chunk = self.socket.recv(4096)
        if not chunk:
            raise ControlError("the game closed the connection")
        self._buffer += chunk

    def _send_frame(self, opcode, payload):
        header = bytearray([0x80 | opcode])
        mask = os.urandom(4)
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", length)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", length)
        header += mask
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        self.socket.sendall(bytes(header) + masked)

    def _read_frame(self):
        while True:
            while len(self._buffer) < 2:
                self._fill()

            first, second = self._buffer[0], self._buffer[1]
            opcode = first & 0x0F
            length = second & 0x7F
            at = 2

            if length == 126:
                while len(self._buffer) < at + 2:
                    self._fill()
                length = struct.unpack(">H", self._buffer[at:at + 2])[0]
                at += 2
            elif length == 127:
                while len(self._buffer) < at + 8:
                    self._fill()
                length = struct.unpack(">Q", self._buffer[at:at + 8])[0]
                at += 8

            while len(self._buffer) < at + length:
                self._fill()

            payload = self._buffer[at:at + length]
            self._buffer = self._buffer[at + length:]

            if opcode == OPCODE_CLOSE:
                raise ControlError("the game closed the connection")
            if opcode == OPCODE_PING:
                self._send_frame(OPCODE_PONG, payload)
                continue
            if opcode == OPCODE_PONG:
                continue
            return payload.decode("utf-8", "replace")

    # -- commands ---------------------------------------------------------

    def send(self, command):
        """Send one command and return the reply, decoded from JSON."""
        self._send_frame(OPCODE_TEXT, command.encode("utf-8"))
        reply = self._read_frame()
        try:
            return json.loads(reply)
        except ValueError:
            raise ControlError("the game answered something that is not JSON: %r" % reply)

    # a few names so that calling code reads like what it is doing

    def status(self):
        return self.send("status")

    def spawn(self, slot, template, count, x, y, spacing=None):
        line = "spawn %d %s %d %g %g" % (slot, template, count, x, y)
        if spacing is not None:
            line += " %g" % spacing
        return self.send(line)

    def move(self, slot, selector, x, y):
        return self.send("move %d %s %g %g" % (slot, selector, x, y))

    def attack_move(self, slot, selector, x, y):
        return self.send("attackmove %d %s %g %g" % (slot, selector, x, y))

    def stop(self, slot, selector):
        return self.send("stop %d %s" % (slot, selector))

    def teleport(self, slot, selector, x, y):
        return self.send("teleport %d %s %g %g" % (slot, selector, x, y))

    def units(self, slot, selector="*"):
        """The list of {id, template, x, y, health, maxHealth} the seat owns under the selector."""
        return self.send("units %d %s" % (slot, selector))["units"]

    def step(self, frames=1):
        """Run that many logic frames and return the frame reached. From the first call on the game
        runs no logic frame it was not stepped through, until this connection closes."""
        return self.send("step %d" % frames)["frame"]

    def follow(self, object_id):
        """Lock the camera on the object with that id, as units reports it."""
        return self.send("follow %d" % object_id)

    def overlay(self, lines):
        """Write these lines in the top left corner; a line starting '>' is highlighted, no lines clears it."""
        return self.send("overlay " + "\n".join(lines))

    def screenshot(self):
        return self.send("screenshot")


def main(argv):
    port = int(argv[1]) if len(argv) > 1 else DEFAULT_PORT
    commands = argv[2:] or ["ping", "status"]

    with Control(port) as game:
        for command in commands:
            print("%-60s -> %s" % (command, game.send(command)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
