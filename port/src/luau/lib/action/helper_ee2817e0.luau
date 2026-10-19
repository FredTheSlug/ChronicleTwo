--!strict
-- Shared action helper used by 24 scripts, translated by tools/luau/stb2luau.

local vm = require("@lib/vm")

--- Helper function.
local function helper_ee2817e0()
	local var0: s32 = 0
	while true do
		vm.wait()
		var0 = action.CheckMosEnd() or var0
		if var0 ~= 0 then
			return
		end
	end
end

return helper_ee2817e0
