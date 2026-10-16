# The Nakama server

The launcher, the emulator forks and IKEMEN GO share one Nakama server. The server needs no code of its own for the launcher and the emulators' matches: Nakama's accounts, chat rooms, matchmaker and match relay do the work, and the IKEMEN GO fork's server modules, which IKEMEN GO needs anyway, make the two-player matches. The melonDS fork's online window also has game channels, which its own server module provides.

This document describes setting the server up by hand. Nakama Matchmaker can also do it, and run the server, for a community of any of these games ([hosting.md](hosting.md)).

## What the server does for each client

| Need | Provided by |
|---|---|
| Accounts | Nakama's device authentication |
| Game rooms and chat | Nakama room channels |
| Matchmaking | Nakama's matchmaker, with the client's query and properties ([protocol.md](protocol.md)) |
| Two-player matches | the IKEMEN GO fork's `ikemen_matchmaker` hook creates an authoritative `ikemen_session` match, which relays the clients' messages |
| Three- and four-player matches | the hook leaves them alone, and Nakama makes a relayed match |
| The melonDS fork's game channels (who is there, chat, challenges) | the melonDS fork's `melonds_channel.lua` |
| IKEMEN GO's ranked sets: the claims of their winners, one rating change per set, the players' replays for moderators | the IKEMEN GO fork's modules, and its moderator RPCs, called with the server's HTTP key ([hosting.md](hosting.md#ikemen-gos-ranked-sets)) |
| IKEMEN GO's ranked levels (what players see for their ratings) and a leaderboard per game, which the launcher's Leaderboard shows | the IKEMEN GO fork's modules, with the levels in the runtime environment value `IKEMEN_LEVELS` ([hosting.md](hosting.md#ikemen-gos-ranked-levels)) |
| The game itself | nothing: the players connect to each other directly over UDP |

The emulators' matches use the `unranked` mode, so the IKEMEN GO modules' rating code never runs for them. Without the IKEMEN GO modules every match is one that Nakama relays, which the emulators play as well.

## Setting it up

1. Install Nakama 3 and its database (PostgreSQL or CockroachDB) as [Heroic Labs' documentation](https://heroiclabs.com/docs/nakama/getting-started/install/) describes. The launcher was tested with Nakama 3.41.0 on PostgreSQL 16.
2. Copy the Lua modules from the IKEMEN GO fork (`nakama/modules/*.lua` in [RollinJay/IkemenGo-Nakama](https://github.com/RollinJay/IkemenGo-Nakama)) into the server's runtime folder (`--runtime.path`, or `data/modules` by default). For the melonDS fork's players, add its `src/net/rollback/server/melonds_channel.lua` ([RollinJay/melonDS-test](https://github.com/RollinJay/melonDS-test)). Copies of all of them are in this repository's `server/modules`.
3. Start Nakama with the settings below.

| Setting | Value | Why |
|---|---|---|
| `socket.server_key` | a secret of your own | The launcher and the emulators send it at sign-in (**Server key** in their settings). The default, `defaultkey`, lets anyone use the server. |
| `socket.max_message_size_bytes` | `65536` | IKEMEN GO's replay stream headers are larger than Nakama's 4096-byte default, and the melonDS channel module accepts connection signals of up to 4096 bytes before their envelope. The launcher's and the mGBA fork's messages are under 1 KB. |
| `matchmaker.rev_precision` | `true` | Recommended by the IKEMEN GO fork for ranked matches (both tickets' rating windows are enforced). |
| `runtime.http_key` | a secret of your own | IKEMEN GO's moderator RPCs (its ranked sets) accept calls made with it; Nakama's default, `defaulthttpkey`, is public. |
| `socket.ssl_certificate`, `socket.ssl_private_key` | your certificate | For TLS, which the clients use when **TLS** is checked; or put a TLS proxy in front of the server. |

For example, with the modules in `./modules`:

```
nakama migrate up --database.address postgres:password@127.0.0.1:5432/nakama
nakama --name nakama1 \
  --database.address postgres:password@127.0.0.1:5432/nakama \
  --runtime.path ./modules \
  --socket.server_key "<your key>" \
  --socket.max_message_size_bytes 65536 \
  --matchmaker.rev_precision true \
  --runtime.http_key "<your HTTP key>"
```

Every user of the computer can see a program's arguments; to keep the keys private, put them in a configuration file instead (`--config nakama.yml`, in Nakama's YAML format: `socket.server_key` becomes `server_key:` under `socket:`).

The IKEMEN GO fork's `nakama/README.md` lists its modules' own settings (runtime environment values such as how long ranked replays are kept, or the levels players see for their ratings, `IKEMEN_LEVELS`; `--runtime.env NAME=value`).

## Network

Clients reach the server on its API port (7350 by default) for both HTTP and the realtime WebSocket. Players reach each other directly over UDP on ports their computers choose, after learning their public addresses from public STUN servers; the server carries none of the game traffic, so its bandwidth stays small (sign-ins, chat, matchmaking, and a few kilobytes of signals per match).

Players behind restrictive NATs on both sides may not be able to connect to each other; a relay is not implemented ([README](../README.md#limitations)).
