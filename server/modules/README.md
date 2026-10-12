# Server modules

The Lua modules that Nakama Matchmaker installs on the community servers it runs
([docs/hosting.md](../../docs/hosting.md)). At every start of a server, the sets its games
need are copied side by side into the server's `modules` folder, which is Nakama's runtime
path:

| Folder | Installed for | What it is | Source | License |
|---|---|---|---|---|
| `community/` | every server | `community.lua`: the `community_info` RPC, and the refusal of players' writes to the server's own storage collections | this repository | MIT |
| `ikemen/` | IKEMEN GO and GBA servers | IKEMEN GO's matchmaking, sessions, lobbies, ratings and online names; GBA games' two-player matches are its sessions | [RollinJay/IkemenGo-Nakama](https://github.com/RollinJay/IkemenGo-Nakama) `nakama/modules/`, commit 67f1872 | MIT (`ikemen/LICENSE`) |
| `melonds/` | DS servers | `melonds_channel.lua`: the melonDS fork's game channels (chat, challenges, quick match) | [RollinJay/melonDS-test](https://github.com/RollinJay/melonDS-test) `src/net/rollback/server/` (the rollback netplay commit) | GPL 3.0 or later (`melonds/LICENSE`), as melonDS |

The `ikemen/` and `melonds/` files are copies, unchanged, of their sources. When those
change, `tools/sync-server-modules.sh` copies them again from checkouts of the two forks.

`melonds_channel.lua` keeps melonDS's license, the GPL: it is a separate file that Nakama
loads, not part of Nakama Matchmaker's program, which stays under the MIT license.
