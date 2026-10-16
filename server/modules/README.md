# Server modules

The Lua modules that Nakama Matchmaker installs on the community servers it runs
([docs/hosting.md](../../docs/hosting.md)). At every start of a server, the sets its games
need are copied side by side into the server's `modules` folder, which is Nakama's runtime
path:

| Folder | Installed for | What it is | Source | License |
|---|---|---|---|---|
| `community/` | every server | `community.lua`: the `community_info` RPC, and the refusal of players' writes to the server's own storage collections | this repository | MIT |
| `ikemen/` | IKEMEN GO and GBA servers | IKEMEN GO's matchmaking, sessions, lobbies, ratings and online names, its ranked sets with their replays and moderator RPCs, and the levels players see for their ratings, with a leaderboard per game; GBA games' two-player matches are its sessions | [RollinJay/IkemenGo-Nakama](https://github.com/RollinJay/IkemenGo-Nakama) `nakama/modules/`, commit 4f1f4aa | MIT (`ikemen/LICENSE`) |
| `melonds/` | DS servers | `melonds_channel.lua`: the melonDS fork's game channels (who is there, chat, challenges) | [RollinJay/melonDS-test](https://github.com/RollinJay/melonDS-test) `src/net/rollback/server/`, commit 1a627f0 | MIT (`melonds/LICENSE`) |

The `ikemen/` and `melonds/` files are copies, unchanged, of their sources. When those
change, `tools/sync-server-modules.sh` copies them again from checkouts of the two forks.

`melonds_channel.lua` comes from the melonDS fork but is under the MIT license, not
melonDS's GPL: it contains no melonDS code, and the fork's copy carries the same MIT notice.
So every file in this repository is under the MIT license.
