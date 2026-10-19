--!strict
-- Shared action helper used by 7 scripts, translated by tools/luau/stb2luau.

--- Helper function.
local function helper_6322acfa()
	local var0: s32 = 0
	local prog: s32 = 0
	local var2: f32 = 0
	prog = action.ProgGet() or prog
	if prog == 1000 then
		action.SetMos("アイテム投げ", -1, 6)
		action.ProgSet(prog + 1)
	end
	if prog == 1001 then
		var2 = action.NowMosWait() or var2
		if var2 >= 0.45 and var2 < 0.5 then
			action.ThrowHandObject()
		end
		var0 = action.CheckMosEnd() or var0
		if var0 ~= 0 then
			action.ProgSet(0)
		end
	end
end

return helper_6322acfa
