local seconds = tonumber(arg and arg[1]) or 5

for i = seconds, 1, -1 do
    io.write(("\r\27[33m%d\27[0m second%s left...  "):format(i, i == 1 and "" or "s"))
    io.flush()
    system.sleep(1000)
end
print("\r\27[32mDone!\27[0m                 ")
