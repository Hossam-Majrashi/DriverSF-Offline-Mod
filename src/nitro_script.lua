-- Executed only through the game's own protected Lua API, on a Lua callback.
-- The C predicate reads the current switch; no saved/global cheat flag is set.
local enabled = ...
assert(type(enabled) == "function")
local names = {"getAbilityHoldValue", "getAbilityCostValue", "getUseDurationBoost"}
return function()
  local a = rawget(_G, "abilities")
  if type(a) ~= "table" then return false end
  local records = rawget(a, "__dsfNitroV72")
  local complete = type(records) == "table"
  for _, name in ipairs(names) do
    local current = rawget(a, name)
    if type(current) ~= "function" then return false end
    local record = complete and records[name]
    if not record or current ~= record.wrapper then complete = false end
  end
  if complete then return true end -- no new wrappers/closures on ordinary input
  if type(records) ~= "table" then records = {} end
  local function allowed()
    local g = rawget(_G, "gameStatus")
    local manager = rawget(_G, "localPlayerManager")
    local p = manager and manager.players and manager.players[0]
    return enabled() and g and not g.onlineSession and not g.splitscreenSession
      and p and not p.inZap and p.currentVehicle ~= nil
  end
  for _, name in ipairs(names) do
    local current = rawget(a, name)
    local record = records[name]
    if not record or current ~= record.wrapper then
      local original = current
      local wrapper
      if name == "getUseDurationBoost" then
        wrapper = function(...)
          if allowed() then return false end
          return original(...)
        end
      else
        wrapper = function(ability, ...)
          if ability == "nitro" and allowed() then return 0 end
          return original(ability, ...)
        end
      end
      records[name] = {original = original, wrapper = wrapper}
      rawset(a, name, wrapper)
    end
  end
  rawset(a, "__dsfNitroV72", records)
  return true
end
