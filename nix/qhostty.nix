{
  lib,
  stdenv,
  callPackage,
  cmake,
  ninja,
  pkg-config,
  zig_0_17,
  bzip2,
  expat,
  fontconfig,
  freetype,
  glslang,
  harfbuzz,
  libGL,
  libpng,
  libxml2,
  oniguruma,
  simdutf,
  spirv-cross,
  zlib,
  libxcb,
  wayland,
  wayland-scanner,
  kdePackages,
  ghostty,
  optimize ? "fast",
}: let
  zigOptimize =
    {
      debug = "Debug";
      safe = "ReleaseSafe";
      fast = "ReleaseFast";
    }
    .${optimize};
in
  stdenv.mkDerivation (finalAttrs: {
    pname = "qhostty";
    version = "1.4.0";

    src = lib.fileset.toSource {
      root = ../.;
      fileset = lib.fileset.intersection (lib.fileset.fromSource (lib.sources.cleanSource ../.)) (
        lib.fileset.unions [
          ../dist/linux
          ../images
          ../include
          ../pkg
          ../po
          ../qt
          ../src
          ../vendor
          ../build.zig
          ../build.zig.zon
          ../build.zig.zon.nix
        ]
      );
    };

    deps = callPackage ../build.zig.zon.nix {name = "qhostty-cache-${finalAttrs.version}";};

    nativeBuildInputs = [
      cmake
      ninja
      pkg-config
      zig_0_17
      wayland-scanner
      kdePackages.wrapQtAppsHook
    ];

    # Zig uses system libraries for all integrations with --system.
    buildInputs = [
      bzip2
      expat
      fontconfig
      freetype
      glslang
      harfbuzz
      libGL
      libpng
      libxml2
      oniguruma
      simdutf
      spirv-cross
      zlib
      libxcb
      wayland
      kdePackages.qtbase
      kdePackages.layer-shell-qt
      kdePackages.kwindowsystem
      kdePackages.kglobalaccel
    ];

    # CMake runs Zig itself.
    dontUseZigBuild = true;
    dontUseZigInstall = true;
    dontUseZigCheck = true;

    cmakeDir = "../qt";

    cmakeFlags = [
      (lib.cmakeFeature "QHOSTTY_ZIG_SYSTEM_DIR" "${finalAttrs.deps}")
      (lib.cmakeFeature "QHOSTTY_GHOSTTY_VERSION" finalAttrs.version)
      (lib.cmakeFeature "QHOSTTY_GHOSTTY_OPTIMIZE" zigOptimize)
      (lib.cmakeFeature "QHOSTTY_GHOSTTY_CPU" "baseline")
      (lib.cmakeBool "BUILD_TESTING" false)
    ];

    preConfigure = ''
      export ZIG_GLOBAL_CACHE_DIR="$TMPDIR/zig-cache"
    '';

    # Ghostty finds its resources next to the executable.
    postInstall = ''
      mkdir -p "$out/share/ghostty"
      ln -s "${ghostty.terminfo}/share/terminfo" "$out/share/terminfo"
      ln -s "${ghostty.shell_integration}" "$out/share/ghostty/shell-integration"
    '';

    meta = {
      description = "Native Qt 6 frontend for the Ghostty terminal core";
      homepage = "https://github.com/dr460nf1r3/qhostty";
      license = lib.licenses.mit;
      platforms = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      mainProgram = "qhostty";
    };
  })
