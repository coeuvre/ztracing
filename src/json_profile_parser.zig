const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;

// State
//   ctx, token
//
//
// var runtime = Runtime(ResumeType, YieldType)
// const v: YieldType = runtime.resume(r: ResumeType);
//
// by resuming, or by poping
// Step: resume(ctx) {
//   ctx.args();
//
//   ctx.yield(v);
//   ctx.push(State);
//   ctx.pop();
// }

const Context = union(enum) {
    run: Token,
    pop: State,
    push,
};

const StateMachine = struct {
    const ControlFlow = union(enum) {
        push: State,
        pop,
        yield: ParseResult,
    };

    stack: std.ArrayList(State),

    pub fn run(self: *StateMachine, token: Token) ParseError!ParseResult {
        var ctx = .{ .run = token };
        while (true) {
            switch (ctx) {
                .pop => |state| {
                    state.deinit();
                },
                else => {},
            }

            std.debug.assert(self.stack.items.len > 0);
            var top = &self.stack.items[self.stack.items.len - 1];
            const control_flow = try top.onResume(&ctx);
            switch (control_flow) {
                .push => |state| {
                    self.stack.append(state);
                    ctx = .push;
                },
                .pop => {
                    const state = self.stack.pop();
                    ctx = .{ .pop = state };
                },
                .yield => |result| {
                    return result;
                },
            }
        }
    }
};

pub const TraceEvent = union(enum) {
    a,
};

pub const ParseError = anyerror;
pub const ParseResult = union(enum) {
    trace: TraceEvent,
    none,
};

const StateResult = union(enum) {
    value: std.json.Value,
    string: []u8,
    none,
};

const Context = struct {
    const StateStack = std.ArrayList(State);

    stack: StateStack,
    result: ?ParseResult,

    pub fn init(allocator: Allocator) Context {
        return .{
            .stack = StateStack.init(allocator),
            .result = null,
        };
    }

    pub fn deinit(self: *Context) void {
        while (self.stack.items.len > 0) {
            var state = self.stack.pop();
            state.deinit();
        }
        self.stack.deinit();
    }

    pub fn yield(self: *Context, result: ParseResult) void {
        self.result = result;
    }

    pub fn push(self: *Context, state: State) void {
        self.stack.append(state) catch unreachable;
    }

    pub fn pop(self: *Context, result: StateResult) ParseError!void {
        {
            var state = self.stack.pop();
            state.deinit();
        }
        if (self.stack.items.len > 0) {
            try self.stack.items[self.stack.items.len - 1].onPopResult(self, result);
        }
    }
};

const Start = struct {
    allocator: Allocator,

    fn init(allocator: Allocator) Start {
        return .{ .allocator = allocator };
    }

    fn onResume(self: *Start, ctx: *Context, token: Token) ParseError!void {
        switch (token) {
            .object_begin => {
                var object_foramt = .{ .object_format = ObjectFormat.init(self.allocator) };
                ctx.push(object_foramt);
            },
            .array_begin => {
                var array_format = .{ .array_format = ArrayFormat.init() };
                ctx.push(array_format);
            },
            else => return error.expected_token,
        }
    }

    fn onPopResult(self: *@This(), ctx: *Context, result: StateResult) ParseError!void {
        _ = self;
        try ctx.pop(result);
    }
};

const ObjectFormat = struct {
    allocator: Allocator,
    state: union(enum) {
        begin,
        wait_object_key,
        wait_json_value,
    },

    fn init(allocator: Allocator) ObjectFormat {
        return .{ .allocator = allocator, .state = .begin };
    }

    fn onResume(self: *ObjectFormat, ctx: *Context, token: Token) ParseError!void {
        switch (self.state) {
            .begin => {
                switch (token) {
                    .object_begin => {
                        ctx.push(.{ .object_key = ObjectKey.init(self.allocator) });
                        self.state = .wait_object_key;
                        ctx.yield(.none);
                    },
                    else => {
                        return error.unexpected_token;
                    },
                }
            },
            else => unreachable,
        }
    }

    fn onPopResult(self: *ObjectFormat, ctx: *Context, result: StateResult) ParseError!void {
        switch (self.state) {
            .wait_object_key => {
                switch (result) {
                    .string => |str| {
                        defer self.allocator.free(str);
                        std.log.debug("{s}", .{str});

                        if (std.mem.eql(u8, str, "traceEvents")) {
                            ctx.push(.{ .array_format = ArrayFormat.init() });
                            self.state = .wait_json_value;
                        } else {
                            // Ignore unrecognized key
                            ctx.push(.{ .json_value = JsonValue.init() });
                            self.state = .wait_json_value;
                        }
                    },
                    .none => {
                        try ctx.pop(.none);
                    },
                    else => unreachable,
                }
            },
            .wait_json_value => {
                ctx.push(.{ .object_key = ObjectKey.init(self.allocator) });
                self.state = .wait_object_key;
            },
            else => unreachable,
        }
    }
};

