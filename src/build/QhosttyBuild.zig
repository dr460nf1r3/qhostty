//! Build steps of the Qhostty fork. The upstream build files call this file,
//! so that the fork changes them by only one line each.
const std = @import("std");
const Config = @import("Config.zig");
const SharedDeps = @import("SharedDeps.zig");

/// Add Glad and EGL to the embedded OpenGL library. SharedDeps adds them
/// only to executables.
pub fn addEmbeddedOpenGL(cfg: *const Config, step: *std.Build.Step.Compile) void {
    if (step.kind != .lib or
        cfg.app_runtime != .none or
        cfg.emit_lib_vt or
        cfg.renderer != .opengl) return;

    const b = step.step.owner;
    step.root_module.addIncludePath(b.path("vendor/glad/include/"));
    step.root_module.addCSourceFile(.{
        .file = b.path("vendor/glad/src/gl.c"),
        .flags = &.{},
    });
    step.root_module.addCSourceFile(.{
        .file = b.path("vendor/glad/src/glad_egl.c"),
        .flags = &.{},
    });
    step.root_module.linkSystemLibrary("egl", .{
        .preferred_link_mode = .dynamic,
        .search_strategy = .mode_first,
    });
}

/// Install the files that embedders need next to libghostty: the fork
/// header, the shell integration, the themes, and the config schema.
pub fn installEmbedderFiles(b: *std.Build, deps: *const SharedDeps) void {
    const cfg = deps.config;
    const install = b.getInstallStep();
    install.dependOn(&configSchema(b, deps).step);
    install.dependOn(&b.addInstallHeaderFile(
        b.path("include/ghostty_ext.h"),
        "ghostty_ext.h",
    ).step);

    install.dependOn(&b.addInstallDirectory(.{
        .source_dir = b.path("src/shell-integration"),
        .install_dir = .{ .custom = "share" },
        .install_subdir = b.pathJoin(&.{ "ghostty", "shell-integration" }),
        .exclude_extensions = &.{".md"},
    }).step);

    if (cfg.emit_themes) {
        if (b.lazyDependency("iterm2_themes", .{})) |themes| {
            install.dependOn(&b.addInstallDirectory(.{
                .source_dir = themes.path(""),
                .install_dir = .{ .custom = "share" },
                .install_subdir = b.pathJoin(&.{ "ghostty", "themes" }),
                .exclude_extensions = &.{".md"},
            }).step);
        }
    }
}

/// Generate share/qhostty/config-schema.json from Config.zig. See
/// src/qhostty_schema.zig.
fn configSchema(b: *std.Build, deps: *const SharedDeps) *std.Build.Step.InstallFile {
    const exe = b.addExecutable(.{
        .name = "qhostty_schema",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/qhostty_schema.zig"),
            .target = b.graph.host,
        }),
    });
    deps.help_strings.addImport(exe);

    // Config.zig reads the build options. Any entrypoint will do.
    const options = b.addOptions();
    deps.config.addOptions(options) catch @panic("OOM");
    exe.root_module.addOptions("build_options", options);

    const run = b.addRunArtifact(exe);
    return b.addInstallFile(
        run.captureStdOut(.{}),
        "share/qhostty/config-schema.json",
    );
}
