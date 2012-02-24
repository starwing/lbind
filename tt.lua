local exports = {}
local _ENV = exports 

require 'lbind'          .exports(_ENV)
require 'lbind.typeinfo' .exports(_ENV)

-- config module
require 'lbind.config' {
    indent = 2;
}

module "test"
        :lang "C++"
        :output "test_gen.cpp" {

    include "stdio.h";
    source [[

enum testEnum {
  enum1,
  enum2,
  enum3,
  enum4 = 98,
  enum5 = 98,
};

class test {
  int x_;
public:
  test(int x = 0) : x_(x) {}
  virtual int vx() { return x(); }
  int x() { return x_; }
  int proc(int(*f)(int)) { return f(x()); }
};

int virt_test(test *t) {
    printf("vx() returns: %d\n", t->vx());
}

]];

    enum "testEnum" {
        "enum1",
        "enum2",
        "enum3",
        "enum4",
        "enum5",
    };

    class "test" {
        func "new" :args(int "x":opt(0));
        virtual "vx" :rets "int";
        func "x" :rets "int";
        func "proc" :args(callback "f"(int):rets(int)) :rets "int";
    };

    func "virt_test" :args(class_ptr "test" "t") :rets(int);
};

-- first way to change: use a table contain new things.
test.test {
    func "new" :args(int "x":opt(2)); -- select and change default argument.
};

-- second way to change: use add/hide to select items.
test.testEnum
    :add 'enum6' (1000) -- add a new enum item.

-- add a alias of type.
test.testEnum
    :alias "myEnum"

gen(exports)
