--!strict
-- dungeon/act_script/f207a.stb: action script, translated from the NTSC-timed bytecode by tools/luau/stb2luau.

local vm = require("@lib/vm")
local helper_4ccf4adc = require("@lib/action/helper_4ccf4adc")
local helper_72d1daf9 = require("@lib/action/helper_72d1daf9")
local helper_77acd540 = require("@lib/action/helper_77acd540")
local helper_9f2e9866 = require("@lib/action/helper_9f2e9866")
local helper_cd42357d = require("@lib/action/helper_cd42357d")

local script = {}

--- Runs when the script is loaded, before any program.
function script.setup() end

--- Sets the character up when its script is initialised.
function script.init()
	action.SetDmg2("dcol00", "", "モンスター攻撃", 4.5, 1.0, "攻撃１", 0.38, 0.6)
	action.SetDmg2("dcol01", "", "モンスター攻撃", 4.5, 1.2, "攻撃２", 0.4, 0.7)
end

--- Runs the character's normal behaviour.
function script.loop()
	local prog: s32 = 0
	local var3: s32 = 0
	local var4: s32 = 0
	action.ProgSet(0)
	while true do
		prog = action.ProgGet() or prog
		if prog >= 100 and prog < 199 then
			action.SetTrgAngle(40.0, 5.0)
			helper_cd42357d()
		end
		if prog >= 200 and prog < 299 then
			action.SetTrgAngle(40.0, 5.0)
			helper_4ccf4adc()
		end
		prog = action.ProgGet() or prog
		if prog == 0 then
			action.RunMainMove()
			var3 = action.GetBtn_2(50) or var3
			var4 = action.GetBtn_2(51) or var4
			if var3 ~= 0 then
				prog = 100
			end
			if var4 ~= 0 then
				prog = 200
			end
			action.ProgSet(prog)
		end
		vm.wait()
	end
end

--- Reacts to a small hit.
script.on_small_damage = helper_9f2e9866

--- Holds the character in place.
script.on_hold = helper_9f2e9866

--- Reacts to a large hit.
script.on_large_damage = helper_72d1daf9

--- Follows up a melee hit that the character has landed.
-- Defined empty, so the engine runs this instead of its default.
function script.on_melee_hit() end

--- Runs the character's death.
script.on_death = helper_77acd540

return script
