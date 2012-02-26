require 'lbind'.export(_ENV)
local types = require 'lbind.types'.export(_ENV)
local C = types.class

module 'gd' {
    export = true,
    include "gd.h";
    subfiles {
        --"gdImage.bind.lua";
    };

    object "gdImage" {
        include "errno.h";
        include "string.h";

        method "new" :cname "gdImageCreate"
            (int "sx", int "sy") :rets(selfType:ptr());
        method "newTrueColor" :cname "gdImageCreateTrueColor"
            (int "sx", int "sy") :rets(selfType:ptr());
        method "delete" () :alias "close" :cname "gdImageDestroy";
        method "color_allocate" :cname "gdImageColorAllocate"
            (int "r", int "g", int "b");
        method "line" :cname "gdImageLine"
            (int "x1", int "y1", int "x2", int "y2", int "color");
        method "toPNG" (char:const():ptr "name") :body [[
            FILE *pngout = fopen(name, "wb");
            if (pngout == NULL) {
                lua_pushnil(L);
                lua_pushstring(L, strerror(errno));
                return 2;
            }
            gdImagePng(self, pngout);
            fclose(pngout);
        ]]
    };
};
