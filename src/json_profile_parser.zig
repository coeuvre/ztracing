const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;
const ArenaAllocator = std.heap.ArenaAllocator;

const Buffer = std.ArrayList(u8);

const BufOrRef = struct {
    buf: Buffer,
    ref: []const u8,

    pub fn init(allocator: Allocator) BufOrRef {
        return .{
            .buf = Buffer.init(allocator),
            .ref = &[_]u8{},
        };
    }

    pub fn appendSlice(self: *BufOrRef, slice: []const u8) !void {
        return self.buf.appendSlice(slice);
    }

    pub fn setRef(self: *BufOrRef, ref: []const u8) void {
        std.debug.assert(self.buf.items.len == 0);
        self.ref = ref;
    }

    pub fn deinit(self: *BufOrRef) void {
        self.buf.deinit();
    }

    pub fn toOwnedSlice(self: *BufOrRef) ![]u8 {
        if (self.buf.items.len > 0) {
            return self.buf.toOwnedSlice();
        }
        return self.buf.allocator.dupe(u8, self.ref);
    }

    pub fn items(self: *const BufOrRef) []const u8 {
        if (self.buf.items.len == 0) {
            return self.ref;
        }
        return self.buf.items;
    }
};

const Context = struct {
    state: union(enum) {
        run: Token,
        pop: State,
    },

    pub fn forRun(t: Token) Context {
        return .{ .state = .{ .run = t } };
    }

    pub fn forPop(popped_state: State) Context {
        return .{ .state = .{ .pop = popped_state } };
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
            state.deinit();
        }
        self.stack.deinit();
    }

    pub fn run(self: *StateMachine, token: Token) ParseError!ParseResult {
        // {
        //     std.log.info("Parsing {}", .{token});
        //     for (0..self.stack.items.len) |i| {
        //         const at = self.stack.items.len - i - 1;
        //         const state = &self.stack.items[at];
        //         std.log.info("    [{}] {s}", .{ at, @tagName(state.*) });
        //     }
        // }

        var ctx = Context.forRun(token);
        while (true) {
            std.debug.assert(self.stack.items.len > 0);
            var top = &self.stack.items[self.stack.items.len - 1];
            const control_flow = top.onResume(&ctx);

            switch (ctx.state) {
                .pop => |*state| {
                    state.deinit();
                },
                else => {},
            }

            switch (try control_flow) {
                .push => |state| {
                    try self.stack.append(state);
                    ctx = Context.forRun(token);
                },
                .pop => {
                    const state = self.stack.pop();
                    ctx = Context.forPop(state);
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
    name: ?[]const u8 = null,
    cat: ?[]const u8 = null,
    ph: ?[]const u8 = null,
    ts: ?i64 = null,
    tss: ?i64 = null,
    pid: ?i64 = null,
    tid: ?i64 = null,
    args: ?std.json.ObjectMap = null,
    cname: ?[]const u8 = null,
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
                        var object_foramt = .{ .object_format = ObjectFormat.init(self.allocator) };
                        self.state = .wait;
                        return ctx.push(object_foramt);
                    },
                    .array_begin => {
                        var array_format = .{ .array_format = ArrayFormat.init(self.allocator) };
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
    state: union(enum) {
        begin,
        wait_object_key,
        wait_array_format,
        wait_json_value,
        end,
    },

    fn init(allocator: Allocator) ObjectFormat {
        return .{
            .allocator = allocator,
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
                var object_key = ctx.popped().object_key;
                switch (object_key.state) {
                    .no_key => {
                        self.state = .end;
                        return ctx.pop();
                    },
                    .key => |*key| {
                        defer key.deinit();

                        // std.log.info("{s} ", .{key});
                        if (std.mem.eql(u8, key.items(), "traceEvents")) {
                            self.state = .wait_array_format;
                            return ctx.yieldPush(.none, .{ .array_format = ArrayFormat.init(self.allocator) });
                        } else {
                            // Ignore unrecognized key
                            self.state = .wait_json_value;
                            return ctx.yieldPush(.none, .{ .json_value = JsonValue.init(self.allocator) });
                        }
                    },
                    else => {
                        unreachable;
                    },
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

const TraceEventValue = struct {
    state: enum {
        begin,
        wait_object_key,
        wait_name,
        wait_json_value,
        no_value,
        value,
    },
    allocator: Allocator,
    value: TraceEvent,

    fn init(allocator: Allocator) TraceEventValue {
        return .{
            .state = .begin,
            .allocator = allocator,
            .value = TraceEvent{},
        };
    }

    fn onResume(self: *TraceEventValue, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_end => {
                        self.state = .no_value;
                        return ctx.pop();
                    },
                    .object_begin => {
                        self.state = .wait_object_key;
                        return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_object_key => {
                var object_key = ctx.popped().object_key;
                switch (object_key.state) {
                    .no_key => {
                        self.state = .value;
                        return ctx.pop();
                    },
                    .key => |*key| {
                        defer key.deinit();

                        if (std.mem.eql(u8, key.items(), "name")) {
                            self.state = .wait_name;
                            return ctx.yieldPush(.none, .{ .string_value = StringValue.init(self.allocator) });
                        } else {
                            self.state = .wait_json_value;
                            return ctx.yieldPush(.none, .{ .json_value = JsonValue.init(self.allocator) });
                        }
                    },
                    else => unreachable,
                }
            },
            .wait_name => {
                const val = ctx.popped().string_value.value();
                self.value.name = val.items();
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            .wait_json_value => {
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            else => unreachable,
        }
    }
};

const ArrayFormat = struct {
    state: union(enum) {
        begin,
        wait_trace_event,
        cleanup,
    },
    allocator: Allocator,
    arena: ArenaAllocator,

    fn init(allocator: Allocator) ArrayFormat {
        return .{
            .state = .begin,
            .allocator = allocator,
            .arena = ArenaAllocator.init(allocator),
        };
    }

    fn onResume(self: *ArrayFormat, ctx: *Context) ParseError!ControlFlow {
        switch (self.state) {
            .begin => {
                switch (ctx.token().*) {
                    .array_begin => {
                        self.state = .wait_trace_event;
                        return ctx.yieldPush(.none, .{ .trace_event_value = TraceEventValue.init(self.arena.allocator()) });
                    },
                    else => return error.unexpected_token,
                }
            },
            .wait_trace_event => {
                const trace_event_value = ctx.popped().trace_event_value;
                switch (trace_event_value.state) {
                    .no_value => return ctx.pop(),
                    .value => {
                        self.state = .cleanup;
                        return ctx.yield(.{ .trace_event = trace_event_value.value });
                    },
                    else => unreachable,
                }
            },
            .cleanup => {
                _ = self.arena.reset(.retain_capacity);
                self.state = .wait_trace_event;
                return ctx.push(.{ .trace_event_value = TraceEventValue.init(self.arena.allocator()) });
            },
        }
    }

    pub fn deinit(self: *ArrayFormat) void {
        self.arena.deinit();
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
    state: union(enum) {
        begin,
        wait_string_value,
        key: BufOrRef,
        no_key,
    },
    allocator: Allocator,

    fn init(allocator: Allocator) ObjectKey {
        return .{
            .state = .begin,
            .allocator = allocator,
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
                const val = ctx.popped().string_value.value();
                self.state = .{ .key = val };
                return ctx.pop();
            },
            else => unreachable,
        }
    }
};

const StringValue = struct {
    val: BufOrRef,
    partial: bool,
    state: enum {
        parsing,
        value,
    },

    fn init(allocator: Allocator) StringValue {
        return .{
            .val = BufOrRef.init(allocator),
            .partial = false,
            .state = .parsing,
        };
    }

    fn onResume(self: *StringValue, ctx: *Context) ParseError!ControlFlow {
        switch (ctx.token().*) {
            .partial_string => |str| {
                try self.val.appendSlice(str);
                self.partial = true;
                return ctx.yield(.none);
            },
            .partial_string_escaped_1 => |*str| {
                try self.val.appendSlice(str);
                self.partial = true;
                return ctx.yield(.none);
            },
            .partial_string_escaped_2 => |*str| {
                try self.val.appendSlice(str);
                self.partial = true;
                return ctx.yield(.none);
            },
            .partial_string_escaped_3 => |*str| {
                try self.val.appendSlice(str);
                self.partial = true;
                return ctx.yield(.none);
            },
            .partial_string_escaped_4 => |*str| {
                try self.val.appendSlice(str);
                self.partial = true;
                return ctx.yield(.none);
            },
            .string => |str| {
                if (self.partial) {
                    try self.val.appendSlice(str);
                } else {
                    self.val.setRef(str);
                }
                self.state = .value;
                return ctx.pop();
            },
            else => {
                return error.unexpected_token;
            },
        }
    }

    pub fn value(self: *StringValue) BufOrRef {
        return self.val;
    }

    pub fn deinit(self: *StringValue) void {
        if (self.state == .parsing) {
            self.val.deinit();
        }
    }
};

const NumberValue = struct {
    buf: Buffer,

    fn init(allocator: Allocator) NumberValue {
        return .{ .buf = Buffer.init(allocator) };
    }

    fn onResume(self: *NumberValue, ctx: *Context) ParseError!ControlFlow {
        switch (ctx.token().*) {
            .partial_number => |num| {
                try self.buf.appendSlice(num);
                return ctx.yield(.none);
            },
            .number => |num| {
                try self.buf.appendSlice(num);
                return ctx.pop();
            },
            else => return error.unexpected_token,
        }
    }

    pub fn value(self: *NumberValue) ![]u8 {
        return self.buf.toOwnedSlice();
    }

    pub fn deinit(self: *NumberValue) void {
        self.buf.deinit();
    }
};

const ObjectValue = struct {
    state: union(enum) {
        begin,
        wait_object_key,
        wait_json_value: struct {
            key: BufOrRef,
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
            .wait_json_value => |*s| {
                const json_value = ctx.popped().json_value;
                try self.object.put(try s.key.toOwnedSlice(), json_value.state.value);
                self.state = .wait_object_key;
                return ctx.yieldPush(.none, .{ .object_key = ObjectKey.init(self.allocator) });
            },
            else => unreachable,
        }
    }

    fn deinit(self: *ObjectValue) void {
        switch (self.state) {
            .wait_json_value => |*s| {
                s.key.deinit();
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
                var val = ctx.popped().string_value.value();
                self.state = .{ .value = .{ .string = try val.toOwnedSlice() } };
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
                const str = try ctx.popped().number_value.value();
                self.state = .{ .value = .{ .number_string = str } };
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

    fn deinit(self: *ArrayValue) void {
        if (self.state != .value) {
            self.array.deinit();
        }
    }
};

const State = union(enum) {
    start: Start,

    object_format: ObjectFormat,
    array_format: ArrayFormat,
    trace_event_value: TraceEventValue,

    json_value: JsonValue,
    array_value: ArrayValue,
    array_item: ArrayItem,
    object_value: ObjectValue,
    object_key: ObjectKey,
    string_value: StringValue,
    number_value: NumberValue,

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
    var allocator = std.testing.allocator;
    var parser = JsonProfileParser.init(allocator);
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
    defer std.testing.allocator.free(results);

    try std.testing.expect(results.len == 1);
}
