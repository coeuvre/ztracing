const c = @import("c.zig");

pub fn getWindowPos() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowPos(&result);
    return result;
}

pub fn getWindowContentRegionMin() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowContentRegionMin(&result);
    return result;
}

pub fn getWindowContentRegionMax() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowContentRegionMax(&result);
    return result;
}

pub fn getMousePos() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetMousePos(&result);
    return result;
}
