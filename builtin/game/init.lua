
local scriptpath = core.get_builtin_path()..DIR_DELIM
local commonpath = scriptpath.."common"..DIR_DELIM
local gamepath = scriptpath.."game"..DIR_DELIM

-- Shared between builtin files, but
-- not exposed to outer context
local builtin_shared = {}

dofile(commonpath.."vector.lua")

dofile(gamepath.."constants.lua")
assert(loadfile(gamepath.."item.lua"))(builtin_shared)
dofile(gamepath.."register.lua")

if core.settings:get_bool("profiler.load") then
	profiler = dofile(scriptpath.."profiler"..DIR_DELIM.."init.lua")
end

dofile(gamepath.."item_entity.lua")
dofile(gamepath.."deprecated.lua")
dofile(gamepath.."misc.lua")
dofile(gamepath.."privileges.lua")
if core.setting_getbool("auth_kv") then
	dofile(gamepath.."fm_auth.lua")
else
	dofile(gamepath.."auth.lua")
end
dofile(gamepath.."stat.lua")
dofile(gamepath.."chatcommands.lua")
-- internal is better
-- dofile(gamepath.."static_spawn.lua")
dofile(gamepath.."detached_inventory.lua")
assert(loadfile(gamepath.."falling.lua"))(builtin_shared)
dofile(gamepath.."features.lua")
dofile(gamepath.."voxelarea.lua")
dofile(gamepath.."forceloading.lua")
dofile(gamepath.."statbars.lua")

if core.setting_getbool("mod_debugging") then
	dofile(gamepath.."mod_debugging.lua")
end

profiler = nil