const ArrayFormat = struct {
    state: union(enum) {
        begin,
        wait_array_value,
    },

    fn init() ArrayFormat {
        return .{ .state = .begin };
    }

    fn onResume(self: *ArrayFormat, ctx: *Context, token: Token) ParseError!void {
        switch (self.state) {
            .begin => {
                switch (token) {
                    .array_begin => {
                        ctx.push(.{ .array_value = ArrayValue.init() });
                        self.state = .wait_array_value;
                        ctx.yield(.none);
                    },
                    else => return error.unexpected_token,
                }
            },
            else => unreachable,
        }
    }

    fn onPopResult(self: *ArrayFormat, ctx: *Context, result: StateResult) ParseError!void {
        switch (self.state) {
            .wait_array_value => {
                switch (result) {
                    .value => {
                        ctx.push(.{ .array_value = ArrayValue.init() });
                    },
                    .none => try ctx.pop(.none),
                    else => unreachable,
                }
            },
            else => unreachable,
        }
    }
};

const ObjectKey = struct {
    allocator: Allocator,
    key: std.ArrayList(u8),
    state: enum {
        begin,
        parse_string,
    },

    fn init(allocator: Allocator) ObjectKey {
        return .{
            .allocator = allocator,
            .key = std.ArrayList(u8).init(allocator),
            .state = .begin,
        };
    }

    fn onResume(self: *ObjectKey, ctx: *Context, token: Token) ParseError!void {
        switch (self.state) {
            .begin => {
                switch (token) {
                    .object_end => {
                        try ctx.pop(.none);
                        ctx.yield(.none);
                    },
                    .string,
                    .partial_string,
                    .partial_string_escaped_1,
                    .partial_string_escaped_2,
                    .partial_string_escaped_3,
                    .partial_string_escaped_4,
                    => {
                        self.state = .parse_string;
                    },
                    else => {
                        std.log.err("Unexpected token: {s}", .{@tagName(token)});
                        return error.unexpected_token;
                    },
                }
            },
            .parse_string => {
                switch (token) {
                    .partial_string => |str| {
                        try self.key.appendSlice(str);
                        ctx.yield(.none);
                    },
                    .partial_string_escaped_1 => |*str| {
                        try self.key.appendSlice(str);
                        ctx.yield(.none);
                    },
                    .partial_string_escaped_2 => |*str| {
                        try self.key.appendSlice(str);
                        ctx.yield(.none);
                    },
                    .partial_string_escaped_3 => |*str| {
                        try self.key.appendSlice(str);
                        ctx.yield(.none);
                    },
                    .partial_string_escaped_4 => |*str| {
                        try self.key.appendSlice(str);
                        ctx.yield(.none);
                    },
                    .string => |str| {
                        try self.key.appendSlice(str);
                        const buf = try self.key.toOwnedSlice();
                        try ctx.pop(.{ .string = buf });
                        ctx.yield(.none);
                    },
                    else => {
                        std.log.err("Unexpected token: {s}", .{@tagName(token)});
                        return error.unexpected_token;
                    },
                }
            },
        }
    }

    fn deinit(self: *ObjectKey) void {
        self.key.deinit();
    }
};

const JsonValue = struct {
    fn init() JsonValue {
        return .{};
    }

    fn onResume(self: *JsonValue, ctx: *Context, token: Token) ParseError!void {
        _ = token;
        _ = self;
        ctx.yield(.none);
    }
};

const ArrayValue = struct {
    state: enum {
        begin,
        wait_json_value,
    },

    fn init() ArrayValue {
        return .{ .state = .begin };
    }

    fn onResume(self: *ArrayValue, ctx: *Context, token: Token) ParseError!void {
        switch (self.state) {
            .begin => {
                switch (token) {
                    .array_end => {
                        try ctx.pop(.none);
                        ctx.yield(.none);
                    },
                    else => {
                        ctx.push(.{ .json_value = JsonValue.init() });
                        self.state = .wait_json_value;
                    },
                }
            },
            else => unreachable,
        }
    }

    fn onPopResult(self: *ArrayValue, ctx: *Context, result: StateResult) ParseError!void {
        switch (self.state) {
            .wait_json_value => {
                try ctx.pop(result);
            },
            else => unreachable,
        }
    }
};

