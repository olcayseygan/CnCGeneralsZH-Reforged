# Virtual LAN relay

The game's LAN mode finds games by broadcast and talks to peers by address, so it stops at the
router. This relay gives every connected copy of the game a virtual address in 10.0.0.0/8 and
forwards its LAN traffic to the other copies that run the same exe and INI set. Each game thinks it
is on one LAN; the relay is the switch.

It is one Node file with no dependencies, and it keeps no state on disk.

## Running it

```
node relay.js                  # UDP 8094
RELAY_PORT=9000 node relay.js  # another port
node test.js                   # self-check, exit code 0 on pass
```

Tested on Node 22. The relay listens on every interface; open the UDP port in the firewall and that
is the whole deployment.

It prints one line per join and leave, with the room and the virtual address, and once a minute a
counter line:

```
2026-11-06T08:50:00.000Z join room=1a2b3c4d:5e6f7a8b vip=10.0.0.3 name="Tank Girl"
2026-11-06T08:51:00.000Z stats sessions=4 in=5120p/880213B out=9877p/1702260B dropped=3p
2026-11-06T08:52:10.000Z leave room=1a2b3c4d:5e6f7a8b vip=10.0.0.3 reason=idle
```

A leave says why: `idle` (30 seconds without a packet), `replaced` (a new session came from the
same address and port, which is a restarted game), or `version changed` (a hello for a known
session with different CRCs).

## Wire format

UDP, one message per datagram. Integers are big-endian. Every packet starts with the two bytes
`Z` `R` (0x5A 0x52) and a type byte. No datagram may exceed 1400 bytes; larger ones are dropped,
which leaves 1385 bytes of payload in a client's DATA.

HELLO, type 1, client to relay:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 1 |
| 3 | 4 | exeCRC |
| 7 | 4 | iniCRC |
| 11 | 4 | session, 0 on the first hello |
| 15 | 0..32 | player name, UTF-8, may be empty (longer is cut at 32 bytes) |

WELCOME, type 2, relay to client, sent in answer to every HELLO:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 2 |
| 3 | 4 | session |
| 7 | 4 | vip, the client's virtual IPv4 address (10.x.y.z) |

DATA, type 3, client to relay:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 3 |
| 3 | 4 | session |
| 7 | 4 | dstVip, 0xFFFFFFFF for broadcast |
| 11 | 2 | dstPort |
| 13 | 2 | srcPort, the sender's own logical port |
| 15 | .. | payload |

DATA, type 3, relay to client:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 3 |
| 3 | 4 | srcVip |
| 7 | 2 | srcPort |
| 9 | 2 | dstPort |
| 11 | .. | payload |

The relay never reads the payload.

## Sessions and rooms

A HELLO with session 0, or with a session the relay does not know, opens a new session with a
fresh random session number and the next free virtual address. A HELLO carrying a live session and
the same two CRCs keeps both, so the game should send HELLO every 10 seconds or so as its
keepalive and to learn its address after a reconnect. The session number is the only credential:
any packet carrying a valid one is accepted from whatever address it arrives from, and replies go
to that address from then on. That is how a NAT that rebinds the client's port stays connected.
Keep the session number out of logs and screenshots.

The room is the (exeCRC, iniCRC) pair, which is what the game's own LAN join already insists on.
Broadcast goes to every other session in the sender's room. Unicast to an address in another room,
or to one that does not exist, is dropped.

A session with no packet for 30 seconds is removed and its address can be handed out again. The
relay holds at most 4096 sessions.

## Rate limit

Each source address and port has a token bucket in bytes: 2 MiB of burst, refilled at 512 KiB a
second. A packet costs its length, and never less than 64 bytes, so a flood of tiny packets runs
dry as fast as one of big ones. The burst is sized for a map transfer, which is the only time the
game sends much; a lobby and a match sit far under the refill rate. Packets over the limit are
dropped and counted in the `dropped` figure. The numbers are constants at the top of `relay.js`.
