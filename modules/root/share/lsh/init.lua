local C = {}
local help = {}

shell.commands = C
shell.columns = 80
shell.history = {}

local RESET, BOLD = "\27[0m", "\27[1m"
local RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, GRAY = "\27[31m", "\27[32m", "\27[33m", "\27[34m", "\27[35m", "\27[36m", "\27[90m"

local function out(...)
    io.write(...)
end

local function err(msg)
    msg = tostring(msg):gsub("^%[string \"stdin\"%]:", "stdin:")
    io.write(RED, msg, RESET, "\n")
end

local function fail(msg)
    error({ shell_message = msg }, 0)
end

local function human_size(n)
    if n < 1024 then return n .. " B" end
    if n < 1024 * 1024 then return string.format("%.1f KB", n / 1024) end
    return string.format("%.1f MB", n / (1024 * 1024))
end

local function basename(path)
    return path:match("([^/]+)/*$") or path
end

local function join(dir, name)
    if dir == "/" then return "/" .. name end
    return dir .. "/" .. name
end

local function abs(path)
    return system.realpath(path) or path
end

local function is_dir(path)
    local st = system.stat(path)
    return st and st.type == "dir"
end

local function read_file(path)
    local f, e = io.open(path, "rb")
    if not f then fail(e) end
    local data = f:read("a")
    f:close()
    return data
end

local function write_file(path, data)
    local f, e = io.open(path, "wb")
    if not f then fail(e) end
    f:write(data)
    f:close()
end

