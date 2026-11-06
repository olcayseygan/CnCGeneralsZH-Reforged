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
