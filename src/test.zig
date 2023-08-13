const json_profile_parser = @import("./json_profile_parser.zig");
const profile = @import("./profile.zig");

comptime {
    _ = json_profile_parser;
    _ = profile;
}