local function command(name, usage, desc, fn)
    C[name] = fn
    help[#help + 1] = { name = name, usage = usage, desc = desc }
end

local function list_sorted(path)
    local entries, e = system.list(path)
    if not entries then fail(e) end
    table.sort(entries, function(a, b)
        if (a.type == "dir") ~= (b.type == "dir") then return a.type == "dir" end
        return a.name:lower() < b.name:lower()
    end)
    return entries
end

local function colored_name(e)
    if e.type == "dir" then return BLUE .. BOLD .. e.name .. RESET .. "/", #e.name + 1 end
    if e.type == "device" or e.type == "socket" then return YELLOW .. e.name .. RESET, #e.name end
    if e.name:match("%.elf$") then return GREEN .. e.name .. RESET, #e.name end
    if e.name:match("%.lua$") then return MAGENTA .. e.name .. RESET, #e.name end
    return e.name, #e.name
end

command("help", "help", "show this list", function()
    out(BOLD, "StrawOS Lua shell", RESET, "\n\n")
    out("Type Lua code, or a command followed by its arguments:\n\n")
    for _, h in ipairs(help) do
        out("  ", YELLOW, string.format("%-26s", h.usage), RESET, GRAY, h.desc, RESET, "\n")
    end
    out("\nPrograms in /bin run by name, e.g. ", GREEN, "cube", RESET, " or ", GREEN, "files /pictures", RESET, ".\n")
    out("Chain commands with ", YELLOW, ";", RESET, ", ", YELLOW, "&&", RESET, " and ", YELLOW, "||", RESET,
        ", e.g. ", GREEN, "cd /bin && ls", RESET, ".\n")
    out("Start a line with ", YELLOW, "=", RESET, " to print a value, e.g. ", YELLOW, "=2^10", RESET, ".\n")
    out("Commands are also Lua functions: ", YELLOW, "sh.ls(\"/bin\")", RESET, ", ",
        YELLOW, "system.list(\"/\")", RESET, ".\n")
end)

command("ls", "ls [-l] [path]", "list a directory", function(...)
    local args, long = {}, false
    for _, a in ipairs({ ... }) do
        if a == "-l" then long = true else args[#args + 1] = a end
    end
    if #args == 0 then args[1] = "." end

    for i, path in ipairs(args) do
        if #args > 1 then out(BOLD, path, ":", RESET, "\n") end
        local st = system.stat(path)
        if not st then fail(path .. ": no such file or directory") end
        local entries = st.type == "dir" and list_sorted(path) or { { name = basename(path), type = st.type, size = st.size } }

        if long then
            for _, e in ipairs(entries) do
                local size = e.type == "file" and human_size(e.size) or ""
                out(GRAY, string.format("%-7s %10s  ", e.type, size), RESET, (colored_name(e)), "\n")
            end
        elseif #entries > 0 then
            local width = 0
            for _, e in ipairs(entries) do
                local _, n = colored_name(e)
                if n > width then width = n end
            end
            width = width + 2
            local per_line = math.max(1, shell.columns // width)
            for j, e in ipairs(entries) do
                local text, n = colored_name(e)
                out(text)
                if j % per_line == 0 or j == #entries then out("\n") else out(string.rep(" ", width - n)) end
            end
        end
        if i < #args then out("\n") end
    end
end)

command("cd", "cd [dir]", "change the current directory", function(dir)
    local ok, e = system.cd(dir or "/")
    if not ok then fail(e) end
end)

command("pwd", "pwd", "print the current directory", function()
    out(system.cwd(), "\n")
end)

command("cat", "cat <file...>", "print files", function(...)
    for _, path in ipairs({ ... }) do
        local data = read_file(path)
        out(data)
        if #data > 0 and data:sub(-1) ~= "\n" then out("\n") end
    end
end)

command("echo", "echo [text...]", "print its arguments", function(...)
    out(table.concat({ ... }, " "), "\n")
end)

command("mkdir", "mkdir <dir...>", "create directories", function(...)
    for _, path in ipairs({ ... }) do
        local ok, e = system.mkdir(path)
        if not ok then fail(e) end
    end
end)

command("touch", "touch <file...>", "create empty files", function(...)
    for _, path in ipairs({ ... }) do
        if not system.stat(path) then write_file(path, "") end
    end
end)

command("rm", "rm <path...>", "remove files or empty directories", function(...)
    for _, path in ipairs({ ... }) do
        local ok, e = os.remove(path)
        if not ok then fail(e or (path .. ": could not remove")) end
    end
end)

command("mv", "mv <from> <to>", "move or rename", function(from, to)
    if not from or not to then fail("usage: mv <from> <to>") end
    if is_dir(to) then to = join(to, basename(from)) end
    local ok, e = os.rename(from, to)
    if not ok then fail(e or "could not move") end
end)

local function copy(from, to)
    local st = system.stat(from)
    if not st then fail(from .. ": no such file or directory") end
    if st.type == "dir" then
        local ok, e = system.mkdir(to)
        if not ok then fail(e) end
        for _, e2 in ipairs(system.list(from)) do
            copy(join(abs(from), e2.name), join(abs(to), e2.name))
        end
    else
        write_file(to, read_file(from))
    end
end

command("cp", "cp <from> <to>", "copy files or directories", function(from, to)
    if not from or not to then fail("usage: cp <from> <to>") end
    if is_dir(to) then to = join(to, basename(from)) end
    copy(from, to)
end)

command("write", "write <file> <text...>", "write text to a file", function(path, ...)
    if not path then fail("usage: write <file> <text...>") end
    write_file(path, table.concat({ ... }, " ") .. "\n")
end)

command("clear", "clear", "clear the screen", function()
    out("\27[2J\27[H")
end)

command("date", "date", "show the date and time", function()
    out(os.date("%A, %d %B %Y  %H:%M:%S"), "\n")
end)

command("uptime", "uptime", "time since boot", function()
    local s = system.uptime() // 1000
    out(string.format("up %d:%02d:%02d\n", s // 3600, s // 60 % 60, s % 60))
end)

command("mem", "mem", "memory usage", function()
    local m = system.mem()
    local used = m.total - m.free
    out(string.format("%s used of %s (%d%%)\n", human_size(used), human_size(m.total), used * 100 // m.total))
end)

local function launch(prog, args)
    local background = args[#args] == "&"
    if background then table.remove(args) end
    for i, a in ipairs(args) do
        if not a:match("^%-") and system.stat(a) then args[i] = abs(a) end
    end
    if background then
        local pid = shell.spawn(prog, table.unpack(args))
        if not pid then fail("could not start " .. prog) end
        out(GRAY, "[", pid, "] started in the background", RESET, "\n")
        return true
    end
    local code, e = shell.run(prog, table.unpack(args))
    if not code then fail(e) end
    shell.last_status = code
    return code == 0
end

command("run", "run <program> [args...]", "start a program (add & to run it in the background)", function(name, ...)
    if not name then fail("usage: run <program> [args...]") end
    local prog = shell.which(name)
    if not prog then fail(name .. ": program not found") end
    launch(prog, { ... })
end)

command("kill", "kill <pid>", "stop a background program", function(pid)
    if not tonumber(pid) or not shell.kill(tonumber(pid)) then fail("no such program: " .. tostring(pid)) end
end)

command("which", "which <name>", "find a program", function(name)
    if C[name] then out(name, ": shell command\n") return end
    local prog = name and shell.which(name)
    if not prog then fail((name or "") .. ": not found") end
    out(prog, "\n")
end)

command("edit", "edit <file>", "open a file in Notes", function(path)
    if not path then fail("usage: edit <file>") end
    shell.spawn("/bin/notes.elf", abs(path))
end)

command("open", "open <path>", "open with the matching app", function(path)
    path = path or "."
    local st = system.stat(path)
    if not st then fail(path .. ": no such file or directory") end
    local full = abs(path)
    if st.type == "dir" then
        shell.spawn("/bin/files.elf", full)
    elseif full:match("%.lua$") then
        shell.spawn("/bin/terminal.elf", "--run", full)
    elseif full:match("%.png$") or full:match("%.bmp$") then
        shell.spawn("/bin/viewer.elf", full)
    elseif full:match("%.elf$") then
        shell.spawn(full)
    else
        shell.spawn("/bin/notes.elf", full)
    end
end)

local function run_script(path, ...)
    local chunk, e = loadfile(path)
    if not chunk then fail(e) end
    arg = { [0] = path, ... }
    return chunk(...)
end

command("lua", "lua <file> [args...]", "run a Lua script", function(path, ...)
    if not path then fail("usage: lua <file> [args...]") end
    run_script(path, ...)
end)

local function is_script(word)
    if not word:match("%.lua$") then return false end
    local st = system.stat(word)
    return st and st.type == "file"
end

command("exit", "exit [code]", "leave the shell", function(code)
    shell.exit_code = tonumber(code) or 0
end)

sh = C

local function split_args(s)
    local args, cur, quote, any = {}, {}, nil, false
    local i = 1
    while i <= #s do
        local c = s:sub(i, i)
        if quote then
            if c == quote then
                quote = nil
            elseif c == "\\" and quote == '"' and i < #s then
                i = i + 1
                cur[#cur + 1] = s:sub(i, i)
            else
                cur[#cur + 1] = c
            end
        elseif c == '"' or c == "'" then
            quote, any = c, true
        elseif c:match("%s") then
            if #cur > 0 or any then
                args[#args + 1] = table.concat(cur)
                cur, any = {}, false
            end
        elseif c == "\\" and i < #s then
            i = i + 1
            cur[#cur + 1] = s:sub(i, i)
        else
            cur[#cur + 1] = c
        end
        i = i + 1
    end
    if quote then return nil, "unfinished quote" end
    if #cur > 0 or any then args[#args + 1] = table.concat(cur) end
    return args
end

local function looks_like_command(word, rest)
    if rest == "" then return true end
    if not rest:match("^%s") then return false end
    local after = rest:match("^%s+(.*)$")
    if after:match("^=[^=]") or after == "=" then return false end
    return true
end

local function format_value(v, depth, seen)
    local t = type(v)
    if t == "string" then
        return depth > 0 and string.format("%q", v) or v
    elseif t == "number" or t == "boolean" or t == "nil" then
        return CYAN .. tostring(v) .. RESET
    elseif t == "table" then
        if seen[v] or depth > 2 then return GRAY .. tostring(v) .. RESET end
        seen[v] = true
        local parts, n = {}, 0
        for i, x in ipairs(v) do
            n = n + 1
            if n > 30 then parts[#parts + 1] = GRAY .. "..." .. RESET break end
            parts[#parts + 1] = format_value(x, depth + 1, seen)
        end
        local len = #v
        for k, x in pairs(v) do
            if not (math.type(k) == "integer" and k >= 1 and k <= len) then
                n = n + 1
                if n > 30 then parts[#parts + 1] = GRAY .. "..." .. RESET break end
                local key = type(k) == "string" and k:match("^[%a_][%w_]*$") and k or ("[" .. format_value(k, depth + 1, seen) .. "]")
                parts[#parts + 1] = key .. " = " .. format_value(x, depth + 1, seen)
            end
        end
        seen[v] = nil
        return "{ " .. table.concat(parts, ", ") .. (#parts > 0 and " }" or "}")
    else
        return GRAY .. tostring(v) .. RESET
    end
end

local function traceback(e)
    if type(e) == "table" and e.shell_message then return e end
    return debug.traceback(e, 2)
end

local function report(e)
    if type(e) == "table" and e.shell_message then
        err(e.shell_message)
    else
        local msg = tostring(e)
        if msg:find("interrupted") then
            err("interrupted")
        else
            local first, rest = msg:match("^(.-)\n(.*)$")
            err(first or msg)
            if rest then
                local frames = {}
                for frame in rest:gmatch("\n%s*([^\n]+)") do
                    if frame:find("in function 'xpcall'", 1, true) then break end
                    if not frame:match("^%[C%]") then frames[#frames + 1] = "  " .. frame end
                end
                if #frames > 1 then out(GRAY, table.concat(frames, "\n"), RESET, "\n") end
            end
        end
    end
end

-- Each runner returns whether it succeeded, plus the error when it raised one.
local function run_lua(fn)
    local results = table.pack(xpcall(fn, traceback))
    if not results[1] then
        report(results[2])
        return false, results[2]
    end
    if results.n > 1 then
        local parts = {}
        for i = 2, results.n do parts[#parts + 1] = format_value(results[i], 0, {}) end
        out(table.concat(parts, "\t"), "\n")
    end
    return true
end

local function run_command(word, rest)
    local args, e = split_args(rest)
    if not args then err(e) return false end

    local fn = C[word]
    if not fn and is_script(word) then
        local script = word
        fn = function(...) return run_script(script, ...) end
    end
    if fn then
        local ok, e2 = xpcall(fn, traceback, table.unpack(args))
        if not ok then report(e2) return false, e2 end
        return true
    end

    local prog = shell.which(word)
    local ok, result = xpcall(launch, traceback, prog, args)
    if not ok then report(result) return false, result end
    return result
end

local function compile(chunk)
    local fn, e = load("return " .. chunk, "=stdin")
    if not fn then fn, e = load(chunk, "=stdin") end
    return fn, e
end

local function command_word(line)
    local word, rest = line:match("^([%w_%.%-/~]+)(.*)$")
    if word and looks_like_command(word, rest) and (C[word] or is_script(word) or shell.which(word)) then
        local defined = rawget(_G, word)
        if not (rest == "" and defined ~= nil and C[word] == nil) then return word, rest end
    end
end

local function split_chain(s)
    local parts, start, op, quote = {}, 1, ";", nil
    local function cut(stop, next_op)
        parts[#parts + 1] = { op = op, text = s:sub(start, stop):match("^%s*(.-)%s*$") }
        op = next_op
    end
    local i = 1
    while i <= #s do
        local c = s:sub(i, i)
        if quote then
            if c == quote then
                quote = nil
            elseif c == "\\" and quote == '"' then
                i = i + 1
            end
        elseif c == '"' or c == "'" then
            quote = c
        elseif c == "\\" then
            i = i + 1
        elseif s:sub(i, i + 1) == "&&" or s:sub(i, i + 1) == "||" then
            cut(i - 1, s:sub(i, i + 1))
            i = i + 1
            start = i + 1
        elseif c == ";" then
            cut(i - 1, ";")
            start = i + 1
        end
        i = i + 1
    end
    cut(#s)
    return parts
end

local function run_one(line)
    local word, rest = command_word(line)
    if word then return run_command(word, rest) end
    if line:match("^[%w_%.%-/~]+$") and rawget(_G, line) == nil then
        err(line .. ": command not found")
        return false
    end
    if line:sub(1, 1) == "=" then line = "return " .. line:sub(2) end
    local fn, e = compile(line)
    if not fn then err(e) return false end
    return run_lua(fn)
end

local function run_chain(parts)
    for i, p in ipairs(parts) do
        local after = parts[i + 1]
        if p.text == "" and (p.op ~= ";" or (after and after.op ~= ";")) then
            err("syntax error near " .. (p.op ~= ";" and p.op or after.op))
            return
        end
    end

    local ok = true
    for _, p in ipairs(parts) do
        if p.text ~= "" and (p.op == ";" or (p.op == "&&") == ok) then
            local e
            ok, e = run_one(p.text)
            if not ok and type(e) == "string" and e:find("interrupted") then return end
        end
    end
end

local pending

function shell.handle(line)
    local chunk = pending and (pending .. "\n" .. line) or line

    if not pending then
        local trimmed = line:match("^%s*(.-)%s*$")
        if trimmed == "" then return end
        local parts = split_chain(trimmed)
        if #parts > 1 then
            local fn, e = compile(trimmed)
            if command_word(parts[1].text) or (not fn and not e:match("<eof>$")) then
                run_chain(parts)
                return
            end
        end
        local word, rest = command_word(trimmed)
        if word then
            run_command(word, rest)
            return
        end
        if trimmed:sub(1, 1) == "=" then chunk = "return " .. trimmed:sub(2) end
    end

    local fn, e = compile(chunk)
    if not fn then
        if e:match("<eof>$") then
            pending = chunk
            return
        end
        pending = nil
        err(e)
        return
    end

    pending = nil
    run_lua(fn)
end

function shell.prompt()
    if pending then return GRAY .. "  .. " .. RESET end
    local cwd = system.cwd()
    return YELLOW .. BOLD .. "straw" .. RESET .. " " .. BLUE .. BOLD .. cwd .. RESET .. GRAY .. " > " .. RESET
end

local function main_script()
    local path = shell.run_script
    local dir = path:match("^(.*)/[^/]*$")
    if dir and dir ~= "" then system.cd(dir) else system.cd("/") end

    local ok, e = xpcall(run_script, traceback, path, table.unpack(shell.run_args or {}))
    io.flush()
    if not ok then report(e) end

    out("\n", GRAY, ok and "Script finished." or "Script stopped with an error.", " Press Enter to close this window.", RESET)
    io.flush()
    pcall(system.readline)
    return ok and 0 or 1
end

function shell.main()
    if shell.run_script then return main_script() end

    out(YELLOW, BOLD, "StrawOS", RESET, " Lua shell  ", GRAY, "(", _VERSION, ")", RESET, "\n")
    out("Type ", YELLOW, "help", RESET, " for commands, or any Lua code.\n\n")

    local rc = io.open("/config/lshrc.lua")
    if rc then
        rc:close()
        local ok, e = pcall(dofile, "/config/lshrc.lua")
        if not ok then err(e) end
    end

    while shell.exit_code == nil do
        local ok, e = pcall(function()
            io.write(shell.prompt())
            io.flush()
            local line = system.readline()
            if line == nil then
                shell.exit_code = 0
            elseif line == false then
                pending = nil
                out("\n")
            else
                shell.handle(line)
            end
        end)
        if not ok then report(e) end
        io.flush()
    end
    return shell.exit_code
end
