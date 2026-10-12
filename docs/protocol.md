# Protocol

This document describes what the launcher and the emulators exchange with the Nakama server and with each other. netplay-online implements all of it: `NakamaClient` (server), `MatchSession` (one match), `PeerLinks` (direct connections) and `GekkoAdapter` (game packets).

The server side is plain Nakama plus the IKEMEN GO fork's server modules ([server.md](server.md)); nothing here needs server code of its own, except the optional `community_info` of [Community servers](#community-servers).

## Accounts

A player signs in with Nakama's device authentication (`POST /v2/account/authenticate/device?create=true`), with a random device id of 32 hexadecimal digits that the launcher keeps in its settings. The account therefore belongs to the computer.

The username is what the other players see. Usernames are unique on a Nakama server, so the client asks for names in this order, the first that the server accepts winning:

1. the name chosen in the settings;
2. that name followed by `#` and the first four hexadecimal digits of the SHA-1 of the device id, such as `Ryu#3fa0`, which are the same at every sign-in from that computer;
3. no name, for which the server makes one up.

For a new account, the request's `username` parameter carries the name. An existing account whose name is neither the chosen one nor the tagged one is renamed with `PUT /v2/account`, then signed in again so that the session token carries the new name. Nakama answers a name that another account has with 409 at sign-in and with 400 ("Username is already in use") when renaming; both move on to the next name. Whitespace in a name becomes `_`.

## Community servers

A server that Nakama Matchmaker runs ([hosting.md](hosting.md)) answers the RPC `community_info` (`{}` → `{"version": 1, "title": "...", "message": "...", "kinds": ["ikemen", "mgba", "melonds"]}`): the community's name, its message, and the kinds of game it is set up for. The launcher calls it after opening the realtime socket; when it answers, the status line names the community, the message appears in the chat, and a game of another kind is marked as such. Other servers have no such RPC, and the launcher goes on without it.

## Rooms

Each game has a chat room: a Nakama room channel (`channel_join`, type 1, persistent) named after what the players of that game must share to play each other.

| Game kind | Room name |
|---|---|
| `mgba` | the kind, `.`, and the first 24 digits of the profile's identity, e.g. `mgba.12ea7bc41c53d6468b943e26` |
| `melonds` | `game.<id>`: DS players meet in melonDS's own game channel, not in this room |
| `ikemen` | `ikemen.<game>.<build>` |

Room names are cut to 64 characters, Nakama's limit. A launcher that cannot compute an identity uses `game.<id>` until it can. Chat messages are `channel_message_send` with the content `{"text": "..."}`, at most 200 characters.

## Matchmaking

A player looking for a match adds a matchmaker ticket (`matchmaker_add`) with these string properties:

| Property | Value |
|---|---|
| `game` | the profile's identity |
| `kind` | `mgba` or `melonds` |
| `mode` | `unranked` |
| `players` | the number of players per match, as text |

and this query, with `min_count` and `max_count` both equal to the number of players:

```
+properties.game:<identity> +properties.kind:<kind> +properties.mode:unranked +properties.players:<n>
```

The launcher and the emulators' own "Find a match" use the same properties and query, so a player in the launcher and a player in an emulator's netplay window find each other.

When the matchmaker pairs the players (`matchmaker_matched`), the IKEMEN GO fork's matchmaker hook decides what kind of match they get:

- **two players**: the hook creates an authoritative `ikemen_session` match, and every player receives its `match_id`;
- **three or four players**: the hook returns nothing, and every player receives a matchmaker `token` for a relayed match.

Either way, each player joins with `match_join` (by id or by token). The users in `matchmaker_matched` come in the same order for every player, and that order is the player order: the first user is player 1.

## Messages inside a match

The players send each other JSON messages through the match (`match_data_send`). The server forwards them to every other player: `ikemen_session` relays every op code except its own rematch op codes, and a relayed match forwards everything.

| Op code | Name | Content |
|---|---|---|
| `0x494B5347` | signal | address candidates for the direct connections (below) |
| `0x4E504845` | hello | `{"player": 0, "name": "Ryu", "identity": "<40 digits>", "kind": "mgba"}`, sent once after joining |
| `0x4E504348` | chat | `{"text": "..."}`, at most 200 characters |

A player whose hello carries another identity ends the match on the receiving side, with a message that the players' setups differ. The signal op code is the IKEMEN GO fork's (`IKSG`); hello and chat are this project's (`NPHE`, `NPCH`).

## Direct connections

The game itself does not go through the server: each player connects to every other player over UDP, all on one socket.

**Candidates.** Each player gathers address candidates: its own IPv4 addresses (`host`), and its public address as the first STUN server that answers reports it (`srflx`, from the binding response's XOR-MAPPED-ADDRESS). A STUN server gets a binding request every 500 ms for up to 3 seconds.

**Signals.** Each player sends every other player a signal through the match, and sends it again every 500 ms until that player is connected:

```json
{
  "magic": "IKEMEN-P2P/1",
  "match_id": "<the match id>",
  "token": "<16 random bytes, hexadecimal>",
  "candidates": [ { "type": "host", "address": "192.168.1.20", "port": 50211 },
                  { "type": "srflx", "address": "203.0.113.7", "port": 50211 } ],
  "from": "<sender's user id>",
  "to": "<receiver's user id>"
}
```

The token identifies the sender in the handshake. Signals addressed to someone else, or carrying one's own token, are ignored.

**Hole punching.** Every 75 ms, each player sends a handshake packet to every candidate of every player it is not yet connected to:

```json
{ "magic": "IKEMEN-P2P/1", "kind": "hello", "match_id": "<the match id>", "token": "<the sender's token>" }
```

A player that receives a hello answers it three times with the same packet of kind `ack`, and considers the sender connected at the address the hello came from; a player that receives an ack does the same. The connection attempt fails after 20 seconds without a handshake with every player.

**Datagrams.** After the handshake, the first byte of every datagram says what it carries:

| First byte | Carries |
|---|---|
| `0xA0` | control: `0x01` keepalive, `0x02` goodbye, `0x03` ping followed by 8 bytes (microseconds, little-endian), `0x04` pong echoing those 8 bytes |
| `0xA1` | reserved: the IKEMEN GO fork's reliable stream, which these clients do not use |
| `0xA2` | a game packet |

Each player pings every other player every second, which gives the round trip, and sends a keepalive when it has sent nothing to a player for 2 seconds. A player that sends a goodbye, or is silent for 8 seconds after connecting, is lost. Closing sends the goodbye three times.

**Game packets.** The emulators run [GekkoNet](https://github.com/HeatXD/GekkoNet) sessions, whose network adapter sends GekkoNet's packets as game packets; GekkoNet addresses a player by its Nakama user id, and netplay-online maps that id to the player's connection.

## Compatibility with IKEMEN GO

The signal, the handshake and the datagram tags are those of the IKEMEN GO Nakama fork's P2P layer (IKEMEN-P2P/1), extended from one peer to several on the same socket. The game packets differ (IKEMEN GO's rollback packets against GekkoNet's), so an emulator and IKEMEN GO cannot play each other; they share the server, the accounts, the rooms and the matchmaker.
