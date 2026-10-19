--!strict
-- Shared action helper used by 3 scripts, translated by tools/luau/stb2luau.

local vm = require("@lib/vm")
local helper_bac4ec65 = require("@lib/action/helper_bac4ec65")

--- Helper function.
local function helper_1f0fc9e1()
	local var0: s32 = 0
	local var1: f32 = 0
	action.SetMos("よけのみ(バク宙）", -1, 6)
	action.SetMuteki(20)
	while true do
		vm.wait()
		var1 = action.NowMosWait() or var1
		if var1 > 0.18 and var1 < 0.8 then
			action.SetBlowMove(4.0, 0.0, 1, 180)
		end
		var0 = action.CheckMosEnd() or var0
		if var0 ~= 0 then
			break
		end
	end
	helper_bac4ec65(6)
	action.ProgSet(0)
end

return helper_1f0fc9e1
