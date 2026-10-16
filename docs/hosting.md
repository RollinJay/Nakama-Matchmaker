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
| IKEMEN GO (`ikemen`) | the IKEMEN GO Nakama fork's online menus | IKEMEN GO's modules | quick and ranked matchmaking, lobbies, ratings, online names, and the ranked sets that the server's moderators decide ([IKEMEN GO's ranked sets](#ikemen-gos-ranked-sets)); the levels that players see for their ratings, and a leaderboard per game ([IKEMEN GO's ranked levels](#ikemen-gos-ranked-levels)) |
| GBA (`gba`, or `mgba`) | the mGBA fork's netplay window, or Nakama Matchmaker | IKEMEN GO's modules | two-player matches run as IKEMEN GO's sessions; three and four players use matches that Nakama relays |
| DS (`ds`, or `melonds`) | the melonDS fork's online window (opened by itself, or by Nakama Matchmaker's **Play online**), and Nakama Matchmaker's **Find a match** | melonDS's channel module | the game channels: who is there, chat and challenges. Quick match and Nakama Matchmaker's matches use Nakama's matchmaker and need no module; on a server for DS games alone, Nakama relays their matches |

Every server also gets Nakama Matchmaker's community module, which tells clients the community's name and message, and stops players from writing into the records that the server modules keep (without it, a player could write an IKEMEN GO rating of their own choosing). [server/modules/README.md](../server/modules/README.md) lists the modules and where they come from.

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

