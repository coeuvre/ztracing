const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;

const Context = struct {
    allocator: Allocator,
    state: union(enum) {
        run: Token,
        pop: State,
        deinit,
    },

    pub fn forRun(allocator: Allocator, t: Token) Context {
        return .{ .allocator = allocator, .state = .{ .run = t } };
    }

    pub fn forPop(allocator: Allocator, popped_state: State) Context {
        return .{ .allocator = allocator, .state = .{ .pop = popped_state } };
    }

    pub fn forDeinit(allocator: Allocator) Context {
        return .{ .allocator = allocator, .state = .deinit };
    }

    pub fn push(self: *Context, state: State) ControlFlow {
        std.debug.assert(self.state == .run);
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
            var ctx = Context.forDeinit(self.allocator);
            state.deinit(&ctx);
        }
        self.stack.deinit();
    }

    pub fn run(self: *StateMachine, token: Token) ParseError!ParseResult {
        defer {
            std.log.debug("Parsing {}", .{token});
            for (0..self.stack.items.len) |i| {
                const at = self.stack.items.len - i - 1;
                const state = &self.stack.items[at];
                std.log.debug("    [{}] {s}", .{ at, @tagName(state.*) });
            }
        }

        var ctx = Context.forRun(self.allocator, token);
        while (true) {
            std.debug.assert(self.stack.items.len > 0);
            var top = &self.stack.items[self.stack.items.len - 1];
            const control_flow = top.onResume(&ctx);

            switch (ctx.state) {
                .pop => |*state| {
                    ctx = Context.forDeinit(self.allocator);
                    state.deinit(&ctx);
                },
                else => {},
            }

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
        done,
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
                self.state = .done;
                return ctx.yield(.none);
            },
            .done => {
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
        wait_array_format,
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
                    .key => |str| {
                        defer ctx.allocator.free(str);

                        std.log.debug("{s}", .{str});
                        if (std.mem.eql(u8, str, "traceEvents")) {
                            self.state = .wait_array_format;
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
            .wait_array_format => {
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init() });
            },
            .wait_json_value => {
                self.state = .wait_object_key;
                // TODO: Handle json value
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init() });
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
                        // TODO: Handle value
                        std.log.debug("Trace Event: {}", .{JsonValueWrapper{ .value = value }});

                        if (value == .object) {
                            // TODO: emit trace event
                        } else {
                            freeJsonValue(ctx.allocator, value);
                        }

                        return ctx.yieldPush(.none, .{ .array_value = ArrayValue.init() });
                    },
                    else => unreachable,
                }
            },
        }
    }
};

const JsonValueWrapper = struct {
    value: std.json.Value,

    pub fn format(self: JsonValueWrapper, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = options;
        _ = fmt;
        return self.value.jsonStringify(.{}, writer);
    }
};

fn freeJsonValue(allocator: Allocator, value: std.json.Value) void {
    switch (value) {
        .string => |str| {
            allocator.free(str);
        },
        else => unreachable,
    }
}

const ObjectKey = struct {
    state: union(enum) {
        begin,
        wait_string_value,
        key: []u8,
        no_key,
    },

    fn init() ObjectKey {
        return .{
            .state = .begin,
        };
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
                        self.state = .wait_string_value;
                        return ctx.push(.{ .string_value = StringValue.init(ctx) });
                    },
                }
            },
            .wait_string_value => {
                var string_value = ctx.popped().string_value;
                const key = try string_value.str.toOwnedSlice();
                self.state = .{ .key = key };
                return ctx.pop();
            },
            else => unreachable,
        }
    }
};

