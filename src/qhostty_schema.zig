//! Writes a JSON description of all configuration options for the Qhostty
//! settings window: key, kind, values, repeatability and documentation.
//! The build runs it, so the description always matches Config.zig.
//! Default values come from `+show-config --default` at runtime.
const std = @import("std");
const Config = @import("config/Config.zig");
const help_strings = @import("help_strings");
const input = @import("input.zig");

pub fn main(init: std.process.Init) !void {
    var buffer: [4096]u8 = undefined;
    var stdout_writer = std.Io.File.stdout().writer(init.io, &buffer);
    const stdout = &stdout_writer.interface;
    try write(stdout);
    try stdout.flush();
}

/// The kinds the settings window knows. Types without a kind here are
/// "text": the window shows the raw value.
fn kind(comptime T: type) []const u8 {
    return switch (@typeInfo(T)) {
        .optional => |info| kind(info.child),
        .bool => "bool",
        .int => "int",
        .float => "float",
        .@"enum" => "enum",
        .pointer => |info| if (info.child == u8) "string" else "text",
        .@"struct" => |info| if (isFlags(info)) "flags" else named(T),
        .@"union" => named(T),
        else => "text",
    };
}

fn isFlags(comptime info: std.builtin.Type.Struct) bool {
    if (info.layout != .@"packed") return false;
    for (info.field_types) |T| {
        if (T != bool) return false;
    }
    return true;
}

/// The type name without its namespace and parameters, for example
/// "Limit" for "config.limit.Limit(usize,50000000)".
fn shortName(comptime T: type) []const u8 {
    const full = @typeName(T);
    const name = full[0 .. std.mem.indexOfScalar(u8, full, '(') orelse full.len];
    const dot = std.mem.lastIndexOfScalar(u8, name, '.') orelse
        return name;
    return name[dot + 1 ..];
}

fn named(comptime T: type) []const u8 {
    const name = shortName(T);
    const kinds = .{
        .{ "Color", "color" },
        .{ "TerminalColor", "color" },
        .{ "Duration", "duration" },
        .{ "Palette", "palette" },
        .{ "Keybinds", "keybind" },
        .{ "Theme", "theme" },
        .{ "Path", "path" },
        .{ "RepeatablePath", "path" },
        .{ "RepeatableString", "string" },
    };
    inline for (kinds) |entry| {
        if (comptime std.mem.eql(u8, name, entry[0])) return entry[1];
    }
    return "text";
}

/// Repeatable options can appear more than once in a config file.
fn repeatable(comptime T: type) bool {
    const Base = switch (@typeInfo(T)) {
        .optional => |info| info.child,
        else => T,
    };
    switch (@typeInfo(Base)) {
        .@"struct", .@"union" => {},
        else => return false,
    }
    const name = shortName(Base);
    return std.mem.startsWith(u8, name, "Repeatable") or
        std.mem.eql(u8, name, "Keybinds") or
        std.mem.eql(u8, name, "Palette");
}

/// The value names of enums and flags, for dropdowns and checkboxes.
fn valueNames(comptime T: type) []const []const u8 {
    const Base = switch (@typeInfo(T)) {
        .optional => |info| info.child,
        else => T,
    };
    return switch (@typeInfo(Base)) {
        .@"enum" => std.meta.fieldNames(Base),
        .@"struct" => |info| if (isFlags(info)) std.meta.fieldNames(Base) else &.{},
        else => &.{},
    };
}

fn writeString(writer: *std.Io.Writer, value: []const u8) !void {
    try writer.writeByte('"');
    for (value) |byte| switch (byte) {
        '"' => try writer.writeAll("\\\""),
        '\\' => try writer.writeAll("\\\\"),
        '\n' => try writer.writeAll("\\n"),
        '\t' => try writer.writeAll("\\t"),
        0...8, 11...31, 127 => try writer.print("\\u{x:0>4}", .{byte}),
        else => try writer.writeByte(byte),
    };
    try writer.writeByte('"');
}

fn write(writer: *std.Io.Writer) !void {
    @setEvalBranchQuota(100_000);
    try writer.writeAll("{\"version\":1,\"options\":[");

    // Fields without documentation share the documentation of the
    // previous field, as on the website.
    var docs: []const u8 = "";
    var first = true;
    const config = @typeInfo(Config).@"struct";
    inline for (config.field_names, config.field_types) |name, Field| {
        if (name[0] == '_') continue;
        if (@hasDecl(help_strings.Config, name)) {
            docs = @field(help_strings.Config, name);
        }

        if (!first) try writer.writeByte(',');
        first = false;

        try writer.writeAll("\n{\"key\":");
        try writeString(writer, name);
        try writer.writeAll(",\"kind\":");
        try writeString(writer, comptime kind(Field));
        try writer.writeAll(",\"type\":");
        try writeString(writer, comptime shortName(Field));
        try writer.print(",\"optional\":{},\"repeatable\":{}", .{
            @typeInfo(Field) == .optional,
            comptime repeatable(Field),
        });

        const Base = switch (@typeInfo(Field)) {
            .optional => |info| info.child,
            else => Field,
        };
        if (@typeInfo(Base) == .int) {
            try writer.print(",\"min\":{d},\"max\":{d}", .{
                std.math.minInt(Base),
                std.math.maxInt(Base),
            });
        }

        const values = comptime valueNames(Field);
        if (values.len > 0) {
            try writer.writeAll(",\"values\":[");
            inline for (values, 0..) |value, i| {
                if (i > 0) try writer.writeByte(',');
                try writeString(writer, value);
            }
            try writer.writeByte(']');
        }

        try writer.writeAll(",\"docs\":");
        try writeString(writer, docs);
        try writer.writeByte('}');
    }
    try writer.writeAll("\n]");

    // Names for the keybind editor.
    try writeNames(writer, "actions", @typeInfo(input.Binding.Action).@"union".field_names);
    try writeNames(writer, "keys", @typeInfo(input.Key).@"enum".field_names);
    try writer.writeAll("}\n");
}

fn writeNames(
    writer: *std.Io.Writer,
    comptime field: []const u8,
    comptime names: []const [:0]const u8,
) !void {
    try writer.writeAll(",\n\"" ++ field ++ "\":[");
    inline for (names, 0..) |name, i| {
        if (i > 0) try writer.writeByte(',');
        try writeString(writer, name);
    }
    try writer.writeByte(']');
}
