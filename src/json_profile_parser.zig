const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;

const Context = struct {
    state: union(enum) {
        run: Token,
        pop: State,
        deinit,
    },
    trace_event_allocator: ?Allocator,

    pub fn forRun(t: Token, trace_event_allocator: Allocator) Context {
        return .{ .state = .{ .run = t }, .trace_event_allocator = trace_event_allocator };
    }

    pub fn forPop(popped_state: State, trace_event_allocator: Allocator) Context {
        return .{ .state = .{ .pop = popped_state }, .trace_event_allocator = trace_event_allocator };
    }

    pub fn forDeinit() Context {
        return .{ .state = .deinit, .trace_event_allocator = null };
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
            var ctx = Context.forDeinit();
            state.deinit(&ctx);
        }
        self.stack.deinit();
    }

    pub fn run(self: *StateMachine, token: Token, trace_event_allocator: Allocator) ParseError!ParseResult {
        // defer {
        //     std.log.debug("Parsing {}", .{token});
        //     for (0..self.stack.items.len) |i| {
        //         const at = self.stack.items.len - i - 1;
        //         const state = &self.stack.items[at];
        //         std.log.debug("    [{}] {s}", .{ at, @tagName(state.*) });
        //     }
        // }

        var ctx = Context.forRun(token, trace_event_allocator);
        while (true) {
            std.debug.assert(self.stack.items.len > 0);
            var top = &self.stack.items[self.stack.items.len - 1];
            const control_flow = top.onResume(&ctx);

            switch (ctx.state) {
                .pop => |*state| {
                    ctx = Context.forDeinit();
                    state.deinit(&ctx);
                },
                else => {},
            }

            switch (try control_flow) {
                .push => |state| {
                    try self.stack.append(state);
                    ctx = Context.forRun(token, trace_event_allocator);
                },
                .pop => {
                    const state = self.stack.pop();
                    ctx = Context.forPop(state, trace_event_allocator);
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

pub const TraceEvent = struct {
    name: ?[]u8,
    cat: ?[]u8,
    ph: ?[]u8,
    ts: ?i64,
    tss: ?i64,
    pid: ?i64,
    tid: ?i64,
    args: ?std.json.ObjectMap,
    cname: ?[]u8,

    pub fn deinit(self: *TraceEvent, allocator: Allocator) void {
        if (self.name) |name| {
            allocator.free(name);
        }
        if (self.cat) |cat| {
            allocator.free(cat);
        }
        if (self.ph) |ph| {
            allocator.free(ph);
        }
        if (self.args) |args| {
            freeJsonValue(allocator, .{ .object = args });
        }
        if (self.cname) |cname| {
            allocator.free(cname);
        }
    }
};

fn toTraceEvent(allocator: Allocator, object: std.json.ObjectMap) ParseError!TraceEvent {
    var obj = object;
    return TraceEvent{
        .name = try maybeTakeString(allocator, &obj, "name"),
        .cat = try maybeTakeString(allocator, &obj, "cat"),
        .ph = try maybeTakeString(allocator, &obj, "ph"),
        .ts = try maybeTakeI64(allocator, &obj, "ts"),
        .tss = try maybeTakeI64(allocator, &obj, "tss"),
        .pid = try maybeTakeI64(allocator, &obj, "pid"),
        .tid = try maybeTakeI64(allocator, &obj, "tid"),
        .args = try maybeTakeObj(allocator, &obj, "args"),
        .cname = try maybeTakeString(allocator, &obj, "cname"),
    };
}

fn maybeTakeString(allocator: Allocator, obj: *std.json.ObjectMap, key: []const u8) ParseError!?[]u8 {
    if (obj.fetchSwapRemove(key)) |kv| {
        defer allocator.free(kv.key);
        errdefer freeJsonValue(allocator, kv.value);
        switch (kv.value) {
            .string => |str| {
                return @constCast(str);
            },
            else => return error.unexpected_token,
        }
    }

    return null;
}

fn takeString(ctx: *Context, obj: *std.json.ObjectMap, key: []const u8) ParseError![]u8 {
    if (try maybeTakeString(ctx, obj, key)) |str| {
        return str;
    }

    return error.key_missing;
}

fn maybeTakeI64(allocator: Allocator, obj: *std.json.ObjectMap, key: []const u8) ParseError!?i64 {
    if (obj.fetchSwapRemove(key)) |kv| {
        defer allocator.free(kv.key);
        errdefer freeJsonValue(allocator, kv.value);
        switch (kv.value) {
            .number_string => |str| {
                return try std.fmt.parseInt(i64, str, 10);
            },
            else => return error.unexpected_token,
        }
    }

    return null;
}

fn takeI64(ctx: *Context, obj: *std.json.ObjectMap, key: []const u8) ParseError!i64 {
    if (try maybeTakeI64(ctx, obj, key)) |num| {
        return num;
    }

    return error.key_missing;
}

fn maybeTakeObj(allocator: Allocator, obj: *std.json.ObjectMap, key: []const u8) ParseError!?std.json.ObjectMap {
    if (obj.fetchSwapRemove(key)) |kv| {
        defer allocator.free(kv.key);
        errdefer freeJsonValue(allocator, kv.value);
        switch (kv.value) {
            .object => |o| {
                return o;
            },
            else => return error.unexpected_token,
        }
    }

    return null;
}

pub const ParseError = anyerror;
pub const ParseResult = union(enum) {
    trace_event: TraceEvent,
    none,
};

const Start = struct {
    state: enum {
        begin,
        wait,
        done,
    },
    allocator: Allocator,

    fn init(allocator: Allocator) Start {
        return .{
            .state = .begin,
            .allocator = allocator,
        };
    }

    fn onResume(self: *Start, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .object_begin => {
                        var object_foramt = .{ .object_format = ObjectFormat.init(self.allocator, ctx.trace_event_allocator.?) };
                        self.state = .wait;
                        return ctx.push(object_foramt);
                    },
                    .array_begin => {
                        var array_format = .{ .array_format = ArrayFormat.init(ctx.trace_event_allocator.?) };
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
    allocator: Allocator,
    trace_event_allocator: Allocator,
    state: union(enum) {
        begin,
        wait_object_key,
        wait_array_format,
        wait_json_value,
        end,
    },

    fn init(allocator: Allocator, trace_event_allocator: Allocator) ObjectFormat {
        return .{
            .allocator = allocator,
            .trace_event_allocator = trace_event_allocator,
            .state = .begin,
        };
    }

    fn onResume(self: *ObjectFormat, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .object_begin => {
                        self.state = .wait_object_key;
                        return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
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
                        defer self.allocator.free(str);

                        if (std.mem.eql(u8, str, "traceEvents")) {
                            self.state = .wait_array_format;
                            return ctx.yieldPush(.none, .{ .array_format = ArrayFormat.init(self.trace_event_allocator) });
                        } else {
                            // Ignore unrecognized key
                            self.state = .wait_json_value;
                            return ctx.yieldPush(.none, .{ .json_value = JsonValue.init(self.allocator) });
                        }
                    },
                    else => unreachable,
                }
            },
            .wait_array_format => {
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            .wait_json_value => {
                self.state = .wait_object_key;
                // TODO: Handle json value
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            else => unreachable,
        }
    }
};

const ArrayFormat = struct {
    trace_event_allocator: Allocator,
    state: union(enum) {
        begin,
        wait_array_item,
    },

    fn init(trace_event_allocator: Allocator) ArrayFormat {
        return .{
            .state = .begin,
            .trace_event_allocator = trace_event_allocator,
        };
    }

    fn onResume(self: *ArrayFormat, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_begin => {
                        self.state = .wait_array_item;
                        return ctx.yieldPush(.none, .{ .array_item = ArrayItem.init(self.trace_event_allocator) });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_array_item => {
                switch (ctx.popped().array_item.state) {
                    .no_value => return ctx.pop(),
                    .value => |value| {
                        if (value == .object) {
                            // // TODO: Handle value
                            var trace_event = try toTraceEvent(self.trace_event_allocator, value.object);
                            // std.log.debug("Trace Event: {any}", .{trace_event});
                            return ctx.yieldPush(.{ .trace_event = trace_event }, .{ .array_item = ArrayItem.init(self.trace_event_allocator) });
                        } else {
                            freeJsonValue(self.trace_event_allocator, value);
                            return ctx.yieldPush(.none, .{ .array_item = ArrayItem.init(self.trace_event_allocator) });
                        }
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
        .array => |*arr| {
            for (arr.items) |item| {
                freeJsonValue(allocator, item);
            }
            arr.deinit();
        },
        .object => |obj| {
            var o = obj;
            while (o.popOrNull()) |*kv| {
                freeJsonValue(allocator, kv.value);
                allocator.free(kv.key);
            }
            o.deinit();
        },
        else => {},
    }
}

const ObjectKey = struct {
    allocator: Allocator,
    state: union(enum) {
        begin,
        wait_string_value,
        key: []u8,
        no_key,
    },

    fn init(allocator: Allocator) ObjectKey {
        return .{
            .allocator = allocator,
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
                        return ctx.push(.{ .string_value = StringValue.init(self.allocator) });
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

    fn init(allocator: Allocator) StringValue {
        return .{
            .str = std.ArrayList(u8).init(allocator),
        };
    }

    fn onResume(self: *StringValue, ctx: *Context) ParseError!ControlFlow {
        switch (ctx.token().*) {
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
                return error.unexpected_token;
            },
        }
    }
};

const NumberValue = struct {
    num: std.ArrayList(u8),

    fn init(allocator: Allocator) NumberValue {
        return .{
            .num = std.ArrayList(u8).init(allocator),
        };
    }

    fn onResume(self: *NumberValue, ctx: *Context) ParseError!ControlFlow {
        switch (ctx.token().*) {
            .partial_number => |num| {
                try self.num.appendSlice(num);
                return ctx.yield(.none);
            },
            .number => |num| {
                try self.num.appendSlice(num);
                return ctx.pop();
            },
            else => return error.unexpected_token,
        }
    }

    fn deinit(self: *NumberValue, ctx: *Context) void {
        _ = ctx;
        self.num.deinit();
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
    allocator: Allocator,
    object: std.json.ObjectMap,

    fn init(allocator: Allocator) ObjectValue {
        return .{
            .state = .begin,
            .allocator = allocator,
            .object = std.json.ObjectMap.init(allocator),
        };
    }

    fn onResume(self: *ObjectValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                const token = ctx.token().*;
                switch (token) {
                    .object_begin => {
                        self.state = .wait_object_key;
                        return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
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
                        return ctx.yieldPush(.none, .{ .json_value = JsonValue.init(self.allocator) });
                    },
                    else => unreachable,
                }
            },
            .wait_json_value => |s| {
                const json_value = ctx.popped().json_value;
                try self.object.put(s.key, json_value.state.value);
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            else => unreachable,
        }
    }

    fn deinit(self: *ObjectValue, ctx: *Context) void {
        _ = ctx;
        switch (self.state) {
            .wait_json_value => |s| {
                self.allocator.free(s.key);
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
    allocator: Allocator,
    state: union(enum) {
        begin,
        wait_string_value,
        wait_object_value,
        wait_array_value,
        wait_number_value,
        value: std.json.Value,
    },

    fn init(allocator: Allocator) JsonValue {
        return .{
            .allocator = allocator,
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
                        return ctx.push(.{ .string_value = StringValue.init(self.allocator) });
                    },

                    .object_begin => {
                        self.state = .wait_object_value;
                        return ctx.push(.{ .object_value = ObjectValue.init(self.allocator) });
                    },

                    .array_begin => {
                        self.state = .wait_array_value;
                        return ctx.push(.{ .array_value = ArrayValue.init(self.allocator) });
                    },

                    .true => {
                        self.state = .{ .value = .{ .bool = true } };
                        return ctx.pop();
                    },
                    .false => {
                        self.state = .{ .value = .{ .bool = false } };
                        return ctx.pop();
                    },
                    .null => {
                        self.state = .{ .value = .null };
                        return ctx.pop();
                    },

                    .partial_number, .number => {
                        self.state = .wait_number_value;
                        return ctx.push(.{ .number_value = NumberValue.init(self.allocator) });
                    },

                    else => return error.unexpected_token,
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
            .wait_array_value => {
                const arr = ctx.popped().array_value.array;
                self.state = .{ .value = .{ .array = arr } };
                return ctx.pop();
            },
            .wait_number_value => {
                var num = ctx.popped().number_value.num;
                self.state = .{ .value = .{ .number_string = try num.toOwnedSlice() } };
                return ctx.pop();
            },
            else => unreachable,
        }
    }
};

const ArrayItem = struct {
    allocator: Allocator,
    state: union(enum) {
        begin,
        wait_json_value,
        value: std.json.Value,
        no_value,
    },

    fn init(allocator: Allocator) ArrayItem {
        return .{ .state = .begin, .allocator = allocator };
    }

    fn onResume(self: *ArrayItem, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_end => {
                        self.state = .no_value;
                        return ctx.pop();
                    },
                    else => {
                        self.state = .wait_json_value;
                        return ctx.push(.{ .json_value = JsonValue.init(self.allocator) });
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

const ArrayValue = struct {
    state: union(enum) {
        begin,
        wait_array_item,
        value,
    },
    allocator: Allocator,
    array: std.json.Array,

    fn init(allocator: Allocator) ArrayValue {
        return .{
            .state = .begin,
            .allocator = allocator,
            .array = std.json.Array.init(allocator),
        };
    }

    fn onResume(self: *ArrayValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_begin => {
                        self.state = .wait_array_item;
                        return ctx.yieldPush(.none, .{ .array_item = ArrayItem.init(self.allocator) });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_array_item => {
                switch (ctx.popped().array_item.state) {
                    .no_value => {
                        self.state = .value;
                        return ctx.pop();
                    },
                    .value => |value| {
                        try self.array.append(value);
                        self.state = .wait_array_item;
                        return ctx.yieldPush(.none, .{ .array_item = ArrayItem.init(self.allocator) });
                    },
                    else => unreachable,
                }
            },
            else => unreachable,
        }
    }

    fn deinit(self: *ArrayValue, ctx: *Context) void {
        _ = ctx;
        if (self.state != .value) {
            self.array.deinit();
        }
    }
};

const State = union(enum) {
    start: Start,

    object_format: ObjectFormat,
    array_format: ArrayFormat,

    json_value: JsonValue,
    array_value: ArrayValue,
    array_item: ArrayItem,
    object_value: ObjectValue,
    object_key: ObjectKey,
    string_value: StringValue,
    number_value: NumberValue,

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
            .state_machine = StateMachine.init(allocator, .{ .start = Start.init(allocator) }),
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

    pub fn next(self: *JsonProfileParser, trace_event_allocator: Allocator) ParseError!ParseResult {
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
            const result = try self.state_machine.run(token, trace_event_allocator);
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
    var allocator = std.testing.allocator;
    var parser = JsonProfileParser.init(allocator);
    parser.feedInput(input);
    parser.endInput();
    defer parser.deinit();

    var results = std.ArrayList(ParseResult).init(std.testing.allocator);
    errdefer results.deinit();

    var trace_event_allocator = std.heap.ArenaAllocator.init(allocator);
    defer trace_event_allocator.deinit();
    while (true) {
        const result = try parser.next(trace_event_allocator.allocator());
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
    defer std.testing.allocator.free(results);

    try std.testing.expect(results.len == 1);
}
