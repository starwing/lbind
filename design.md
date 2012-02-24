lbind 0.2 设计
==============

通过参考 [LuaNativeObject]，我们改变了 `lbind` 的设计。采用了类似
LuaNativeObject 的设计方法，即：

  - 表格代表可嵌套集合
  - 冒号方法代表选项
  - 内部用表格而不是 `upvalue` 整体描述数据，数据透明地放置在导出表
    中，导出表由 `exports` 函数指定。该函数同时将表指定为导出表，并
    为导出表设置元表使之能访问 `lbind` 内的函数。
  - 导出数据透明：
    - `tag` 域指明数据类型（`module`，`class`，`enum`，`variable`
      ，`func` 等等）
    - 其他 `hash` 域指明必要的选项。
    - 数组域指明包含的项目。
  - 导出数据不包含任何创建的操作。而由 `gen` 模块单独提供针对 `tag`
    的函数作为导出的方法。也就是说，`gen` + 导出表就可以生成绑定文本
    了。
  - 类型系统重新设计，骨架上改成上述的内容，具体生产方法不变。仍然是
    采用模板式生成方式。
  - `gen_xxx` 模块被分解为两个部分： `api` 模块和 `gen` 模块，前者提
    供生成数据表格（称 `ast` 也可）的接口，而后者提供用于从数据表格
    中产生实际的绑定代码的函数体。
  - `driver` 模块提供了将两者结合在一起的驱动部分。
  - `init` 提供了 `exports`，`gen` 等函数。

[LuaNativeObject]: http://github.com/Neopallium/LuaNativeObject

语法
----

variable, func 和 enum 作为原子类型。即：只有方法，不接受表格作为参数
。

    var_obj = variable(name)

variable 接受字符串作为变量名，返回一个表格作为对象本身。

    var_obj:readonly()
    var_obj:writeonly()

设置只读/只写属性。这将决定了这个变量在其模块母表中的只读/只写属性。注
意：目前这个可能实现不了= =

    var_obj:type(type_obj)

设置变量的类型。

    func_obj = func(name)

func 函数接受字符串作为函数在 Lua 中的名字。

    func_obj:alias(name)

函数别名

    func_obj:hide(name or typelist...)

隐藏函数别名，或者特定重载。如果第一个参数是字符串则隐藏对应的别名，否
则隐藏对应的重载。

    func_obj:args(typelist...)
    
开启一个新的重载，函数下面的操作 **必须** 在重载的基础上才能操作。如果
没有重载，则默认的重载是“无参数”重载，即函数不接受任何参数。

如果参数签名和之前的某个重载完全一致（除参数名和默认值以外），则那个重
载会被选中用于修改。

除非是选中了某个重载（即该重载在 C++ 的意义上会造成歧义），否则该重载
的 Lua 类型不允许和之前的任何重载相同。这一点会在 gen 的时候进行检查，
如果满足这个要求，会在 gen 的时候警告。

    func_obj:rets(typelist...)

设置当前重载的返回值。如果返回值中有名字和函数重载中的名字相同，则其类
型必须相同，否则 gen 时警告。

    func_obj:prev(text)
    func_obj:post(text)
    func_obj:call(text)
    func_obj:body(text)

设置代码：
  - prev 指定的文本会出现在当前重载在提取参数之后，调用具体函数之前；
  - post 会出现在调用函数之后，产生返回值之前；
  - body 如果提供，则忽略其余的三个，会替代 prev+call+post 的部分；
  - call 会替代用于调用函数的代码；

理论上，可以通过以下代码凭空产生一个 Lua 函数：

    func "test"
        :args(lua_State "L", lua_Object "obj", int "a", int "b", int "c")
        :rets(int "d", int "e")
        :body [[
      printf("lua_State = %p, a = %d, b = %d, c = %d\n", L, a, b, c);
      lua_call(obj, 0, 0);
      d = 10;
      e = lua_tonumber(L, -1);
    ]]
               
这段“绑定”代码会产生这样的 C 函数：

    int binder_test(lua_State *L) {
      int obj = 1;
      int a = lua_tonumber(L, 2);
      int b = lua_tonumber(L, 3);
      int c = lua_tonumber(L, 4);
      int d;
      int e;
      printf("lua_State = %p, a = %d, b = %d, c = %d\n", L, a, b, c);
      lua_call(obj, 0, 0);
      d = 10;
      e = lua_tonumber(L, -1);
      lua_pushinteger(L, d);
      lua_pushinteger(L, e);
      return 2;
    }

注意这个函数没有绑定任何其他的 C 函数，它就是作为本身使用的，这个特性
允许你通过 lbind 实际上书写任何独立的 C 模块。

-- cc: cc='lunamark'
