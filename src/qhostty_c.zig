//! C API additions of the Qhostty fork. They are in this file, not in the
//! upstream files, so that the fork can rebase on upstream without
//! conflicts. main_c.zig references this file. The declarations are in
//! include/ghostty_ext.h.
const std = @import("std");
const build_config = @import("build_config.zig");
const apprt = @import("apprt.zig");
const global = @import("global.zig");
const input = @import("input.zig");
const renderer = @import("renderer.zig");
const Config = @import("config.zig").Config;
const cgroup = @import("apprt/embedded_cgroup.zig");

const log = std.log.scoped(.qhostty);

comptime {
    if (apprt.runtime == apprt.embedded) {
        _ = Embedded;
        if (build_config.renderer == .opengl) _ = OpenGL;
    }
}

/// Reload the process environment. Call this after the host changes the
/// environment, for example with setenv or unsetenv.
export fn ghostty_sync_environ() void {
    global.syncEnviron();
}

const GlobalKeybindCallback = *const fn (
    ?*anyopaque,
    input.Binding.Trigger.C,
    [*]const u8,
    usize,
) callconv(.c) bool;

/// Call the callback for each global keybind that has one action.
export fn ghostty_config_enumerate_global_keybinds(
    self: *Config,
    userdata: ?*anyopaque,
    callback: GlobalKeybindCallback,
) void {
    var action_buf: [4096]u8 = undefined;
    var it = self.keybind.set.bindings.iterator();
    while (it.next()) |entry| {
        const leaf: input.Binding.Set.GenericLeaf = switch (entry.value_ptr.*) {
            .leader => continue,
            inline .leaf, .leaf_chained => |leaf| leaf.generic(),
        };
        if (!leaf.flags.global) continue;

        const actions = leaf.actionsSlice();
        if (actions.len != 1) continue;
        const action = std.fmt.bufPrint(&action_buf, "{f}", .{actions[0]}) catch continue;
        if (!callback(userdata, entry.key_ptr.*.cval(), action.ptr, action.len)) return;
    }
}

const Embedded = struct {
    const App = apprt.embedded.App;
    const Surface = apprt.embedded.Surface;

    /// Set the callback that moves the child process of each surface into
    /// a transient systemd scope. See embedded_cgroup.zig.
    export fn ghostty_app_set_cgroup_scope_cb(
        app: *App,
        callback: ?cgroup.ScopeCallback,
    ) void {
        cgroup.setScopeCallback(callback, app.opts.userdata);
    }

    /// Free allocations owned by an inherited surface configuration.
    export fn ghostty_surface_free_inherited_config(
        surface: *Surface,
        config: *Surface.Options,
    ) void {
        if (config.working_directory) |ptr| {
            surface.app.core_app.alloc.free(std.mem.span(ptr));
            config.working_directory = null;
        }
    }
};

/// The OpenGL renderer for hosts that present its frames themselves.
const OpenGL = struct {
    const Surface = apprt.embedded.Surface;
    const Inspector = apprt.embedded.Inspector;
    const ExportedFrame = renderer.Renderer.ExportedFrame;
    const cimgui = @import("dcimgui");

    const FrameType = enum(c_int) {
        dmabuf,
        memory,
    };

    /// ghostty_frame_s
    const Frame = extern struct {
        type: FrameType,
        width: u32,
        height: u32,
        fourcc: u32,
        modifier: u64,
        plane_count: u32,
        fds: [4]c_int,
        offsets: [4]c_int,
        strides: [4]c_int,
        pixels: ?[*]const u8,
        pixels_len: usize,
        handle: ?*ExportedFrame,
    };

    /// Take the latest frame of the renderer. Returns false if there is no
    /// new frame. The caller must free the frame with ghostty_frame_free.
    export fn ghostty_surface_take_frame(
        surface: *Surface,
        result: *Frame,
    ) bool {
        const frame = surface.core_surface.renderer.takeFrame() orelse
            return false;
        const handle = global.alloc().create(ExportedFrame) catch {
            frame.deinit();
            return false;
        };
        handle.* = frame;

        result.* = .{
            .type = .memory,
            .width = 0,
            .height = 0,
            .fourcc = 0,
            .modifier = 0,
            .plane_count = 0,
            .fds = @splat(-1),
            .offsets = @splat(0),
            .strides = @splat(0),
            .pixels = null,
            .pixels_len = 0,
            .handle = handle,
        };
        switch (frame) {
            .dmabuf => |v| {
                result.type = .dmabuf;
                result.width = v.width;
                result.height = v.height;
                result.fourcc = v.fourcc;
                result.modifier = v.modifier;
                result.plane_count = v.planes.count;
                result.fds = v.planes.fds;
                result.offsets = v.planes.offsets;
                result.strides = v.planes.strides;
            },
            .memory => |v| {
                result.width = v.width;
                result.height = v.height;
                result.pixels = v.pixels.ptr;
                result.pixels_len = v.pixels.len;
            },
        }
        return true;
    }

    /// Free a frame from ghostty_surface_take_frame. This closes the DMABUF
    /// file descriptors of the frame.
    export fn ghostty_frame_free(frame: *Frame) void {
        const handle = frame.handle orelse return;
        handle.deinit();
        global.alloc().destroy(handle);
        frame.handle = null;
    }

    /// Tell the renderer if the host can present DMABUF frames. If the
    /// health is unhealthy, the renderer exports frames in CPU memory.
    export fn ghostty_surface_report_presentation_health(
        surface: *Surface,
        health: renderer.Health,
    ) void {
        surface.core_surface.reportPresentationHealth(health);
    }

    /// Initialize the inspector for OpenGL. The host must make the inspector
    /// context current first.
    export fn ghostty_inspector_opengl_realize(self: *Inspector) bool {
        cimgui.c.ImGui_SetCurrentContext(self.ig_ctx);
        if (self.backend) |v| {
            v.deinit();
            self.backend = null;
        }

        if (!cimgui.ImGui_ImplOpenGL3_Init(null)) {
            log.warn("failed to initialize OpenGL inspector backend", .{});
            return false;
        }
        self.backend = .opengl;
        return true;
    }

    export fn ghostty_inspector_opengl_render(self: *Inspector) void {
        renderInspector(self) catch |err| {
            log.err("error rendering OpenGL inspector err={}", .{err});
        };
    }

    fn renderInspector(self: *Inspector) !void {
        if (self.backend != .opengl) return error.InspectorNotRealized;
        cimgui.c.ImGui_SetCurrentContext(self.ig_ctx);

        for (0..2) |_| {
            cimgui.ImGui_ImplOpenGL3_NewFrame();
            try self.newFrame();
            cimgui.c.ImGui_NewFrame();

            render: {
                const surface = &self.surface.core_surface;
                const inspector = surface.inspector orelse break :render;
                inspector.render(surface);
            }

            cimgui.c.ImGui_Render();
        }

        cimgui.ImGui_ImplOpenGL3_RenderDrawData(cimgui.c.ImGui_GetDrawData());
    }

    export fn ghostty_inspector_opengl_unrealize(self: *Inspector) void {
        cimgui.c.ImGui_SetCurrentContext(self.ig_ctx);
        if (self.backend == .opengl) {
            self.backend.?.deinit();
            self.backend = null;
        }
    }
};
