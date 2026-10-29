local BOLD, CYAN, GRAY, RESET = "\27[1m", "\27[36m", "\27[90m", "\27[0m"

local function size(n)
    if n >= 1024 * 1024 then return ("%.1f MB"):format(n / 1024 / 1024) end
    return ("%.1f KB"):format(n / 1024)
end

local function row(label, value)
    print(("%s%-14s%s %s"):format(GRAY, label, RESET, value))
end

print(BOLD .. "System information" .. RESET)
print()

local mem = system.mem()
local up = system.uptime() // 1000
row("Date", os.date("%A %d %B %Y, %H:%M"))
row("Uptime", ("%d:%02d:%02d"):format(up // 3600, up // 60 % 60, up % 60))
row("Memory", ("%s used of %s"):format(size(mem.total - mem.free), size(mem.total)))
row("Lua", _VERSION)
print()

print(BOLD .. "Programs in /bin" .. RESET)
local total = 0
for _, e in ipairs(system.list("/bin")) do
    total = total + e.size
    print(("  %s%-16s%s %10s"):format(CYAN, e.name, RESET, size(e.size)))
end
print(("  %-16s %10s"):format("total", size(total)))