const State = union(enum) {
    start: Start,
    object_format: ObjectFormat,
    array_format: ArrayFormat,
    object_key: ObjectKey,
    json_value: JsonValue,
    array_value: ArrayValue,

    pub fn deinit(self: *State) void {
        switch (self.*) {
            inline else => |*state| {
                if (@hasDecl(@TypeOf(state.*), "deinit")) {
                    state.deinit();
                }
            },
        }
    }

    pub fn onResume(self: *State, ctx: *Context, token: Token) ParseError!void {
        switch (self.*) {
            inline else => |*state| return state.onResume(ctx, token),
        }
    }

    pub fn onPopResult(self: *State, ctx: *Context, result: StateResult) ParseError!void {
        switch (self.*) {
            inline else => |*state| {
                if (@hasDecl(@TypeOf(state.*), "onPopResult")) {
                    return state.onPopResult(ctx, result);
                }
            },
        }
    }
};

pub const JsonProfileParser = struct {
    allocator: Allocator,
    scanner: std.json.Scanner,
    ctx: Context,

    pub fn init(allocator: Allocator) JsonProfileParser {
        var ctx = Context.init(allocator);
        ctx.push(.{ .start = Start.init(allocator) });
        return .{
            .allocator = allocator,
            .scanner = std.json.Scanner.initStreaming(allocator),
            .ctx = ctx,
        };
    }

    pub fn deinit(self: *JsonProfileParser) void {
        self.scanner.deinit();
        self.ctx.deinit();
    }

    pub fn feedInput(self: *JsonProfileParser, input: []const u8) void {
        self.scanner.feedInput(input);
    }

    pub fn endInput(self: *JsonProfileParser) void {
        self.scanner.endInput();
    }

    pub fn next(self: *JsonProfileParser) ParseError!ParseResult {
        while (true) {
            const token = self.scanner.next() catch |err| switch (err) {
                error.BufferUnderrun => {
                    std.debug.assert(!self.scanner.is_end_of_input);
                    return .none;
                },
                else => {
                    return err;
                },
            };
            const result = try self.parseToken(token);
            if (result != .none) {
                return result;
            }
            switch (token) {
                .end_of_document => {
                    if (self.scanner.is_end_of_input) {
                        return .none;
                    }
                    return error.unexpected_end_of_document;
                },
                else => {},
            }
        }
    }

    fn parseToken(self: *JsonProfileParser, token: Token) ParseError!ParseResult {
        errdefer {
            while (self.ctx.stack.items.len > 0) {
                var state = self.ctx.stack.pop();
                state.deinit();
            }
        }

        self.ctx.result = null;
        while (self.ctx.result == null and self.ctx.stack.items.len > 0) {
            var top_state = &self.ctx.stack.items[self.ctx.stack.items.len - 1];
            try top_state.onResume(&self.ctx, token);
        }

        {
            std.log.debug("Parsing token `{s}`", .{@tagName(token)});
            for (0..self.ctx.stack.items.len) |i| {
                const at = self.ctx.stack.items.len - i - 1;
                const state = &self.ctx.stack.items[at];
                std.log.debug("    [{}] {s}", .{ at, @tagName(state.*) });
            }
        }

        if (self.ctx.result) |result| {
            return result;
        }
        return .none;
    }
};

fn parseAll(input: []const u8) ParseError![]ParseResult {
    var parser = JsonProfileParser.init(std.testing.allocator);
    parser.feedInput(input);
    parser.endInput();
    defer parser.deinit();

    var results = std.ArrayList(ParseResult).init(std.testing.allocator);
    errdefer results.deinit();

    while (true) {
        const result = try parser.next();
        if (result == .none) {
            break;
        }
        try results.append(result);
    }

    return try results.toOwnedSlice();
}

test "invalid profile" {
    try std.testing.expectError(error.SyntaxError, parseAll("}"));
}

test "empty profile" {
    const results = try parseAll("{}");
    try std.testing.expect(results.len == 0);
}

test "empty traceEvents profile" {
    const results = try parseAll("{\"traceEvents\":[]}");
    try std.testing.expect(results.len == 0);
}
