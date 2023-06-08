const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;

const Context = struct {
    allocator: Allocator,
    state: union(enum) {
        run: Token,
        pop: State,
    },

    pub fn forRun(allocator: Allocator, t: Token) Context {
        return .{ .allocator = allocator, .state = .{ .run = t } };
    }

    pub fn forPop(allocator: Allocator, popped_state: State) Context {
        return .{ .allocator = allocator, .state = .{ .pop = popped_state } };
    }

    fn deinit(self: *Context) void {
        switch (self.state) {
            .pop => |*state| {
                state.deinit();
            },
            else => {},
        }
    }

    pub fn push(self: *Context, state: State) ControlFlow {
        _ = self;
        return .{ .push = state };
    }

    pub fn pop(self: *Context) ControlFlow {
        _ = self;
        return .pop;
    }

    pub fn yield(self: *Context, result: ParseResult) ControlFlow {
        _ = self;
        return .{ .yield = result };
    }

    pub fn yieldPush(self: *Context, result: ParseResult, state: State) ControlFlow {
        _ = self;
        return .{ .yield_push = .{ .state = state, .result = result } };
    }

    pub fn token(self: *Context) *Token {
        switch (self.state) {
            .run => |*a| return a,
            else => unreachable,
        }
    }

    pub fn popped(self: *Context) *State {
        switch (self.state) {
            .pop => |*s| return s,
            else => unreachable,
        }
    }
};

const ControlFlow = union(enum) {
    push: State,
    yield_push: struct {
        state: State,
        result: ParseResult,
    },
    pop,
    yield: ParseResult,
};