const StringValue = struct {
    str: std.ArrayList(u8),

    fn init(ctx: *Context) StringValue {
        return .{
            .str = std.ArrayList(u8).init(ctx.allocator),
        };
    }

    fn onResume(self: *StringValue, ctx: *Context) ParseError!ControlFlow {
        const token = ctx.token().*;
        switch (token) {
            .partial_string => |str| {
                try self.str.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_1 => |*str| {
                try self.str.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_2 => |*str| {
                try self.str.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_3 => |*str| {
                try self.str.appendSlice(str);
                return ctx.yield(.none);
            },
            .partial_string_escaped_4 => |*str| {
                try self.str.appendSlice(str);
                return ctx.yield(.none);
            },
            .string => |str| {
                try self.str.appendSlice(str);
                return ctx.pop();
            },
            else => {
                std.log.err("Unexpected token: {}", .{token});
                return error.unexpected_token;
            },
        }
    }
};

const ObjectValue = struct {
    state: union(enum) {
        begin,
        wait_object_key,
        wait_json_value: struct {
            key: []u8,
        },
        value,
    },
    object: std.json.ObjectMap,

    fn init(ctx: *Context) ObjectValue {
        return .{ .state = .begin, .object = std.json.ObjectMap.init(ctx.allocator) };
    }

    fn onResume(self: *ObjectValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                const token = ctx.token().*;
                switch (token) {
                    .object_begin => {
                        self.state = .wait_object_key;
                        return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init() });
                    },
                    else => {
                        std.log.err("Unexpected token: {}", .{token});
                        return error.unexpected_token;
                    },
                }
            },
            .wait_object_key => {
                const object_key = ctx.popped().object_key;
                switch (object_key.state) {
                    .no_key => {
                        self.state = .value;
                        return ctx.pop();
                    },
                    .key => |key| {
                        self.state = .{ .wait_json_value = .{ .key = key } };
                        return ctx.yieldPush(.none, .{ .json_value = JsonValue.init() });
                    },
                    else => unreachable,
                }
            },
            .wait_json_value => |s| {
                const json_value = ctx.popped().json_value;
                try self.object.put(s.key, json_value.state.value);
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init() });
            },
            else => unreachable,
        }
    }

    fn deinit(self: *ObjectValue, ctx: *Context) void {
        switch (self.state) {
            .wait_json_value => |s| {
                ctx.allocator.free(s.key);
            },
            else => {
                if (self.state != .value) {
                    self.object.deinit();
                }
            },
        }
    }
};

const JsonValue = struct {
    state: union(enum) {
        begin,
        wait_string_value,
        wait_object_value,
        value: std.json.Value,
    },

    fn init() JsonValue {
        return .{
            .state = .begin,
        };
    }

    fn onResume(self: *JsonValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .partial_string,
                    .partial_string_escaped_1,
                    .partial_string_escaped_2,
                    .partial_string_escaped_3,
                    .partial_string_escaped_4,
                    .string,
                    => {
                        self.state = .wait_string_value;
                        return ctx.push(.{ .string_value = StringValue.init(ctx) });
                    },

                    .object_begin => {
                        self.state = .wait_object_value;
                        return ctx.push(.{ .object_value = ObjectValue.init(ctx) });
                    },

                    else => return ctx.yield(.none),
                }
            },
            .wait_string_value => {
                const str = try ctx.popped().string_value.str.toOwnedSlice();
                self.state = .{ .value = .{ .string = str } };
                return ctx.pop();
            },
            .wait_object_value => {
                const obj = ctx.popped().object_value.object;
                self.state = .{ .value = .{ .object = obj } };
                return ctx.pop();
            },
            else => unreachable,
        }
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
                const value = ctx.popped().json_value.state.value;
                self.state = .{ .value = value };
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
    object_value: ObjectValue,
    string_value: StringValue,

    pub fn deinit(self: *State, ctx: *Context) void {
        switch (self.*) {
            inline else => |*state| {
                if (@hasDecl(@TypeOf(state.*), "deinit")) {
                    state.deinit(ctx);
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

test "invalid trace events" {
    const results = try parseAll(
        \\{
        \\    "traceEvents": [
        \\        "a"
        \\    ]
        \\}
        \\
    );
    try std.testing.expect(results.len == 0);
}

test "empty profile" {
    const results = try parseAll("{}");
    try std.testing.expect(results.len == 0);
}

test "empty traceEvents profile" {
    const results = try parseAll("{\"traceEvents\":[]}");
    try std.testing.expect(results.len == 0);
}

test "simple traceEvents profile" {
    const results = try parseAll("{\"traceEvents\":[{\"a\":\"b\"}]}");
    try std.testing.expect(results.len == 0);
}
