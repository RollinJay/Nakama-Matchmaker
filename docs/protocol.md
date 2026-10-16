# Protocol

This document describes what the launcher and the emulators exchange with the Nakama server and with each other. netplay-online implements all of it for the launcher and the mGBA fork: `NakamaClient` (server), `MatchSession` (one match), `PeerLinks` (direct connections) and `GekkoAdapter` (game packets). The melonDS fork implements the same device sign-in, matchmaking, match messages and direct connections with code of its own; its game packets differ ([Game packets](#game-packets)).

The server side is plain Nakama plus the IKEMEN GO fork's server modules ([server.md](server.md)); nothing here needs server code of its own, except the optional `community_info` of [Community servers](#community-servers). The melonDS fork's game channels are a server module of their own ([melonDS's game channels](#melondss-game-channels)).

## Accounts

A player signs in with Nakama's device authentication (`POST /v2/account/authenticate/device?create=true`), with a random device id of 32 hexadecimal digits that the launcher keeps in its settings. The account therefore belongs to the computer.

The username is what the other players see. Usernames are unique on a Nakama server, so the client asks for names in this order, the first that the server accepts winning:

1. the name chosen in the settings;
2. that name followed by `#` and the first four hexadecimal digits of the SHA-1 of the device id, such as `Ryu#3fa0`, which are the same at every sign-in from that computer;
3. no name, for which the server makes one up.

For a new account, the request's `username` parameter carries the name. An existing account whose name is neither the chosen one nor the tagged one is renamed with `PUT /v2/account`, then signed in again so that the session token carries the new name. Nakama answers a name that another account has with 409 at sign-in and with 400 ("Username is already in use") when renaming; both move on to the next name. Whitespace in a name becomes `_`.

## Community servers

A server that Nakama Matchmaker runs ([hosting.md](hosting.md)) answers the RPC `community_info` (`{}` → `{"version": 1, "title": "...", "message": "...", "kinds": ["ikemen", "mgba", "melonds"]}`): the community's name, its message, and the kinds of game it is set up for. The launcher calls it after opening the realtime socket; when it answers, the status line names the community, the message appears in the chat, and a game of another kind is marked as such. Other servers have no such RPC, and the launcher goes on without it.

## IKEMEN GO's leaderboards

For an IKEMEN GO game, the launcher's **Leaderboard** calls the IKEMEN GO modules' RPC `ikemen_leaderboard` with the player's session: `{"game": "<game id>", "limit": 50, "cursor": "<the previous page's cursor>"}` → `{"label": "Power level", "records": [{"rank", "user_id", "name", "level", "level_text", "wins", "losses"}], "cursor": "...", "mine": {...}}`. The records come highest rating first, `cursor` is `""` after the last page, and `mine` is the player's own record, left out when the player has none. The launcher shows `level_text`, the level that the server wrote with thousands separators (`9,001`): levels reach billions and more, past what a 32-bit number holds. A server without the RPC (without IKEMEN GO's modules, or with older ones) answers that it has no such function, and the launcher says so. The IKEMEN GO fork's `nakama/README.md` ("Leaderboards") describes the RPC in full.

## Rooms

Each game has a chat room: a Nakama room channel (`channel_join`, type 1, persistent) named after what the players of that game must share to play each other.

| Game kind | Room name |
|---|---|
| `mgba`, `melonds` | the kind, `.`, and the first 24 digits of the game's identity, e.g. `mgba.12ea7bc41c53d6468b943e26` |
| `ikemen` | `ikemen.<game>.<build>` |

Room names are cut to 64 characters, Nakama's limit. A launcher that cannot compute an identity uses `game.<id>` until it can. Chat messages are `channel_message_send` with the content `{"text": "..."}`, at most 200 characters.

These rooms are the launcher's. The melonDS fork's online window has game channels of its own instead ([melonDS's game channels](#melondss-game-channels)), which the launcher does not join.

## Matchmaking

A player looking for a match adds a matchmaker ticket (`matchmaker_add`) with these string properties:

| Property | Value |
|---|---|
| `game` | the game's identity |
| `kind` | `mgba` or `melonds` |
| `mode` | `unranked` |
| `players` | the number of players per match, as text (`2` for `melonds`) |

no numeric properties, and this query, with `min_count` and `max_count` both equal to the number of players and `count_multiple` 1:

```
+properties.game:<identity> +properties.kind:<kind> +properties.mode:unranked +properties.players:<n>
```

The launcher, the mGBA fork's netplay window ("Find a match") and the melonDS fork's quick match add exactly this ticket, so a player in the launcher and a player in an emulator's own window find each other. For a DS game the ticket is always:

```
+properties.game:<identity> +properties.kind:melonds +properties.mode:unranked +properties.players:2
```

with `min_count` = `max_count` = 2.

When the matchmaker pairs the players (`matchmaker_matched`), the IKEMEN GO fork's matchmaker hook decides what kind of match they get:

- **two players**: the hook creates an authoritative `ikemen_session` match, and every player receives its `match_id`;
- **three or four players**: the hook returns nothing, and every player receives a matchmaker `token` for a relayed match.

A server without these modules, such as a DS community server ([hosting.md](hosting.md)), has no hook: every player receives a `token`. Either way, each player joins with `match_join` (by id or by token). The users in `matchmaker_matched` come in the same order for every player, and that order is the player order: the first user is player 1.

## Messages inside a match

The players send each other JSON messages through the match (`match_data_send`). The server forwards them to every other player: `ikemen_session` relays every op code except its own rematch op codes, and a relayed match forwards everything.

| Op code | Name | Content |
|---|---|---|
| `0x494B5347` | signal | address candidates for the direct connections (below) |
| `0x4E504845` | hello | `{"player": 0, "name": "Ryu", "identity": "<40 digits>", "kind": "mgba"}`, sent after joining |
| `0x4E504348` | chat | `{"text": "..."}`, at most 200 characters |

A player whose hello carries another identity ends the match on the receiving side, with a message that the players' setups differ; the melonDS fork also refuses a hello of another `kind` than `melonds`. A match does not keep messages for players who join later, so the melonDS fork sends its hello again whenever another player joins the match. Its players chat over their direct connection instead of the chat message. The signal op code is the IKEMEN GO fork's (`IKSG`); hello and chat are this project's (`NPHE`, `NPCH`).

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
| `0xA1` | a reliable stream (KCP): the IKEMEN GO fork's, and the melonDS fork's session messages; netplay-online does not use it |
| `0xA2` | a game packet |

Each player pings every other player every second, which gives the round trip, and sends a keepalive when it has sent nothing to a player for 2 seconds. A player that sends a goodbye, or is silent for 8 seconds after connecting, is lost. Closing sends the goodbye three times.

### Game packets

netplay-online runs [GekkoNet](https://github.com/HeatXD/GekkoNet) sessions (the mGBA fork), whose network adapter sends GekkoNet's packets as game packets; GekkoNet addresses a player by its Nakama user id, and netplay-online maps that id to the player's connection.

The melonDS fork runs its own rollback session (GGPO) on the connection: its game packets are GGPO's, and its session messages (the ROM and save checks, settings, desync checks, chat) go in the reliable stream (its `docs/NETPLAY.md`, "Wire formats"). The player who is first in the matchmaker's order hosts that session.

## melonDS's game channels

The melonDS fork's online window gathers the players of each game in a *game channel*: an authoritative match of the fork's channel module (`melonds_channel.lua`, in `server/modules/melonds`), which a client finds or creates with the RPC `melonds_channel_join` and lists with `melonds_channels`. The channel shows who is there and whether they are looking for a game, carries their chat, and pairs two players when one challenges the other and the other accepts; it then relays the pair's connection signals (the signal above) to the pair only. It runs no emulation and sees none of the game's traffic.

A player whose quick match is searching shows as looking in the channel; the search itself is the matchmaker ticket above, not the channel's. The module still has its own quick match (its `QUEUE` message) for older versions of the fork; current versions do not use it.

## Compatibility with IKEMEN GO

The signal, the handshake and the datagram tags are those of the IKEMEN GO Nakama fork's P2P layer (IKEMEN-P2P/1), which netplay-online extends from one peer to several on the same socket. The game packets differ (IKEMEN GO's rollback packets, GekkoNet's and the melonDS fork's), so the emulators and IKEMEN GO cannot play each other; they share the server, the accounts, the rooms and the matchmaker.
