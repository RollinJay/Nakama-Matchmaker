# Handing a match to an emulator

When the matchmaker pairs players for an mGBA game, the launcher does not play the match itself: it writes the match to a file and starts the emulator, which joins the match and plays it. (The rollback melonDS fork and IKEMEN GO make their own matches and get a server profile instead; see the README's [Games](../README.md#games).) This document describes that file (the *connection file*, version 1) and what an emulator does with it. netplay-online's `ConnectInfo` reads and writes it.

## Passing the file

The launcher writes the file into a new temporary folder that only the user can open, gives the file itself owner-only permissions (0600), and starts the emulator with the file's absolute path in the environment variable `NETPLAY_CONNECT`. The file holds the player's session tokens, so the emulator deletes it as soon as it has read it, and removes `NETPLAY_CONNECT` from its own environment so that programs it starts do not inherit it. The launcher deletes the folder when the emulator exits.

An emulator started without `NETPLAY_CONNECT` behaves as usual.

## Format

The file is a JSON object:

```json
{
  "version": 1,
  "server": { "host": "nakama.example.org", "port": 7350, "key": "defaultkey", "ssl": true },
  "session": { "token": "<session token>", "refresh_token": "<refresh token>" },
  "match": { "match_id": "", "token": "<matchmaker token>" },
  "players": [
    { "user_id": "3975983e-f8e2-4041-8596-298df80e07bc", "name": "Ryu" },
    { "user_id": "8b8bc040-9c82-46e0-9a64-b5de8f332932", "name": "Ken#3fa0" }
  ],
  "self": 1,
  "game": { "kind": "mgba", "profile": "/home/ryu/netplay/vs.ini", "identity": "12ea7bc41c53d6468b943e26d844aae4fd3e5666" },
  "stun": [ "stun.l.google.com:19302" ],
  "port": 0,
  "input_delay": -1
}
```

| Field | Meaning |
|---|---|
| `version` | 1. A reader refuses other versions. |
| `server` | The Nakama server: host, port (7350 by default), server key, and whether it uses TLS. |
| `session` | The launcher's session on that server. The emulator uses it as is, so the emulator is the same Nakama user as the launcher; it refreshes the session with the refresh token when the session is about to expire. |
| `match` | The match the matchmaker made: either an authoritative match's `match_id`, or the matchmaker `token` of a relayed match (one of the two is not empty). |
| `players` | Every player of the match, in player order: player 1 first. `user_id` is the Nakama user id, `name` the username. |
| `self` | The local player's index in `players` (from 0). |
| `game.kind` | `mgba`; `melonds` was used by an earlier netplay version of the melonDS fork, which the launcher no longer starts. |
| `game.profile` | The netplay profile that the launcher matched on, as a path on this computer. |
| `game.identity` | The profile's identity as the launcher computed it (40 hexadecimal digits). The emulator computes it again and refuses the match when they differ, since the players could not stay in sync. |
| `stun` | STUN servers (`host:port`) for finding the public address. May be empty. |
| `port` | UDP port for the direct connections; 0 lets the emulator choose. |
| `input_delay` | Input delay in frames; -1 lets the emulator choose from the measured round trip. |

The player order comes from the matchmaker's result, which every player receives in the same order, so every player agrees on who is player 1 without further messages.

## What the emulator does

1. Reads and deletes the file, and checks the profile's identity.
2. Opens the realtime socket with the session and joins the match (by id, or by token).
3. Sends its hello (player name, identity and kind) to the match, then exchanges address candidates with the other players through the match and connects to each of them directly ([protocol.md](protocol.md)).
4. Plays. When the match ends, it leaves the match and exits (or returns to its own window); the launcher notices that the program exited.

## Identities

The identity is what makes two players compatible, so the launcher and the emulator compute it the same way: the launcher asks the emulator. An emulator supports

```
<emulator> --netplay-identity <profile>
```

which prints the identity on the last line of standard output and exits with status 0, or prints an error on standard error and exits with another status. The launcher asks again every time it looks for a match, so a ROM or save replaced in place is noticed; the room it shows for a game may use an earlier answer.
