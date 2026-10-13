# Nakama Matchmaker

Nakama Matchmaker is a launcher that finds online opponents through a [Nakama](https://heroiclabs.com/nakama/) server and starts the game. It can also run that server: a *community server* for the players of IKEMEN GO, of GBA games or of DS games, started from its window or from the command line ([docs/hosting.md](docs/hosting.md)). It serves three engines:

- **mGBA** (Game Boy Advance), through the mGBA fork at [RollinJay/mgba](https://github.com/RollinJay/mgba) with its netplay changes;
- **melonDS** (Nintendo DS), through the melonDS fork at [RollinJay/melonDS-test](https://github.com/RollinJay/melonDS-test) with its netplay changes (see [Games](#games) for how its current netplay finds matches);
- **IKEMEN GO**, through its Nakama fork at [RollinJay/IkemenGo-Nakama](https://github.com/RollinJay/IkemenGo-Nakama).

Its window is laid out like Fightcade's: the games on the left, the chosen game's chat room in the middle, the players in that room on the right, and buttons that find a match or start the game offline.

![The launcher, signed in, with two players in a game's room](docs/launcher.png)

The repository also holds **netplay-online**, the library that the launcher and the emulator forks share: a Nakama client, direct UDP connections between players, and the file that hands a match from the launcher to an emulator.

## How a match is made

1. Each player signs in to the Nakama server with an account tied to the computer (a random device id kept in the settings) and opens a game. Opening a game joins its chat room.
For an mGBA game:

2. **Find a match** asks the emulator for the game's *identity*: a hash of the emulator build, the netplay profile, and every ROM and save the profile names. Two players can only play each other when their identities are equal, so the identity is what the matchmaker pairs on, and it also names the room.
3. When the matchmaker has enough players, the launcher writes the match to a private file and starts the emulator with the file's path in the `NETPLAY_CONNECT` environment variable ([docs/launch.md](docs/launch.md)).
4. The emulator joins the match with the launcher's session, connects to the other players directly over UDP, and plays the game with rollback through [GekkoNet](https://github.com/HeatXD/GekkoNet). Only the players' inputs travel between computers; every computer runs every console. The launcher stays in the room, and becomes ready for the next match when the emulator exits.

IKEMEN GO and the melonDS fork make their own matches. The launcher starts them with a server profile, a private file that names the launcher's server and account, so they sign in as the same player:

- IKEMEN GO (`-nakama-config`, `-nakama-server`): its online menus (quick match, lobbies) do the rest.
- the melonDS fork (`--netplay-config <file> --netplay-online --netplay-quick --netplay-exit <ROM>`): melonDS opens its online window, joins the loaded game's channel on the server and asks for a quick match there. Two players of the same game who press **Play online** are paired and play with rollback (GGPO). melonDS quits when the session ends, and the launcher is ready again.

## Games

| Kind | Program | Where matches are made | Players |
|---|---|---|---|
| `mgba` | mGBA fork built with `USE_NETPLAY` | the launcher | 2 to 4, as the profile's link needs |
| `melonds` | the melonDS fork (rollback netplay) | melonDS's game channel, on a server with its channel module | 2 |
| `ikemen` | IKEMEN GO Nakama fork | IKEMEN GO's online menus | as in IKEMEN GO |

The melonDS fork at [RollinJay/melonDS-test](https://github.com/RollinJay/melonDS-test) plays online with rollback (GGPO) and finds its opponents in game channels: one per game, on a server that has its channel module, such as a DS community server ([docs/hosting.md](docs/hosting.md)). Players can also open it without the launcher (System > Multiplayer > Play online (rollback)...) and meet the launcher's players in the same channel; its `docs/NETPLAY.md` describes it. The launcher no longer starts melonDS builds that read `NETPLAY_CONNECT` (an earlier netplay version of the fork).

An mGBA game is a *netplay profile*: an INI file of the emulator fork that names the ROMs, how the consoles are linked, and which console each player's keys go to. The mGBA fork's `doc/netplay.md` describes its profiles. A melonDS game is a DS ROM (`.nds`); **Play offline** starts melonDS with it and nothing else. A profile that names its ROMs through environment variables (`${VAR}`), as the forks' examples do, needs those variables in the launcher's environment, which the emulator inherits; a profile of one's own can name the files' paths instead.

## Building

The launcher needs CMake 3.16 or later, a C++14 compiler, and Qt 6 or Qt 5.10 or later (Core, Network, Widgets).

```
cmake -B build
cmake --build build
```

This builds `build/launcher/nakama-matchmaker`, `build/launcher/nakama-matchmaker-server` (the community servers without a window, [docs/hosting.md](docs/hosting.md)) and the test tool `build/netplay-online-test`. `-DBUILD_TOOLS=OFF` leaves the test tool out, `-DBUILD_SERVER=OFF` the server program. `-DBUILD_LAUNCHER=OFF` builds the server program alone, which needs only Qt's Core and Network modules, for a computer without a desktop.

## Setting up

**Settings** holds the player's name, the server (host, port, server key, TLS), the STUN servers that tell each player its public address, and where the programs are: the mGBA fork's `mgba-qt`, the melonDS fork's executable and IKEMEN GO's executable.

The settings (`settings.ini`) and the game list (`games.json`) are kept in the configuration folder: `~/.config/nakama-matchmaker` on Linux, `%LOCALAPPDATA%\nakama-matchmaker` on Windows and `~/Library/Preferences/nakama-matchmaker` on macOS. `--config` selects another folder.

**Add** and **Edit** fill in the game list. Each game in `games.json` has these fields:

| Field | Kinds | Meaning |
|---|---|---|
| `id` | all | Letters, digits and dashes; unique in the list |
| `title` | all | The name in the list |
| `kind` | all | `mgba`, `melonds` or `ikemen` |
| `profile` | `mgba`, `melonds` | Path of the netplay profile (`mgba`) or of the ROM (`melonds`) |
| `players` | `mgba` | Players per match |
| `game`, `build` | `ikemen` | The IKEMEN GO pool the account plays in |
| `workdir` | all | Folder to start the program in (IKEMEN GO's folder by default) |
| `args` | all | Extra arguments for the program |
| `command` | all | Program and arguments to run instead of the configured program |
| `identity_command` | `mgba` | Program and arguments that print the identity; `{profile}` stands for the profile's path |

Without `identity_command`, the launcher runs `mgba-qt --netplay-identity <profile>`. [docs/games.example.json](docs/games.example.json) shows an mGBA game, a melonDS game, IKEMEN GO, and a headless test entry that uses `command` and `identity_command`.

Player names are unique on the server. When another account already has the name, the account gets the name followed by a tag made from the device id, such as `Ryu#3fa0`; the tag stays the same at every sign-in.

## Command line

| Option | Meaning |
|---|---|
| `--config <folder>` | The configuration folder |
| `--name <name>` | Player name (saved) |
| `--device <id>` | Device id to sign in with (saved); one account per id |
| `--host <host>`, `--port <port>` | Nakama server (saved) |
| `--auto <game id>` | No window: sign in, find a match for that game, play it, and exit with the game's exit code |
| `server <command>` | No window: run and manage community servers ([docs/hosting.md](docs/hosting.md)); `nakama-matchmaker-server <command>` does the same |

## The server

Any Nakama 3 server with the IKEMEN GO fork's server modules serves the launcher, the mGBA fork and IKEMEN GO alike; the melonDS fork also needs its own channel module. Nakama Matchmaker can run such a server itself, set up for the community's games, from its **Servers** window or its `server` commands ([docs/hosting.md](docs/hosting.md)); [docs/server.md](docs/server.md) describes setting one up by hand.

## Documents

- [docs/launch.md](docs/launch.md): the file that hands a match from the launcher to an emulator.
- [docs/protocol.md](docs/protocol.md): accounts, rooms, matchmaking, the messages inside a match, and the direct connections between players.
- [docs/server.md](docs/server.md): the Nakama server, set up by hand.
- [docs/hosting.md](docs/hosting.md): community servers that Nakama Matchmaker runs.

## Testing

`netplay-online-test` checks the library against a server without an emulator: run it on two (or more) terminals with different `--device` ids and the same `--players`; each one signs in, matchmakes, joins, connects directly to the others, measures the round trip, exchanges a chat line, and prints `OK`.

```
build/netplay-online-test --device test-device-one --name one
build/netplay-online-test --device test-device-two --name two
```

Two launchers with `--auto` and separate `--config` folders test the whole path, including the start of the emulator. Running an emulator fork's headless player as the game's `command` makes the run headless: the mGBA fork's `mgba-netplay-online-test` plays a fixed number of frames with scripted keys and print a checksum of every console's memory, which must be equal for every player (`--trace` writes one per frame).

## Status

Tested on Ubuntu 24.04 with Qt 5.15 against Nakama 3.41.0, with every player on one computer:

- mGBA fork: two players (two linked GBAs) and three players (three linked GBAs) matched, launched, connected and played to identical checksums, headless; two `mgba-qt` windows played a match started by two launchers;
- melonDS fork (rollback): two launchers with `--auto` on a DS community server started two melonDS windows, which were paired by quick match in the game's channel and played (3,564 frames, 60 desync checks passed) until one was closed; the other launcher then reported the end of the game. The ROM was a small homebrew test ROM, so a game's own DS wireless play was not part of the test;
- the launcher's window: sign-in, rooms, chat, matchmaking, starting the game and returning when it exits;
- community servers: see [docs/hosting.md](docs/hosting.md#tested-and-not).

Not yet tested: players on different networks behind real NATs, Windows and macOS builds, and starting IKEMEN GO from the launcher.

## Limitations

- The direct connections use STUN only. A player behind a symmetric NAT cannot connect to one behind a symmetric or port-restricted NAT; a relay (TURN, or relaying through the Nakama match) is not implemented.
- Every player in a match needs every other player's inputs, so the match ends when any player leaves.
- The account belongs to the computer: there is no sign-in with email or another provider, and moving to another computer means a new account.
- The launcher does not make IKEMEN GO's matches; IKEMEN GO has no option to join a match found elsewhere.

## License

MIT (see [LICENSE](LICENSE); `netplay-online/LICENSE` holds the same text for the forks' copies). netplay-online is MIT so that both emulator forks can include it: mGBA is under the MPL 2.0 and melonDS under the GPL 3.0.

The server modules in `server/modules` are MIT too ([server/modules/README.md](server/modules/README.md)): IKEMEN GO's, and the melonDS fork's channel module, which contains no melonDS code. The emulator forks themselves keep their own licenses (mGBA: MPL 2.0, melonDS: GPL 3.0).
