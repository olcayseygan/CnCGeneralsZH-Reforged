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
2026-11-06T08:51:00.000Z stats sessions=4 in=5120p/880213B out=9877p/1702260B dropped=3p bad=0 hello=1 rate=0 nosession=2 endpoint=0 notarget=0 room=0 unverified=0 alone=14
2026-11-06T08:52:10.000Z leave room=1a2b3c4d:5e6f7a8b vip=10.0.0.3 in=812p/90214B out=790p lastData=118020ms seen=120004ms drop.unverified=3 reason=idle
2026-11-06T08:53:00.000Z room vip=10.0.0.5 from=match:4f1c to=acct:17 in=6911p/1201554B out=6890p lastData=40ms seen=40ms
2026-11-06T08:53:00.000Z empty room=match:4f1c
```

A leave says why: `idle` (120 seconds without a packet) or `version changed` (a HELLO carrying a
known session with different CRCs) or `replaced` (the account logged in again). `setRoom` prints a
`room vip=... from=... to=...` line. Both carry what the session did in the room it is leaving:
`in` is every DATA that carried its id, packets and bytes, and `out` the DATA copies sent to it.
`lastData` is the time since its last DATA was accepted, `never` if none was, and `seen` the time
since anything came from it, so a client that only keeps its HELLO alive shows a small `seen`
beside a large `lastData`. The counters start again from zero when `setRoom` moves the session.
`empty` is printed when the last session leaves a room it was moved into; the rooms sessions open in
(the CRC rooms, `acct:<id>`) empty without a line. A two-player match moves about 290 KB a minute
each way per player through it.

Every datagram that goes nowhere is counted under one cause, in the stats line and on the sessions
involved. `drop.<cause>=n` on a leave or room line is the session as sender, `to.<cause>=n` as
target, and only causes above zero are printed. The causes:

| Cause | Sender | Target | What happened |
|---|---|---|---|
| `bad` | by endpoint | | wrong magic, an unknown type, too short or over 1400 bytes; a HELLO_AUTH with no `authorize` hook |
| `hello` | | | a HELLO or HELLO_AUTH refused: other CRCs from a held endpoint, a move onto an endpoint another session holds, a bad ticket, a cap |
| `rate` | yes | yes | over the source IP's bucket; every listener of a refused broadcast gets a `to.rate` |
| `nosession` | by endpoint | | a session id the relay does not hold, such as the old id after a NAT move or a replace |
| `endpoint` | yes | | a held id arriving from an endpoint that is not its own |
| `notarget` | yes | | a unicast to an address nobody holds, or to the sender itself |
| `room` | yes | yes | a unicast to a session in another room |
| `unverified` | yes | yes | the target has not answered its WELCOME with its id yet; a broadcast that skips such a member counts `to.unverified` on it alone |
| `alone` | yes | | a broadcast nobody in the room could take |

"By endpoint" means the session holding the source address, when there is one. `dropped` sums every
cause but `alone`: a player alone in a lobby broadcasts the whole time and that is not a fault.
Inside a match room, though, `alone` and `to.unverified` are the two to read first.

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

HELLO_AUTH, type 4, client to relay, refused unless the relay was given an `authorize` hook:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `ZR` |
| 2 | 1 | 4 |
| 3 | 4 | exeCRC |
| 7 | 4 | iniCRC |
| 11 | 8 | session, zeros on the first hello |
| 19 | 4 | wantVip |
| 23 | 16 | ticket, from the account server |
| 39 | 0..32 | player name, as in HELLO |

WELCOME, type 2, relay to client, sent in answer to every HELLO or HELLO_AUTH it accepts:

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

## Accounts

A relay started with an `authorize` hook also takes HELLO_AUTH. Everything above holds for it: the
rate limit, the per-IP and room caps, the endpoint check before anything is forwarded. A HELLO_AUTH
that would open a session hands its ticket to `authorize`; a known account gets a session in the
room `acct:<id>`, where it is alone, and an unknown ticket gets nothing back. The relay asks once per
new session. A retry from the same endpoint before the WELCOME arrived, and any HELLO or HELLO_AUTH
carrying the session afterwards, is answered without asking, so the server may burn a ticket on first
use. A session-0 HELLO_AUTH from an endpoint that holds a plain session is dropped.

An account holds one session. Logging in again with a fresh ticket removes the old session (`leave
... reason=replaced`) before the new one opens. The server moves sessions between rooms with
`setRoom`, for example into `match:<id>` at game start and back to `acct:<id>` after; broadcast and
unicast never leave the room, so two players reach each other only once the server put them together.

Plain HELLO keeps working beside the hook, in its (exeCRC, iniCRC) room, so a LAN `-relay` game runs
on the same relay. Without the hook HELLO_AUTH is dropped and the relay behaves as it did before
accounts.

## Using it from a server

`require('./relay')` exports `createRelay(opts)` and the constants `MAX_PACKET`, `IDLE_MS`,
`NO_SESSION`, `HELLO_HEADER`, `HELLO_AUTH_HEADER`, `DATA_IN_HEADER`, `DATA_OUT_HEADER`, plus
`vipString(vip)`. Every option is optional:

| Option | |
|---|---|
| `authorize(ticketHex)` | returns the account id for a ticket (32 hex digits), or `null`. Synchronous. A throw counts as `null` |
| `onAuthorized(accountId, vip)` | a session opened for that account, with its virtual address |
| `onLeave(accountId, atMs)` | an authorised session was removed: idle, replaced or a HELLO with other CRCs. `atMs` is when it was last heard from. One sweep reports in that order, so the first call is the player who went silent first |
| `now()` | clock in ms, `Date.now` by default |
| `log(line)` | one line per event, console by default |
| `rateBytesPerSec`, `burstBytes`, `maxPerIp`, `maxRoom`, `maxBuckets` | the limits above |

The returned object has `start(port, host)` (resolves to the bound port), `stop()` (no `onLeave`
for what is still open), `setRoom(accountId, room)` (false when the account holds no session; the
room cap does not apply, the caller is trusted), `vipOf(accountId)` (`null` when none),
`sessionStats(accountIdOrVip)` and for tests `sessions`, `buckets`, `sweep()` and `report()`.

`sessionStats` looks the key up as an account first, then as a virtual address (the number, not the
dotted string), and returns `null` when no session matches. Otherwise it returns the counters since
the session entered its room: `{ vip, room, at, inP, inB, outP, lastDataMs, seenMs, drop, dropTo,
last }`. `vip` is dotted, `at` is the clock at the call, `lastDataMs` is `null` before the first
DATA, and `drop` and `dropTo` hold every cause in the table above. `last` is the same object, minus
`last`, taken when `setRoom` last moved it, so after a match the counters for the match room are
still there. It is `null` before the first move. A removed session has no stats, so a server
stamping a match report reads them before the session goes idle or is replaced. Account ids are used as map keys and in room names,
so pass strings or numbers, not objects.

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
