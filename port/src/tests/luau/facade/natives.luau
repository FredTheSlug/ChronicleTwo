return {
    [100] = function()
        local v = action.ProgGet()
        action.ProgSet(v)
    end,
    [101] = function()
        action.ProgSet(if action.GetAttkType() == nil then 1 else 0)
    end,
    [102] = function()
        action.SetPallet(4, "テスト")
    end,
    [103] = function()
        action.SetMoveSpeed(1.5)
    end,
    [104] = function()
        action.Ext(200, 1, 2.5, "x", true)
    end,
    [105] = function()
        action.ProgSet(action.InitScript_1())
    end,
    [106] = function()
        action.InitScript()
    end,
    [107] = function()
        print("a", 1, 2.5, true, nil, "b\n")
    end,
}
