-- SPDX-License-Identifier: MIT
-- Nakama Matchmaker's community module, installed on every server that Nakama Matchmaker
-- runs (docs/hosting.md).
--
-- RPC
--   community_info  {} -> {version = 1, title, message, kinds = ["ikemen", "mgba", "melonds"]}
--                   which community the server is for. The values come from Nakama's
--                   runtime.env, which Nakama Matchmaker writes into the server's
--                   configuration: NMM_TITLE, NMM_MESSAGE and NMM_KINDS (comma separated).
--
-- Storage
--   Nakama lets every signed-in player write and delete records of their own through its
--   storage API, which none of the games' clients uses. The server modules keep their
--   records (IKEMEN GO's ratings and results, melonDS's channels) in collections that
--   only the server may change, so players' writes to those collections are refused:
--   without this, a player could give themselves any IKEMEN GO rating.

local nk = require("nakama")

local function split(text)
    local out = {}
    for item in string.gmatch(text or "", "[^,]+") do
        table.insert(out, item)
    end
    return out
end

local function community_info(context, payload)
    local env = context.env or {}
    return nk.json_encode({
        version = 1,
        title = env.NMM_TITLE or "",
        message = env.NMM_MESSAGE or "",
        kinds = split(env.NMM_KINDS),
    })
end

local SERVER_COLLECTIONS = { "ikemen_", "melonds_", "nmm_" }

local function server_collection(collection)
    if type(collection) ~= "string" then
        return false
    end
    for _, prefix in ipairs(SERVER_COLLECTIONS) do
        if collection:sub(1, #prefix) == prefix then
            return true
        end
    end
    return false
end

local function guard(field)
    return function(context, payload)
        for _, object in ipairs((payload or {})[field] or {}) do
            if server_collection(object.collection) then
                error({ "That collection belongs to the server", 7 })   -- PERMISSION_DENIED
            end
        end
        return payload
    end
end

nk.register_rpc(community_info, "community_info")
nk.register_req_before(guard("objects"), "WriteStorageObjects")
nk.register_req_before(guard("object_ids"), "DeleteStorageObjects")
nk.logger_info("Nakama Matchmaker community module loaded")
