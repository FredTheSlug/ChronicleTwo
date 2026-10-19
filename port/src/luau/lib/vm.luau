--!strict
-- Helper library for the script machine, loaded by transpiled scripts.
-- Scripts load it with require("@lib/vm").
local vm = {}

-- Truncates toward zero like C's float-to-integer conversion; NaN yields zero.
local function truncate(x: number): number
    if x ~= x then
        return 0
    end
    if x >= 0 then
        return math.floor(x)
    end
    return math.ceil(x)
end

-- Suspends the script until the engine resumes it on the next frame.
function vm.wait(): ()
    coroutine.yield()
end

-- Wraps an integral value to a signed 32-bit integer.
function vm.i32(x: number): number
    local v = x % 4294967296
    if v >= 2147483648 then
        v -= 4294967296
    end
    return v
end

-- Converts a float to an integer by truncating toward zero, wrapped to 32 bits.
function vm.ftoi(x: number): number
    return vm.i32(truncate(x))
end

-- Divides two integers with C semantics, truncating toward zero and wrapping to 32 bits.
function vm.idiv(a: number, b: number): number
    if b == 0 then
        error("division by zero", 2)
    end
    return vm.i32(truncate(a / b))
end

-- Computes the remainder of two integers with C semantics, where the sign follows the dividend.
function vm.imod(a: number, b: number): number
    if b == 0 then
        error("modulo by zero", 2)
    end
    return vm.i32(math.fmod(a, b))
end

-- Rounds a number to the nearest float32 value.
function vm.f32(x: number): number
    local value: number = string.unpack("<f", string.pack("<f", x)) :: any
    return value
end

-- Runs a region of an event script that skip mode can jump over; skip mode arrives with event scripts, so body always runs for now.
function vm.section(body: () -> ()): ()
    body()
end

-- Marks the end of a skippable region of an event script.
function vm.skip_end(): ()
end

return vm
