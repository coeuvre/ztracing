const c = @cImport({
    @cDefine("CIMGUI_DEFINE_ENUMS_AND_STRUCTS", "1");
    @cDefine("ImDrawIdx", "unsigned int");
    @cInclude("cimgui.h");
    @cInclude("cimplot.h");
});

pub usingnamespace c;
