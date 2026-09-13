# O-Prompter Remote Protocol

O-Prompter exposes a local TCP server for LAN remotes.

- Default host: all IPv4 interfaces
- Default port: `4457`
- Discovery port: UDP `4458`
- Encoding: UTF-8
- Framing: one compact JSON object per line
- Authentication: every command must include the active pairing `token`

## Pairing

The OBS dock shows the generated token and server status. Enter that token in the Android app. Regenerate the token from the dock if it is exposed.

## Commands

```json
{"token":"123456","command":"playPause"}
{"token":"123456","command":"play"}
{"token":"123456","command":"pause"}
{"token":"123456","command":"stop"}
{"token":"123456","command":"restart"}
{"token":"123456","command":"top"}
{"token":"123456","command":"speedDelta","value":10}
{"token":"123456","command":"fontSizeDelta","value":4}
{"token":"123456","command":"nextMarker"}
{"token":"123456","command":"previousMarker"}
{"token":"123456","command":"loadUrl","url":"https://etherpad.example/p/my-script"}
{"token":"123456","command":"status"}
```

For Etherpad-style URLs containing `/p/{pad}`, the plugin requests `/export/txt` automatically.

## Discovery

Android remotes can discover local O-Prompter instances by sending this UDP datagram to port `4458` on the local broadcast address:

```json
{"type":"o-prompter-discover","version":1}
```

The plugin replies directly to the sender:

```json
{
  "type": "o-prompter",
  "name": "O-Prompter",
  "title": "Morning bulletin",
  "port": 4457,
  "discoveryPort": 4458,
  "version": 1
}
```

Discovery does not expose the pairing token. The remote still needs the token for TCP control commands.

## Responses

Every valid command returns a status object:

```json
{
  "ok": true,
  "title": "Morning bulletin",
  "playing": true,
  "speed": 70,
  "fontSize": 54,
  "progress": 0.42,
  "positionPx": 1830.4
}
```

Invalid token or malformed commands return:

```json
{"ok":false,"error":"unauthorized"}
```

## Markers

Markers are plain text lines beginning with `#marker`, for example:

```text
#marker Intro
Good evening...
```

The MVP computes marker locations approximately from the marker line's character offset.
