# Network and local control configuration

## Local WebSocket tools

`-control [port]` still binds only to `127.0.0.1` and is disabled unless requested.
The existing Python `Tools/control_client.py`, network checks and other local
native tools retain their originless WebSocket handshake and command grammar.

Browser clients need an explicitly configured capability. Generate a fresh token
for each session, set `ZH_CONTROL_TOKEN` in the game's environment **before
starting it**, and use the same token as a WebSocket subprotocol:

```sh
export ZH_CONTROL_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
# Start the executable from this shell, with -control and your usual -root/-overlay arguments.
```

In the browser console, enter the generated value locally:

```js
const token = prompt('Session token from the game environment');
const control = new WebSocket('ws://127.0.0.1:8080', 'zhr-control.' + token);
control.onopen = () => control.send('ping');
control.onmessage = event => console.log(event.data);
```

Use your selected port if it differs. Tokens must contain 32–128 ASCII letters,
digits, underscores or hyphens. Browser handshakes with any Origin (including
`null`) require the capability. Malformed/duplicate authorization headers are
rejected before commands are accepted. Do not put session tokens in source code,
URLs, screenshots or shared logs. Native clients do not gain remote access;
they remain local processes with the user's existing authority.

## Configured GameSpy-compatible services

The historical peer/stats code and matchbot remain available. Embedded title
credentials have been removed from both source trees. A service operator must
provide credentials for a service you are authorized to use:

```sh
export ZH_GAMESPY_TITLE='your_service_title'
# Set ZH_GAMESPY_SECRET_KEY privately in the process environment; do not commit it.
```

Peer and statistics connections read the same configuration. Zero Hour and
Generals retain their respective default title names if `ZH_GAMESPY_TITLE` is
unset; beta/custom services can override the name. The key is always required.
Both fields accept 1–31 letters, digits, underscores or hyphens, fitting the
narrowest SDK buffer without truncation. Invalid configuration fails through the
existing connection-failure path. The legacy matchbot uses its existing
`[LOGIN] Title` and `SecretKey` settings instead of a built-in key.

A shared client title key is not per-user authentication. This repository cannot
revoke keys at external services or establish that an old service still accepts
them; operators must rotate any historical credentials they still support.

## UDP trust boundary

Unknown match endpoints are discarded before packet parsing, acknowledgements or
liveness changes. Quitting connections may finish self-authored acknowledgements
while new controls are rejected. Direct file, loading and disconnect controls
must come from their claimed player. Wrapped commands retain the physical sources
of all chunks and are checked again after reassembly. Router forwarding and
explicitly requested historical frame recovery remain supported. Run-ahead
commands require an authorized current/former router and valid rate/window values;
a player-leave command can name only its own author.

The existing wire format is retained. Its CRC and fixed XOR do **not** provide
cryptographic authentication: the current router and requested recovery peers
remain trusted, and a forged network source is not cryptographically detectable.
Protection against a malicious trusted relay needs an authenticated session
protocol; endpoint and command-policy checks alone do not establish that property.