const StateMachine = struct {
    allocator: Allocator,
    stack: std.ArrayList(State),

    pub fn init(allocator: Allocator, init_state: State) StateMachine {
        var stack = std.ArrayList(State).init(allocator);
        stack.append(init_state) catch unreachable;
        return .{
            .allocator = allocator,
            .stack = stack,
        };
    }

    pub fn deinit(self: *StateMachine) void {
        while (self.stack.items.len > 0) {
            var state = self.stack.pop();
            state.deinit();
        }
    }

    pub fn run(self: *StateMachine, token: Token) ParseError!ParseResult {
        var ctx = Context.forRun(self.allocator, token);
        while (true) {
            {
                std.log.debug("Parsing token `{s}`", .{@tagName(token)});
                for (0..self.stack.items.len) |i| {
                    const at = self.stack.items.len - i - 1;
                    const state = &self.stack.items[at];
                    std.log.debug("    [{}] {s}", .{ at, @tagName(state.*) });
                }
            }

            std.debug.assert(self.stack.items.len > 0);
            var top = &self.stack.items[self.stack.items.len - 1];
            const control_flow = top.onResume(&ctx);
            ctx.deinit();

            switch (try control_flow) {
                .push => |state| {
                    try self.stack.append(state);
                    ctx = Context.forRun(self.allocator, token);
                },
                .pop => {
                    const state = self.stack.pop();
                    ctx = Context.forPop(self.allocator, state);
                },
                .yield => |result| {
                    return result;
                },
                .yield_push => |py| {
                    try self.stack.append(py.state);
                    return py.result;
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

const Start = struct {
    state: enum {
        begin,
        wait,
    },

    fn init() Start {
        return .{ .state = .begin };
    }

    fn onResume(self: *Start, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .object_begin => {
                        var object_foramt = .{ .object_format = ObjectFormat.init() };
                        self.state = .wait;
                        return ctx.push(object_foramt);
                    },
                    .array_begin => {
                        var array_format = .{ .array_format = ArrayFormat.init() };
                        self.state = .wait;
                        return ctx.push(array_format);
                    },
                    else => return error.expected_token,
                }
            },
            .wait => {
                // Parsing for ObjectFormat or ArrayFormat is done. Ignore remaining tokens.
                return ctx.yield(.none);
            },
        }
    }
};

const ObjectFormat = struct {
    state: union(enum) {
        begin,
        wait_object_key,
        wait_json_value,
        end,
    },

    fn init() ObjectFormat {
        return .{
            .state = .begin,
        };
    }

    fn onResume(self: *ObjectFormat, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .object_begin => {
                        self.state = .wait_object_key;
                        return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init() });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_object_key => {
                switch (ctx.popped().object_key.state) {
                    .no_key => {
                        self.state = .end;
                        return ctx.pop();
                    },
                    .end => |result| {
                        const str = result.key.items;
                        std.log.debug("{s}", .{str});
                        if (std.mem.eql(u8, str, "traceEvents")) {
                            self.state = .wait_json_value;
                            return ctx.yieldPush(.none, .{ .array_format = ArrayFormat.init() });
                        } else {
                            // Ignore unrecognized key
                            self.state = .wait_json_value;
                            return ctx.yieldPush(.none, .{ .json_value = JsonValue.init() });
                        }
                    },
                    else => unreachable,
                }
            },
            .wait_json_value => {
                self.state = .wait_object_key;
                return ctx.push(.{ .object_key = ObjectKey.init() });
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

    fn onResume(self: *ArrayFormat, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_begin => {
                        self.state = .wait_array_value;
                        return ctx.yieldPush(.none, .{ .array_value = ArrayValue.init() });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_array_value => {
                switch (ctx.popped().array_value.state) {
                    .no_value => return ctx.pop(),
                    .value => |value| {
                        _ = value;
                        return ctx.yieldPush(.none, .{ .array_value = ArrayValue.init() });
                    },
                    else => unreachable,
                }
            },
        }
    }
};

const ObjectKey = struct {
    state: union(enum) {
        begin,
        parse_string: struct {
            key: std.ArrayList(u8),
        },
        end: struct {
            key: std.ArrayList(u8),
        },
        no_key,
    },

    fn init() ObjectKey {
        return .{
            .state = .begin,
        };
    }

    fn parseString(self: *ObjectKey, ctx: *Context) ParseError!ControlFlow {
        var key = &self.state.parse_string.key;
        const token = ctx.token().*;
        switch (token) {
            .partial_string => |str| {
                try key.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_1 => |*str| {
                try key.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_2 => |*str| {
                try key.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_3 => |*str| {
                try key.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_4 => |*str| {
                try key.appendSlice(str);
                return ctx.yield(.none);
            },
            .string => |str| {
                try key.appendSlice(str);
                self.state = .{ .end = .{ .key = self.state.parse_string.key } };
                return ctx.pop();
            },
            else => {
                std.log.err("Unexpected token: {s}", .{@tagName(token)});
                return error.unexpected_token;
            },
        }
    }

    fn onResume(self: *ObjectKey, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .object_end => {
                        self.state = .no_key;
                        return ctx.pop();
                    },
                    else => {
                        self.state = .{
                            .parse_string = .{
                                .key = std.ArrayList(u8).init(ctx.allocator),
                            },
                        };
                        return self.parseString(ctx);
                    },
                }
            },
            .parse_string => return self.parseString(ctx),
            else => unreachable,
        }
    }

    fn deinit(self: *ObjectKey) void {
        switch (self.state) {
            .parse_string => |ps| {
                ps.key.deinit();
            },
            .end => |r| {
                r.key.deinit();
            },
            else => {},
        }
    }
};

const JsonValue = struct {
    value: std.json.Value,

    fn init() JsonValue {
        return .{ .value = .null };
    }

    fn onResume(self: *JsonValue, ctx: *Context) ParseError!ControlFlow {
        _ = self;
        return ctx.yield(.none);
    }
};

const ArrayValue = struct {
    state: union(enum) {
        begin,
        wait_json_value,
        value: std.json.Value,
        no_value,
    },

    fn init() ArrayValue {
        return .{ .state = .begin };
    }

    fn onResume(self: *ArrayValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_end => {
                        self.state = .no_value;
                        return ctx.pop();
                    },
                    else => {
                        self.state = .wait_json_value;
                        return ctx.push(.{ .json_value = JsonValue.init() });
                    },
                }
            },
            .wait_json_value => {
                const json_value = ctx.popped().json_value;
                self.state = .{ .value = json_value.value };
                return ctx.pop();
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

    pub fn onResume(self: *State, ctx: *Context) ParseError!ControlFlow {
        switch (self.*) {
            inline else => |*state| return state.onResume(ctx),
        }
    }
};

pub const JsonProfileParser = struct {
    allocator: Allocator,
    scanner: std.json.Scanner,
    state_machine: StateMachine,

    pub fn init(allocator: Allocator) JsonProfileParser {
        return .{
            .allocator = allocator,
            .scanner = std.json.Scanner.initStreaming(allocator),
            .state_machine = StateMachine.init(allocator, .{ .start = Start.init() }),
        };
    }

    pub fn deinit(self: *JsonProfileParser) void {
        self.scanner.deinit();
        self.state_machine.deinit();
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
            const result = try self.state_machine.run(token);
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
