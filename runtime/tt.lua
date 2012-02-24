local test = require 'test'
local lbind = require 'lbind'
for k, v in pairs(test) do print(k, v) end
print "============="
for k, v in pairs(lbind) do print(k, v) end
print "============="
local x1 = test.new()
print(lbind.type(x1))
local x2 = test.new_local(1)
print(lbind.type(x2))
--test.new_local = nil
local x3 = test(2)
print(lbind.type(x3))
print(x1, x2, x3)
print(lbind.owner(x1, x2, x3))
lbind.register(x1)
lbind.unregister(x2)
print(lbind.owner(x1, x2, x3))
print(x1:x(), x2:x(), x3:x())
x1:proc(function(n)
    print(">> "..n)
end)
x1.foo = "abc"
print(x1.foo)
print(x1)
x1:delete()
print("second")
print(x1)
--print(x1:x())
print(x1.x)
print("end")
print "============="
local info = lbind.info()
for k, v in pairs(info) do print(k, v) end
print "pointers:"
for k, v in pairs(lbind.info "pointers") do print("",k, v) end
print "types:"
for k, v in pairs(lbind.info "types") do print("",k, v) end
print "libmeta:"
for k, v in pairs(lbind.info "libmeta") do print("",k, v) end
print "libmap:"
for k, v in pairs(lbind.info "libmap") do print("",k, v) end
print "============="
-- cc: cc='lua52'