A server with IKEMEN GO also has a **Ranked levels** group in its Settings tab ([IKEMEN GO's ranked levels](#ikemen-gos-ranked-levels)), and a **Ranked** tab, for its moderators ([IKEMEN GO's ranked sets](#ikemen-gos-ranked-sets)).

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
| `ranked <id> [--status <status>] [--match <match id>]` | a running server's IKEMEN GO ranked sets: the disputed ones, or those of `--status` (`open`, `settled`, `void` or `all`), newest first; `--limit` and `--cursor` page through them, `--json` prints the server's answer |
| `ranked-replay <id> <match id> <file or folder> [--player <user>]` | saves a player's replay of a ranked set: to a file whose name ends with `.replay`, or into a folder (one that exists, or a name that ends with `/`, which is made) under the name the server gives it; it replaces an existing file only with `--force` |
| `ranked-settle <id> <match id> (--winner <user> \| --void) [--note <text>]` | decides a ranked set: its winner, or no result |

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
| Ranked levels / `--level-start`, `--level-top`, `--level-label`, `--game-level` | What IKEMEN GO's players see for their ranked ratings, for every game and for games of their own ([IKEMEN GO's ranked levels](#ikemen-gos-ranked-levels)). | the Elo ratings themselves, labelled "Rating" |

The servers' settings and keys are kept in `servers.json` in the configuration folder, next to the launcher's `settings.ini` and `games.json`.

## For players

Players need the server's address, its port and its server key (`show` prints them, and the window shows them above its tabs), and enter them in their program:

- **Nakama Matchmaker**: Settings, Nakama server and Server key. On a community server it shows the community's name in its status line and the community's message in the chat. For an IKEMEN GO game, its **Leaderboard** lists the game's ranked players with their levels ([IKEMEN GO's ranked levels](#ikemen-gos-ranked-levels)).
- **mGBA fork**: Tools > Netplay..., Nakama server and Server key.
- **melonDS fork**: System > Multiplayer > Play online (rollback)... > Settings..., server address, port and key.
- **IKEMEN GO**: a server profile, written by `ikemen-profile` or the window's **IKEMEN GO profile...**, which an IKEMEN GO build ships as `data/online/servers.json` so that its players need no settings. The profile names the server's public address (set it first: a profile that names this computer sends every player to their own computer), the game and build whose pools its players match in, and the STUN servers that tell each player its public address.

## IKEMEN GO's ranked sets

IKEMEN GO's ranked matches come in *sets*. A ranked session, which the matchmaker makes for two players who asked for a ranked match, plays best-of-N sets (3 matches each by default), and the players' ratings change once per set, not per match. IKEMEN GO's modules record each set when the session starts it: the game and build, the set's length, the two players, and its status, `open`. When a set has been decided, each player's game claims its winner:

- the server takes a claim only for a set it recorded, from one of its two players, while the set is open; it refuses claims for other matches, from other players, and for unranked sessions and lobbies;
- two claims that name the same winner *settle* the set: both players' Elo ratings change, once;
- claims that differ make the set *disputed*, and so does a second claim that did not come within 15 minutes of the set's end. A set that nobody claimed becomes *void* (no result), unless a player left it after its matches had started: it is disputed then. A player who left the session before the set was decided is noted on the set.

The server also keeps both players' replays of each ranked set: in a ranked session both games publish their replay stream, and the session stores each player's stream when the set is decided, when the player leaves, or when the session ends, marked complete or incomplete. A replay is kept for 30 days, at most 16 MiB per player and set; expired replays are removed as the server stores new ones and lists the sets. Nothing replays a set automatically: a *moderator* watches the replay of a disputed set and decides it.

The moderators are whoever runs the server. IKEMEN GO's modules accept the moderation calls (the RPCs `ikemen_ranked_list`, `ikemen_ranked_replay` and `ikemen_ranked_settle`) only with the server's HTTP key, never with a player's session, and Nakama Matchmaker keeps that key in `servers.json`. It makes the calls to the running server over HTTP on this computer, at the server's listen address (or 127.0.0.1) and port and never through a proxy, whether the window runs the server or another program of this computer does (the command line, a service) with the same configuration folder; for a server that another program runs, it reads the port, address and key that the server started with from the server's `nakama.yml`, since changes to its settings wait for its next start:

- the **Ranked** tab of the servers' window lists the sets, the disputed ones by default (a menu shows the open, settled or void ones, or all), with their players, claims and replays. **Export replay...** saves a player's replay of the chosen set; **Settle...** decides its winner; **Void...** decides that it has no result. Settle and Void take a note, which stays on the set's record.
- the commands `ranked`, `ranked-replay` and `ranked-settle` ([From the command line](#from-the-command-line)) do the same:

```
nakama-matchmaker-server ranked ik-club
nakama-matchmaker-server ranked-replay ik-club <match id> ~/replays/
nakama-matchmaker-server ranked-settle ik-club <match id> --winner Ryu --note "the replay shows Ryu winning"
```

A set's *match id* is the session's match id for its first set, and `<match id>#<n>` for its n-th set (when both players chose to play another set); the list shows it. `--winner` and `--player` take a player's user id or username. Settling a set changes the ratings as two agreeing claims would, once; voiding it changes nothing. A set that is settled or void already cannot be decided again.

To watch a replay, put the exported file in the `save/replays` folder of an IKEMEN GO with the set's game and build (the list shows them) and its characters and stage, and open WATCH MODE > REPLAY, or run `Ikemen_GO -replay <file>`. Both players' replays of a set hold the same inputs; the second one is there for a set whose first replay is incomplete. The IKEMEN GO fork's `nakama/README.md` ("Ranked sets") describes the records, the claims and the replays in full.

The claim window and the replays' retention are settings of IKEMEN GO's modules, Nakama *runtime environment* values, which a server takes as Nakama arguments (`--nakama-arg`, on the command line). For example, to keep replays for 7 days and wait 30 minutes for a second claim:

```
nakama-matchmaker-server set ik-club --nakama-arg=--runtime.env --nakama-arg=IKEMEN_RANKED_REPLAY_DAYS=7 \
  --nakama-arg=--runtime.env --nakama-arg=IKEMEN_RANKED_CLAIM_MINUTES=30
```

| Value | Default | Meaning |
|---|---|---|
| `IKEMEN_RANKED_CLAIM_MINUTES` | 15 | Minutes after a set's end within which the second claim must come. |
| `IKEMEN_RANKED_REPLAY_DAYS` | 30 | Days a replay is kept; 0 keeps none. |
| `IKEMEN_RANKED_REPLAY_MAX_BYTES` | 16777216 | The most bytes kept of each player's replay of a set. |

`--nakama-arg` replaces the server's earlier Nakama arguments, so give them all in one `set`; a running server takes them at its next start.

## IKEMEN GO's ranked levels

Each IKEMEN GO player has an Elo rating for each game: it starts at 1000, the matchmaker pairs ranked players by it, and each ranked set that settles changes it, by 16 between two players of the same rating ([IKEMEN GO's ranked sets](#ikemen-gos-ranked-sets)). What players see is a *level* computed from that rating, which the server's creator sets, for the whole server and for games of their own. Without settings the level is the rating itself, labelled "Rating". With them it can be, for example, a "Power level" that starts at 9,001 and reaches the billions:

```text
level = start × (top ÷ start) ^ ((Elo − 1000) ÷ 1000)
```

- **start** is the level of a new player, whose rating is 1000;
- **top** is the level of a player 1000 Elo above a new one, which in a large community is the very top. Each step of Elo multiplies the level by the same factor, so a level stays positive however low the rating;
- levels are whole numbers, at least 1 and at most 9,000,000,000,000,000 (up to there, the modules' numbers hold every whole number exactly).

For a Hyper Dragon Ball Z community, with start 9001 and top 1,000,000,000:

| Elo | Power level |
|---|---|
| 600 | 86 |
| 984 (a new player who lost a first set to another) | 7,474 |
| 1000 (a new player) | 9,001 |
| 1016 (a new player who won a first set against another) | 10,840 |
| 1100 | 28,765 |
| 1500 | 3,000,167 |
| 2000 | 1,000,000,000 |
| 2200 | 10,212,729,937 |

```
nakama-matchmaker-server set ik-club --level-start 9001 --level-top 1000000000 --level-label "Power level"
```

| Setting (window / command line) | Meaning |
|---|---|
| Start / `--level-start` | The level of a new player: a whole number of at least 1, written `9001`, `1000000000` or `1e9`. |
| Top / `--level-top` | The level of a player 1000 Elo above a new one: more than the start, and at most 9000000000000000. Start and top go together: give both, or neither (`none` on the command line) for the Elo ratings themselves. |
| Label / `--level-label` | What players see the number called, such as "Power level": 1 to 32 characters, none of them a control character. Empty: "Rating". |
| Games with levels of their own / `--game-level <game>=<start>,<top>[,<label>]` | A game's own start and top, and its label (the rest of the text, commas included). The game is named by the game id that IKEMEN GO sends: the `game` of its server profile, as in Nakama Matchmaker's game list (1 to 114 bytes, without spaces or control characters). What an entry leaves out is the default's: `<game>=,,<label>` only names the game's number, and `<game>=` removes the game's entry. Repeat the option for several games. |

In the window, the servers' **Settings** tab has these in its **Ranked levels** group, for a server with IKEMEN GO: Start, Top and Label, a line under them that spells out what they mean ("Power level 9,001 for a new player → 1,000,000,000 at +1000 Elo ..."), and a table of the games with levels of their own (**Add a game**, **Remove the game**; an empty cell is the default's). `show` prints them.

The levels are a view of the ratings: they change no rating, nor whom the matchmaker pairs. The settings can therefore change at any time, and every player's level changes with them at once, from the server's next start. Nakama Matchmaker hands them to IKEMEN GO's modules as `IKEMEN_LEVELS`, a Nakama runtime environment value in the server's `nakama.yml`, which the modules read when the server starts; the log then says which levels are in effect:

```text
ikemen levels: levels are start 9001, top 1000000000, labelled "Power level"
```

`IKEMEN_LEVELS` given as a Nakama argument as well (`--nakama-arg=--runtime.env --nakama-arg=IKEMEN_LEVELS=...`) would replace these settings, since Nakama prefers its arguments to its configuration file, so a server cannot have both.

Each game also has a leaderboard on the server, which ranks its players by rating. When a ranked set settles, by its players' claims or by a moderator's settlement, the server writes both players' entries on it, and sends both players a Nakama notification with their level before and after the set; players cannot write to the leaderboard themselves. A player whose rating dates from before the leaderboards appears after their next set, or when their game next asks for their rating, as IKEMEN GO does when the player looks for a ranked match.

Players see their level in IKEMEN GO while it looks for a ranked match. After the last match of a ranked set, a screen shows for four seconds their level before and after the set, or that the set is disputed and a moderator will decide, when the server's notification of the result comes while that screen shows. A result that comes later, such as a moderator's settlement of a disputed set, does not show in the game: IKEMEN GO only prints it, to its standard output, and a player who was not connected has it in their Nakama notifications, which IKEMEN GO does not read (the IKEMEN GO fork's `nakama/README.md`, "Online sessions"). Nakama Matchmaker's **Leaderboard** for an IKEMEN GO game lists the game's players by rank, with their names, levels and sets won and lost, page by page, and the player's own place. The moderators' list of ranked sets (the **Ranked** tab, `ranked`) shows each player's level now. The IKEMEN GO fork's `nakama/README.md` ("Levels") describes the levels, the leaderboards and the notifications in full.

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

A folder named in **More modules** (`--extra-modules`) adds its `.lua` files to the server's modules. A file with the name of a bundled module replaces it: that is how a community changes, for example, IKEMEN GO's Elo settings (the `GAME_CONFIG` table in a copy of `ikemen_rating.lua`: the rating a new player starts with, the K-factor and the scale) or its ranked matchmaking windows (`GAME_CONFIG` in a copy of `ikemen_matchmaker.lua`). The levels players see need no module of their own ([IKEMEN GO's ranked levels](#ikemen-gos-ranked-levels)). The log names the modules replaced, the files left out (only the `.lua` files directly in the folder are used), and the hooks and RPCs that a module of yours registers again: Nakama keeps only one of each, the one it loads last, in the order of the file names. Keep the folder outside the server's folder, whose `modules` folder is rebuilt at every start.

## Network

Players reach the server on its port, over TCP, for both HTTP and the realtime WebSocket; on a home network, forward that port on the router. Nothing else needs to be reachable: the port before it (Nakama's gRPC API), the console and the database are not for players, and the database and the console listen on this computer only. The games themselves go directly between the players' computers over UDP.

### TLS

To encrypt the players' connections, the simplest is a reverse proxy (Caddy or nginx, with a Let's Encrypt certificate) in front of the server: the server listens on `--bind 127.0.0.1`, the proxy on port 443, and the server's settings say `--public-port 443 --client-tls yes`, so that what players are told matches the proxy. Nakama can also use TLS itself (`--tls-cert`, `--tls-key`); it then needs a restart to take a renewed certificate.

## Security

- **Keys and passwords.** Each server gets its own random keys: the server key, Nakama's HTTP key, session keys, console password and signing key, MFA key and database password. They are in `servers.json` and in the server's `nakama.yml`, which only their owner can read on Linux and macOS, and reach Nakama and the database through those files, the environment and standard input, never through a command line. The ranked sets' moderation sends the HTTP key to the server within this computer, in its HTTP requests, which go to the server directly, never through a proxy. On Windows, keep the configuration folder inside your user profile (the default), where other users cannot read it.
- **The database.** Nakama connects to a database of its own as `nakama`, a role that owns Nakama's database and has no other rights (in particular, none to run programs on the computer). The database's superuser gets a password that nobody keeps; whoever runs the server can still reach the database with PostgreSQL's single-user mode (`postgres --single -D <folder>/postgres`).
- **The console.** Nakama's web console controls every account and record. It listens on this computer only; from elsewhere, reach it through an SSH tunnel, such as `ssh -N -L 7351:127.0.0.1:7351 you@server` and then http://127.0.0.1:7351, with the user name and password that `show --secrets` prints. Its tools (looking up and banning accounts, deleting messages) are a server's moderation tools, together with the moderation of IKEMEN GO's ranked sets ([IKEMEN GO's ranked sets](#ikemen-gos-ranked-sets)).
- **What players can do.** The server key is public, so anyone can create accounts on the server, as many as they like, and choose their names. Signed-in players can chat, create IKEMEN GO lobbies and melonDS channels (whose name the first player to open a game's channel chooses), and call the server modules' RPCs. A player's addresses (public and on their network) go to the players they play, to connect their computers. IKEMEN GO's ranked results are the players' claims: the server takes a claim only for a ranked set it recorded and only from its players, and changes the ratings only when both claims agree, or a moderator decides. Two players who agree on a false result are believed, and nothing replays the matches to check a result.
- **Statistics sent by Nakama.** Nakama sends anonymous usage statistics to Heroic Labs when it starts and stops. Start Nakama Matchmaker with the environment variable `NAKAMA_TELEMETRY=0` to turn them off (it passes its environment on to Nakama; in the systemd unit above, add `Environment=NAKAMA_TELEMETRY=0`).

## Tested, and not

Tested on Ubuntu 24.04 with Nakama 3.41.0 and PostgreSQL 16, with every program on one computer; the window with Qt 6.4, the command line with Qt 6.4 and Qt 5.15:

- servers for each kind, and for several kinds, started from the window and from both command-line programs, several at once; each server answered with its own community name and kinds, and offered only its kinds' RPCs;
- on GBA servers (one for GBA alone, one for every kind): two and three `netplay-online-test` players matched, connected and chatted; two launchers with `--auto` matched and played an mGBA fork match (`mgba-netplay-online-test`) to identical per-frame checksums;
- on a server with IKEMEN GO: the IKEMEN GO fork's live Nakama tests (its client code against the server: sign-in and names, quick and ranked matchmaking with ratings, the ranked sets' claims, disputes, kept replays and moderator RPCs, the levels, leaderboards and notifications, lobbies and tournaments, sessions and direct connections, watching matches), with the Nakama settings those tests ask for given through `--nakama-arg`, and the level settings of their "example" mode given with `--level-start`, `--level-top`, `--level-label` and `--game-level`: 56 passed; three were skipped (two need other settings, one is a helper); the two that always connect to port 7350 were not run, since the server had another port;
- IKEMEN GO's ranked levels: the settings' round trip on the command line (`add`, `set` and `show`, with both command-line programs, games added, changed and removed, and 21 values that are not valid refused, leaving `servers.json` as it was); the window's Ranked levels group, which saved the settings and a table of games, refused a top below the start, and showed the settings again after the launcher restarted; servers started from the window and from the command line logged the levels they were given; ranked sets settled through the IKEMEN GO modules' RPCs, and by `ranked-settle`, put their players on the games' leaderboards, which the launcher's Leaderboard showed with Qt 6 and Qt 5: their ranks, names, levels (up to 1,629,606,392 for a game whose levels start at a billion) and sets, page by page, and the player's own place, also from beyond the first page; and again, on a server for every kind with ranked levels set, the two launchers' mGBA fork match and their melonDS fork hand-off, and the moderation commands;
- the moderation of IKEMEN GO's ranked sets, with sets that the IKEMEN GO fork's test helper made disputed: `ranked` listed them by status and page by page, `ranked-replay` saved both players' replays, `ranked-settle` settled a set (its winner named by username) and voided another, with notes; the window's Ranked tab listed, exported, settled and voided while another program ran the server, and listed sets by status while the window ran it; the moderator RPCs refused a player's session and a wrong HTTP key; extra runtime values given with `--nakama-arg=--runtime.env` reached the modules beside the server's own;
- on a DS server, and on one for every kind: clients speaking the melonDS fork's channel protocol joined a channel, were paired by the channel's quick match, and had their connection signals relayed to their partner only; two launchers with `--auto` (Find a match) started two melonDS fork windows, which played with rollback (every desync check passing) until one was closed, and a launcher with `--auto` and a melonDS window's quick match met and played the same way; on the DS server, the launcher window's Play online opened melonDS's online window signed in, in the game's channel;
- stopping with Ctrl+C, SIGTERM and the window; recovering a Nakama and a database left running; a stop while the database is being created; refusing folders that are not the server's;
- the database role's rights, and the refusal of players' writes to the server's records (the ranked sets' records included).

Not tested: Windows and macOS (their code paths are written but have not run), the IKEMEN GO game itself against a community server (so no community server's set came from a real game, and none of its replays was watched; the IKEMEN GO fork played its own exported replays), a DS game's own wireless play (the DS tests used a small homebrew ROM), TLS, and players on different networks.
