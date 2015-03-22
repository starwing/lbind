#define LBIND_IMPLEMENTATION
#include "lbind.h"
/* cc: lua='lua53' output='lbind.dll'
 * cc: flags+='-s -O2 -Wall -std=c99 -pedantic -mdll -Id:/$lua/include'
 * cc: run='$lua tt.lua' libs+='-L D:/$lua -l$lua' */
