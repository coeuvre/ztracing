const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;

// State
//   ctx, token
//

pub const TraceEvent = union(enum) {};

pub const ParseError = anyerror;
pub const ParseResult = union(enum) {
    trace: TraceEvent,
    none,
};

const StateResult = union(enum) {
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

    pub fn consumeToken(self: *Context, result: ParseResult) void {
        self.result = result;
    }

    pub fn push(self: *Context, state: State) void {
        self.stack.append(state) catch unreachable;
    }

    pub fn pop(self: *Context, result: StateResult) ParseError!void {
        _ = self.stack.pop();
        if (self.stack.items.len > 0) {
            try self.stack.items[self.stack.items.len - 1].onPopResult(self, result);
        }
    }
};

const Start = struct {
    fn parse(self: *@This(), ctx: *Context, token: Token) ParseError!void {
        _ = self;
        switch (token) {
            .object_begin => {
                var object_foramt = State.objectFormat();
                ctx.push(object_foramt);
            },
            .array_begin => {
                var array_format = State.arrayFormat();
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
    state: union(enum) {
        start,
        object_key: struct {
            key: ?[]u8,
        },
    },

    fn parse(self: *@This(), ctx: *Context, token: Token) ParseError!void {
        _ = token;
        switch (self.state) {
            .start => {
                ctx.push(State.objectKey());
                self.state = .{ .object_key = .{ .key = null } };
                ctx.consumeToken(.none);
            },
            .object_key => |*object_key| {
                std.debug.assert(object_key.key != null);
            },
        }
    }

    fn onPopResult(self: *@This(), ctx: *Context, result: StateResult) ParseError!void {
        switch (self.state) {
            .object_key => |*object_key| {
                switch (result) {
                    .string => |str| {
                        object_key.key = str;
                    },
                    .none => {
                        try ctx.pop(.none);
                    },
                }
            },
            else => return error.expected_pop_result,
        }
    }
};

const ArrayFormat = struct {
    fn parse(self: *@This(), ctx: *Context, token: Token) ParseError!void {
        _ = token;
        _ = self;
        ctx.consumeToken(.none);
    }
};

const ObjectKey = struct {
    fn parse(self: *@This(), ctx: *Context, token: Token) ParseError!void {
        _ = self;
        switch (token) {
            .object_end => {
                try ctx.pop(.none);
                ctx.consumeToken(.none);
            },
            else => {
                std.log.err("Unexpected token: {s}", .{@tagName(token)});
                return error.unexpected_token;
            },
        }
    }
};

const State = union(enum) {
    start: Start,
    object_format: ObjectFormat,
    array_format: ArrayFormat,
    object_key: ObjectKey,

    pub fn start() State {
        return .{ .start = .{} };
    }

    pub fn objectFormat() State {
        return .{
            .object_format = .{
                .state = .start,
            },
        };
    }

    pub fn arrayFormat() State {
        return .{ .array_format = .{} };
    }

    pub fn objectKey() State {
        return .{ .object_key = .{} };
    }

    pub fn deinit(self: *State) void {
        switch (self.*) {
            inline else => |*state| {
                if (@hasDecl(@TypeOf(state.*), "deinit")) {
                    state.deinit();
                }
            },
        }
    }

    pub fn parse(self: *State, ctx: *Context, token: Token) ParseError!void {
        switch (self.*) {
            inline else => |*state| return state.parse(ctx, token),
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
    ctx: Context,

    pub fn init(allocator: Allocator) JsonProfileParser {
        var ctx = Context.init(allocator);
        ctx.push(State.start());
        return .{
            .allocator = allocator,
            .ctx = ctx,
        };
    }

    pub fn parse(self: *JsonProfileParser, token: std.json.Token) ParseError!ParseResult {
        self.ctx.result = null;
        while (self.ctx.result == null and self.ctx.stack.items.len > 0) {
            var top_state = &self.ctx.stack.items[self.ctx.stack.items.len - 1];
            try top_state.parse(&self.ctx, token);
        }
        // for (0..self.ctx.stack.items.len) |i| {
        //     const state = &self.ctx.stack.items[self.ctx.stack.items.len - i - 1];
        //     std.log.debug("{s}", .{@tagName(state.*)});
        // }
        if (self.ctx.result) |result| {
            return result;
        }
        return .none;
    }
};
