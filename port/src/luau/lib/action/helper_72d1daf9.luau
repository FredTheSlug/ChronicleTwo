--!strict
-- Shared action helper used by 17 scripts, translated by tools/luau/stb2luau.

local helper_bac4ec65 = require("@lib/action/helper_bac4ec65")
local helper_ee2817e0 = require("@lib/action/helper_ee2817e0")

--- Helper function.
local function helper_72d1daf9()
	action.BlowStart(5.0, 0.0, 10)
	action.SetBlowAngle()
	action.ReleaseObj(0)
	action.SetMos("吹っ飛び", -1, 6)
	helper_ee2817e0()
	helper_bac4ec65(8)
	action.SetMos("起き上がり", -1, 6)
	helper_ee2817e0()
	helper_bac4ec65(2)
	action.ProgSet(0)
end

return helper_72d1daf9
