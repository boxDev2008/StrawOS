local YELLOW, GREEN, BLUE, GRAY, RESET = "\27[33m", "\27[32m", "\27[34m", "\27[90m", "\27[0m"

print(YELLOW .. "Hello from a Lua script!" .. RESET)
print(GRAY .. "Double-click any .lua file in Files to run it like this one." .. RESET)
print()

io.write("What's your name? ")
local name = io.read() or ""
if name == "" then name = "stranger" end

print(("Nice to meet you, %s%s%s."):format(GREEN, name, RESET))
print()

print("Here are the squares of 1 to 10:")
for i = 1, 10 do
    local bar = string.rep("#", i * i // 4)
    print(("%s%3d%s  %s%s%s"):format(BLUE, i * i, RESET, GREEN, bar, RESET))
end
