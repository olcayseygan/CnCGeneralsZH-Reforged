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
is the whole deployment. Give it a machine that is not also running the game flat out: two uncapped
copies on the same PC starved it for up to 69 seconds at normal priority, and every match through it
stalled.

The game side is `-relay host:port`. Entering the LAN screen waits up to five seconds for the
relay's WELCOME, with the screen frozen, and a relay that never answers is reported as a network
socket error; the next try waits ten more seconds.

It prints one line per join and leave, with the room and the virtual address, and once a minute a
counter line:

```
2026-11-06T08:50:00.000Z join room=1a2b3c4d:5e6f7a8b vip=10.0.0.3 name="Tank Girl"
2026-11-06T08:51:00.000Z stats sessions=4 in=5120p/880213B out=9877p/1702260B dropped=3p
2026-11-06T08:52:10.000Z leave room=1a2b3c4d:5e6f7a8b vip=10.0.0.3 reason=idle
```

A leave says why: `idle` (120 seconds without a packet) or `version changed` (a HELLO carrying a
known session with different CRCs). A two-player match moves about 290 KB a minute each way per
player through it.

## Wire format

UDP, one message per datagram. Integers are big-endian. Every packet starts with the two bytes
`Z` `R` (0x5A 0x52) and a type byte. No datagram may exceed 1400 bytes; larger ones are dropped,
which leaves 1381 bytes of payload in a client's DATA. A session is 8 opaque bytes the relay picks
at random; all zeros means none.

HELLO, type 1, client to relay:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 1 |
| 3 | 4 | exeCRC |
| 7 | 4 | iniCRC |
| 11 | 8 | session, zeros on the first hello |
| 19 | 4 | wantVip, the address asked for, 0 for any |
| 23 | 0..32 | player name, UTF-8, may be empty (longer is cut at 32 bytes) |

WELCOME, type 2, relay to client, sent in answer to every HELLO it accepts:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 2 |
| 3 | 8 | session, the one to use from now on |
| 11 | 4 | vip, the client's virtual IPv4 address (10.x.y.z) |

DATA, type 3, client to relay:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 3 |
| 3 | 8 | session |
| 11 | 4 | dstVip, 0xFFFFFFFF for broadcast |
| 15 | 2 | dstPort |
| 17 | 2 | srcPort, the sender's own logical port |
| 19 | .. | payload |

DATA, type 3, relay to client:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 3 |
| 3 | 4 | srcVip |
| 7 | 2 | srcPort |
| 9 | 2 | dstPort |
| 11 | .. | payload |

The relay never reads the payload. A WELCOME is smaller than the HELLO it answers, so a HELLO from a
forged address gets nothing bigger back.

## Sessions and rooms

A HELLO with no session from an address and port that do not hold one opens a session: a random
8-byte session number and a virtual address, wantVip when it is a 10.x.y.z address, not .0 or .255,
that no session holds, the next free one otherwise. The lobby asks for nothing at first and for the
address it has on every HELLO after, so a relay that restarted or forgot it gives the same address
back if it is still free. A `-netgame` run asks from the start, because its slot list names every
copy's address up front, so `-netgame 10.0.0.1,10.0.0.2 -netslot n -relay host:port` plays a
scripted match through the relay (`net_check.py --relay`).

A HELLO with no session, or one the relay does not know, from an address and port that already hold
a session gets that session back when the CRCs match and nothing when they do not. The game resends
HELLO every 500 ms until it hears a WELCOME, and on a slow link the second one arrives first; and a
forged HELLO from someone's address cannot end their session.

Nothing is forwarded to a session until a packet carrying its number has come back from its address.
The number only ever goes out in a WELCOME to that address, so a session opened by a HELLO with a
forged source address is never sent anything. The game answers its first WELCOME with a second HELLO
at once, and says HELLO every 10 seconds after that as its keepalive.

Only a HELLO moves a session to another address and port, which is what NAT rebinding looks like.
The session gets a new number in the WELCOME to the new address, and is forwarded to again once that
number comes back, so a session can be moved only by whoever reads the new address. A session is
never moved onto an address another session holds. DATA from any address other than the session's
own is dropped. Keep the session number out of logs and screenshots all the same.

The room is the (exeCRC, iniCRC) pair, which is what the game's own LAN join already insists on.
Broadcast goes to every other session in the sender's room. Unicast to an address in another room,
or to one that does not exist, is dropped.

A session with no packet for 120 seconds is removed and its address can be handed out again. The
game says HELLO every 10 seconds, but only while it pumps its network, and a map load can block
for a while between two pumps; two minutes outlasts that.

## Limits

The relay holds at most 4096 sessions, 8 opened from one IP address and 16 in one room.

Each source IP address has a token bucket in bytes: 2 MiB of burst, refilled at 512 KiB a second.
A HELLO costs its length, a DATA its length for every copy the relay sends, and neither less than 64
bytes a copy, so a broadcast to fifteen players costs fifteen sends and a flood of tiny packets runs
dry as fast as one of big ones. The burst is sized for a map transfer, which is the only time the
game sends much; a lobby and a match sit far under the refill rate. Players behind one carrier NAT
share an address and so a bucket. At most 65536 buckets are kept, and a new address is dropped
while the table is full; a bucket left alone long enough to fill up again is forgotten. Packets over
any limit are dropped and counted in the `dropped` figure. The numbers are constants at the top of
`relay.js`.
