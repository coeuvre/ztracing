const c = @cImport({
    @cDefine("CIMGUI_DEFINE_ENUMS_AND_STRUCTS", "1");
    @cDefine("CIMGUI_API", "");
    @cInclude("cimgui.h");
});

pub usingnamespace c;
