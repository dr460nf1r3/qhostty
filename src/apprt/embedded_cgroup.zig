//! Puts the child process of each embedded surface into a transient systemd
//! scope, as the GTK apprt does. The memory killer can then end the
//! processes of one surface instead of the whole host application.
//!
//! The host creates the scope, because it owns the D-Bus connection. It
//! registers a callback with ghostty_app_set_cgroup_scope_cb.
const std = @import("std");
const builtin = @import("builtin");

const configpkg = @import("../config.zig");
const global = @import("../global.zig");
const internal_os = @import("../os/main.zig");
const Command = @import("../Command.zig");

const log = std.log.scoped(.embedded_cgroup);

/// ghostty_cgroup_scope_cb. Creates a transient systemd scope with the
/// given name and moves the process into it. A limit of zero is unset.
/// Returns false on failure.
pub const ScopeCallback = *const fn (
    userdata: ?*anyopaque,
    name: [*:0]const u8,
    pid: u32,
    memory_high: u64,
    tasks_max: u64,
) callconv(.c) bool;

var scope_callback: ?ScopeCallback = null;
var scope_userdata: ?*anyopaque = null;

/// Set the callback that creates scopes. Call this before creating surfaces.
pub fn setScopeCallback(callback: ?ScopeCallback, userdata: ?*anyopaque) void {
    scope_callback = callback;
    scope_userdata = userdata;
}

/// Embedders that use cgroups are single-instance applications, so
/// "single-instance" enables scopes like "always".
fn enabled(mode: configpkg.Config.LinuxCgroup) bool {
    if (comptime builtin.target.os.tag != .linux) return false;
    return switch (mode) {
        .never => false,
        .always, .@"single-instance" => scope_callback != null,
    };
}

pub const PreExecInfo = struct {
    linux_cgroup: configpkg.Config.LinuxCgroup,
    linux_cgroup_hard_fail: bool,

    pub fn init(cfg: *const configpkg.Config) PreExecInfo {
        return .{
            .linux_cgroup = cfg.@"linux-cgroup",
            .linux_cgroup_hard_fail = cfg.@"linux-cgroup-hard-fail",
        };
    }
};

pub const PostForkInfo = struct {
    linux_cgroup: configpkg.Config.LinuxCgroup,
    linux_cgroup_hard_fail: bool,
    linux_cgroup_memory_limit: ?u64,
    linux_cgroup_processes_limit: ?u64,

    pub fn init(cfg: *const configpkg.Config) PostForkInfo {
        return .{
            .linux_cgroup = cfg.@"linux-cgroup",
            .linux_cgroup_hard_fail = cfg.@"linux-cgroup-hard-fail",
            .linux_cgroup_memory_limit = cfg.@"linux-cgroup-memory-limit",
            .linux_cgroup_processes_limit = cfg.@"linux-cgroup-processes-limit",
        };
    }
};

/// The name of the scope for the given PID. Same as the GTK apprt.
fn fmtScope(buf: []u8, pid: u32) [:0]const u8 {
    return std.mem.printSentinel(
        buf,
        "app-ghostty-surface-transient-{}.scope",
        .{pid},
        0,
    ) catch unreachable;
}

/// Wait at most 250 ms until the process is in the scope.
fn waitForScope(pid: u32, scope: []const u8) bool {
    const start: std.Io.Timestamp = .now(global.io(), .awake);
    while (start.untilNow(global.io(), .awake).toMilliseconds() <= 250) {
        var buf: [4096]u8 = undefined;
        if (internal_os.cgroup.current(&buf, pid)) |path| {
            const current = if (std.mem.lastIndexOfScalar(u8, path, '/')) |i|
                path[i + 1 ..]
            else
                path;
            if (std.mem.eql(u8, current, scope)) return true;
        }
        std.Io.sleep(global.io(), .fromMilliseconds(25), .awake) catch unreachable;
    }
    return false;
}

/// In the child: wait for the parent to move us into our scope, so that
/// the command starts in the scope.
pub fn preExec(cmd: *Command) ?u8 {
    if (comptime builtin.target.os.tag != .linux) return null;
    if (!enabled(cmd.rt_pre_exec_info.linux_cgroup)) return null;

    const pid: u32 = @intCast(std.os.linux.getpid());
    var buf: [256]u8 = undefined;
    if (waitForScope(pid, fmtScope(&buf, pid))) return null;
    if (cmd.rt_pre_exec_info.linux_cgroup_hard_fail) {
        log.err("transition to new transient systemd scope took too long", .{});
        return 127;
    }
    return null;
}

/// In the parent: ask the host to move the child into a new scope.
pub fn postFork(cmd: *Command) Command.PostForkError!void {
    if (comptime builtin.target.os.tag != .linux) return;
    const info = cmd.rt_post_fork_info;
    if (!enabled(info.linux_cgroup)) return;
    const callback = scope_callback orelse return;

    const pid: u32 = @intCast(cmd.pid orelse {
        log.err("PID of child not known!", .{});
        return error.PostForkError;
    });

    var buf: [256]u8 = undefined;
    const scope = fmtScope(&buf, pid);
    if (!callback(
        scope_userdata,
        scope.ptr,
        pid,
        info.linux_cgroup_memory_limit orelse 0,
        info.linux_cgroup_processes_limit orelse 0,
    )) {
        if (info.linux_cgroup_hard_fail) {
            log.err("unable to create transient systemd scope {s}", .{scope});
            return error.PostForkError;
        }
        log.warn("unable to create transient systemd scope {s}", .{scope});
        return;
    }

    if (!waitForScope(pid, scope)) {
        if (info.linux_cgroup_hard_fail) {
            log.err("transition to transient systemd scope {s} took too long", .{scope});
            return error.PostForkError;
        }
        log.warn("transition to transient systemd scope {s} took too long", .{scope});
    }
}
