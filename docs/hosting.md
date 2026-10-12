# Running a community server

A **community server** is a Nakama server that Nakama Matchmaker runs for a group of players: an IKEMEN GO community, a community of GBA games (played with the mGBA fork), a community of DS games (played with the melonDS fork), or one community that plays several of them. Players connect to it from their own programs, find each other there, and then play over direct connections between their computers; the server carries the sign-ins, the chat and the matchmaking, not the games.

Nakama Matchmaker does not contain a server of its own. It runs [Nakama](https://heroiclabs.com/nakama/), the open-source game server that the launcher, the emulator forks and IKEMEN GO already use, and does the setting up that a server otherwise needs by hand ([server.md](server.md) describes that manual setup):

- it writes Nakama's configuration, with keys and passwords of its own for every server;
- it installs the server modules that the community's games need;
- it gives the server a database of its own (a PostgreSQL that it creates and runs), unless the server is given an existing database;
- it starts the database and Nakama, checks that they answer, keeps a log, and stops them in order.

Servers can be started from the launcher's window or from the command line, and several can run at once, each with its own ports, database and folder.

## What a server needs

- **Nakama 3** (tested with 3.41.0). Download it from [its releases](https://github.com/heroiclabs/nakama/releases) and put the `nakama` program next to Nakama Matchmaker, or name its path in the servers' settings.
- **PostgreSQL** (tested with 16), for a server with a database of its own. Nakama Matchmaker uses PostgreSQL's programs (`initdb`, `postgres`, `pg_ctl` and `pg_isready`) and looks for them where PostgreSQL usually installs them; it does not need a PostgreSQL service to be running. PostgreSQL does not run as root, or with administrator rights on Windows: run Nakama Matchmaker as an ordinary user. A server can instead use a PostgreSQL or CockroachDB database that already exists.
- **One TCP port** that players can reach: 7350 by default.

## What each kind of game gets

A server is set up for one or more kinds of game. The kinds decide which server modules (the Lua code that Nakama runs) are installed:

| Kind | Players use | Server modules | Why |
|---|---|---|---|
| IKEMEN GO (`ikemen`) | the IKEMEN GO Nakama fork's online menus | IKEMEN GO's modules | quick and ranked matchmaking, lobbies, ratings, online names |
| GBA (`gba`, or `mgba`) | the mGBA fork's netplay window, or Nakama Matchmaker | IKEMEN GO's modules | two-player matches run as IKEMEN GO's sessions; three and four players use matches that Nakama relays |
| DS (`ds`, or `melonds`) | the melonDS fork's online window | melonDS's channel module | the game channels: who is there, chat, challenges and quick match |

Every server also gets Nakama Matchmaker's community module, which tells clients the community's name and message, and stops players from writing into the records that the server modules keep (without it, a player could give themselves any IKEMEN GO rating). [server/modules/README.md](../server/modules/README.md) lists the modules and where they come from.

Every server allows messages of up to 64 KB (Nakama's default, 4 KB, is too small for IKEMEN GO's replay streams); an IKEMEN GO server also enforces both players' rating windows in ranked matchmaking (`matchmaker.rev_precision`).

## From the window

1. **Servers** in the launcher's top bar opens the servers' window.
2. **Add** asks for an id (lowercase letters, digits and dashes, at most 16), the community's name, and its games. The new server gets free ports and new keys.
3. The **Settings** tab holds the rest; **Save** keeps the changes. The fields are described in [Settings](#settings).
4. **Start** starts the server. The **Log** tab shows what Nakama and the database do; the line "The server ... is running" says that players can connect. The first start creates the database, which takes a few seconds.
5. **Stop** stops it. Servers keep running when their window is closed; closing the launcher stops them, after asking.

The other buttons:

- **Use in this launcher** points the launcher's own settings at the server, so that whoever runs the server can play on it too.
- **IKEMEN GO profile...** saves a server profile for IKEMEN GO ([For players](#for-players)).
- **Open folder** opens the server's folder ([The server's folder](#the-servers-folder)).

The bottom of the window names the programs that every server uses: Nakama, and the folder of PostgreSQL's programs. Empty fields mean "found automatically"; the line below them says what was found.

## From the command line

The same servers can be run without a window, for example on a rented server (VPS) without a desktop. Two programs offer the commands:

- `nakama-matchmaker server <command>`, the launcher itself;
- `nakama-matchmaker-server <command>`, a program built alongside it that needs no display and no Qt Widgets. On Windows it is the one to use in a console window, since the launcher is a windowed program there.

Both read and write the same configuration folder as the launcher's window (`--config <folder>` selects another one, before the command).

| Command | Does |
|---|---|
| `list` | the servers, their games and ports |
| `add <id> --kinds <kinds> [options]` | a new server; `--kinds` is `ikemen`, `gba`, `ds` (comma separated) or `all` |
| `set <id> [options]` | changes a server (a running server takes the changes at its next start) |
| `remove <id> [--delete-data]` | forgets a server; `--delete-data` also deletes its folder, database included (a new server cannot reuse a kept folder: its database has the old server's password) |
| `show <id> [--secrets]` | a server's settings and what players enter; `--secrets` adds the console's password and the other keys |
| `run <id>... \| --all` | runs servers until Ctrl+C or SIGTERM, printing their logs |
| `ikemen-profile <id> <file>` | writes an IKEMEN GO server profile (`--game`, `--build`, `--host`, `--region`, `--stun`) |
| `programs [--nakama <file>] [--postgres <folder>] [--modules <folder>]` | the programs every server uses, as set or found |

`run` stops every server when one of them fails, and then exits with status 1, so that a service manager can restart them; Ctrl+C or SIGTERM stops them in order (Nakama first, then its database) and exits with status 0, as does SIGHUP unless the program was started with `nohup`. `<command> --help` lists a command's options; the options of `add` and `set` are the [settings](#settings) below.

For example, a DS community on a VPS:

```
nakama-matchmaker-server programs --nakama /opt/nakama/nakama
nakama-matchmaker-server add ds-club --kinds ds --title "DS Club" --public-host ds.example.org
nakama-matchmaker-server show ds-club
nakama-matchmaker-server run ds-club
```

### As a service

On Linux, systemd can start the servers at boot and restart them when they fail. A unit such as this one (an example, not tested) runs them as an ordinary user, here `games`, whose configuration folder is `/home/games/.config/nakama-matchmaker`:

```ini
[Unit]
Description=Nakama Matchmaker community servers
After=network-online.target
Wants=network-online.target

[Service]
User=games
ExecStart=/usr/local/bin/nakama-matchmaker-server run --all
Restart=on-failure
RestartSec=10
# the program stops Nakama, then the database: let it, and give it the time
KillMode=mixed
TimeoutStopSec=60
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=/home/games/.config/nakama-matchmaker

[Install]
WantedBy=multi-user.target
```

## Settings

| Setting (window / command line) | Meaning | Default |
|---|---|---|
| Title / `--title` | The community's name. Nakama Matchmaker shows it to its players; it also names IKEMEN GO profiles. | the id |
| Message / `--message` | A line that Nakama Matchmaker's players see in the chat when they sign in. | none |
| Games / `--kinds` | IKEMEN GO, GBA, DS: see [What each kind of game gets](#what-each-kind-of-game-gets). | |
| Port / `--port` | The TCP port players connect to. Nakama also takes the port before it, for its gRPC API, which players do not use. | 7350, then 7360, 7370, ... |
| Console / `--console-port` | The port of Nakama's web console, for the server's operator. It listens on this computer only; Nakama also takes the port 3 below it. | the port + 1 |
| Listen address / `--bind` | The IP address the server listens on (IPv4 or IPv6). Servers on different addresses can share a port. | every address |
| Public address, port, TLS / `--public-host`, `--public-port`, `--client-tls` | What players enter, when it differs from what the server listens on: its name on the internet, and the port and TLS of a proxy in front of it ([TLS](#tls)). Used by `show`, the window and IKEMEN GO profiles. | this computer, the port, no TLS |
| Server key / `--server-key` | The key players enter with the address, without `:`, spaces or control characters. It is not a password: every player gets it. `--server-key new` makes a new one. | random |
| Database / `--database`, `--database-port` | A database of its own (on 127.0.0.1, at the given port), or the address of an existing one (`user:password@host:port/database`; `managed` goes back to a database of its own). | its own, at 54330, 54331, ... |
| TLS certificate and key / `--tls-cert`, `--tls-key` | For Nakama to use TLS itself ([TLS](#tls)). | none |
| More modules / `--extra-modules` | A folder of Lua modules of your own ([Your own modules](#your-own-modules)). | none |
| `--data-dir` | The server's folder. | `servers/<id>` in the configuration folder |
| `--nakama-arg` | An argument for Nakama, after the generated configuration (repeat it for several). Every user of the computer can see Nakama's arguments. | none |
| `--console-public yes` | Opens the console to other computers. It speaks plain HTTP, so an SSH tunnel is safer ([Security](#security)). | no |

The servers' settings and keys are kept in `servers.json` in the configuration folder, next to the launcher's `settings.ini` and `games.json`.

## For players

Players need the server's address, its port and its server key (`show` prints them, and the window shows them above its tabs), and enter them in their program:

- **Nakama Matchmaker**: Settings, Nakama server and Server key. On a community server it shows the community's name in its status line and the community's message in the chat.
- **mGBA fork**: Tools > Netplay..., Nakama server and Server key.
- **melonDS fork**: System > Multiplayer > Play online (rollback)... > Settings..., server address, port and key.
- **IKEMEN GO**: a server profile, written by `ikemen-profile` or the window's **IKEMEN GO profile...**, which an IKEMEN GO build ships as `data/online/servers.json` so that its players need no settings. The profile names the server's public address (set it first: a profile that names this computer sends every player to their own computer), the game and build whose pools its players match in, and the STUN servers that tell each player its public address.

## The server's folder

Each server has a folder of its own, `servers/<id>` in the configuration folder unless `--data-dir` names another. Nakama Matchmaker writes into it only when it made it, or when it is empty; a marker file (`.nakama-matchmaker-server`) tells its folders apart.

| In the folder | What it is |
|---|---|
| `nakama.yml` | Nakama's configuration, written again at every start from the server's settings |
| `modules/` | the server modules, copied again at every start |
| `postgres/` | the server's database, when it has one of its own |
| `nakama/` | Nakama's own files |
| `server.log`, `server.log.1` | the log of Nakama, the database and Nakama Matchmaker; past 10 MB the log moves to `server.log.1` |
| `server.lock` | taken while a program runs the server, so that two programs never run one server; the window shows a server that another program runs, and leaves it to that program |
| `nakama.pid` | which process runs Nakama, so that the next start can stop a Nakama left running by a program that ended without stopping it |

The accounts, ratings and everything else the server keeps are in the database. To back up a server with a database of its own, stop it and copy its `postgres` folder. A database made by one major version of PostgreSQL needs that version's programs: after installing a newer PostgreSQL, either keep the folder of the older one's programs in the settings, or upgrade the database with `pg_upgrade`.

When the program that ran a server ended without stopping it (a crash, a forced quit), the next start finds what is left, by its process, its program, its command line and its port, and stops it first. On Linux, Nakama and the database end with the program that started them anyway.

In a container, the program can be its first process (no `--init` needed). A `server.lock` written under another computer name, as when a container is recreated with a new hostname, cannot be checked: the start then says so, and the file can be deleted when no program runs the server. Giving the container a fixed hostname avoids that.

## Your own modules

A folder named in **More modules** (`--extra-modules`) adds its `.lua` files to the server's modules. A file with the name of a bundled module replaces it: that is how a community changes, for example, IKEMEN GO's rating settings (the `GAME_CONFIG` table in a copy of `ikemen_matchmaker.lua`). The log names the modules replaced, the files left out (only the `.lua` files directly in the folder are used), and the hooks and RPCs that a module of yours registers again: Nakama keeps only one of each, the one it loads last, in the order of the file names. Keep the folder outside the server's folder, whose `modules` folder is rebuilt at every start.

## Network

Players reach the server on its port, over TCP, for both HTTP and the realtime WebSocket; on a home network, forward that port on the router. Nothing else needs to be reachable: the port before it (Nakama's gRPC API), the console and the database are not for players, and the database and the console listen on this computer only. The games themselves go directly between the players' computers over UDP.

### TLS

To encrypt the players' connections, the simplest is a reverse proxy (Caddy or nginx, with a Let's Encrypt certificate) in front of the server: the server listens on `--bind 127.0.0.1`, the proxy on port 443, and the server's settings say `--public-port 443 --client-tls yes`, so that what players are told matches the proxy. Nakama can also use TLS itself (`--tls-cert`, `--tls-key`); it then needs a restart to take a renewed certificate.

## Security

- **Keys and passwords.** Each server gets its own random keys: the server key, Nakama's HTTP key, session keys, console password and signing key, MFA key and database password. They are in `servers.json` and in the server's `nakama.yml`, which only their owner can read on Linux and macOS, and reach Nakama and the database through those files, the environment and standard input, never through a command line. On Windows, keep the configuration folder inside your user profile (the default), where other users cannot read it.
- **The database.** Nakama connects to a database of its own as `nakama`, a role that owns Nakama's database and has no other rights (in particular, none to run programs on the computer). The database's superuser gets a password that nobody keeps; whoever runs the server can still reach the database with PostgreSQL's single-user mode (`postgres --single -D <folder>/postgres`).
- **The console.** Nakama's web console controls every account and record. It listens on this computer only; from elsewhere, reach it through an SSH tunnel, such as `ssh -N -L 7351:127.0.0.1:7351 you@server` and then http://127.0.0.1:7351, with the user name and password that `show --secrets` prints. Its moderation tools (looking up and banning accounts, deleting messages) are the only ones a server has.
- **What players can do.** The server key is public, so anyone can create accounts on the server, as many as they like, and choose their names. Signed-in players can chat, create IKEMEN GO lobbies and melonDS channels (whose name the first player to open a game's channel chooses), and call the server modules' RPCs. A player's addresses (public and on their network) go to the players they play, to connect their computers. Ratings and results that IKEMEN GO players report are trusted by IKEMEN GO's modules, not checked: on a public server, treat ratings with that in mind.
- **Statistics sent by Nakama.** Nakama sends anonymous usage statistics to Heroic Labs when it starts and stops. Start Nakama Matchmaker with the environment variable `NAKAMA_TELEMETRY=0` to turn them off (it passes its environment on to Nakama; in the systemd unit above, add `Environment=NAKAMA_TELEMETRY=0`).

## Tested, and not

Tested on Ubuntu 24.04 with Nakama 3.41.0 and PostgreSQL 16, with every program on one computer; the window with Qt 6.4, the command line with Qt 6.4 and Qt 5.15:

- servers for each kind, and for several kinds, started from the window and from both command-line programs, several at once; each server answered with its own community name and kinds, and offered only its kinds' RPCs;
- on a GBA and IKEMEN GO server: two and three `netplay-online-test` players matched, connected and chatted; two launchers with `--auto` matched and played an mGBA fork match (`mgba-netplay-online-test`) to identical per-frame checksums;
- on an IKEMEN GO server: the IKEMEN GO fork's 34 live Nakama tests (its client code against the server: sign-in and names, quick and ranked matchmaking with ratings, lobbies and tournaments, sessions and direct connections, watching matches) passed, with the Nakama settings those tests ask for given through `--nakama-arg`;
- on a DS server: clients speaking the melonDS fork's channel protocol joined a channel, were paired by quick match, and had their connection signals relayed to their partner only;
- stopping with Ctrl+C, SIGTERM and the window; recovering a Nakama and a database left running; a stop while the database is being created; refusing folders that are not the server's;
- the database role's rights, and the refusal of players' writes to the server's records.

Not tested: Windows and macOS (their code paths are written but have not run), the IKEMEN GO game and the melonDS fork's online window themselves against a community server, TLS, and players on different networks.
