# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "EndspielZero - a generalizable FOSS sandbox for chess-like variants";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      devShells = forAll (pkgs:
        let
          # Toolchain shared by every shell. gcc and clang are both present
          # because the exit gate builds with both (docs/plan/00-roadmap.md).
          core = with pkgs; [
            gcc cmake ninja ccache
            clang-tools llvmPackages.clang
            gdb lcov gcovr
            catch2_3 tomlplusplus
            python3 git
          ] ++ lib.optionals stdenv.hostPlatform.isLinux [ valgrind ];

          # Dear ImGui with the backends this project actually uses. The default
          # package ships GLFW and OpenGL only, which are of no use here.
          imguiSdlVulkan = pkgs.imgui.override {
            IMGUI_BUILD_SDL3_BINDING = true;
            IMGUI_BUILD_VULKAN_BINDING = true;
            IMGUI_BUILD_GLFW_BINDING = false;
            IMGUI_BUILD_OPENGL3_BINDING = false;
          };

          gfx = (with pkgs; [
            sdl3 vulkan-headers vulkan-loader vulkan-validation-layers
            vulkan-tools shaderc spirv-tools vulkan-memory-allocator
            # Typefaces are part of the build, not a system dependency: the game
            # installs its own so it looks the same on every machine.
            cinzel jetbrains-mono
          ]) ++ [ imguiSdlVulkan ];

          # Chakra Petch, assembled into one directory so the game can look for it the
          # same way it looks for every other font.
          chakraPetch =
            let
              rev = "23e54b51ddffbc7713c583748e3bd86f62b1fa4a";
              grab = name: hash: pkgs.fetchurl {
                url = "https://raw.githubusercontent.com/google/fonts/${rev}/ofl/chakrapetch/${name}";
                sha256 = hash;
              };
            in pkgs.runCommand "chakra-petch-fonts" { } ''
              mkdir -p $out
              cp ${grab "ChakraPetch-Bold.ttf" "183qn96i14jjn1w68iykvn4sg58fgiqlvnqrbqkrf5k5jmnzgyv5"} $out/ChakraPetch-Bold.ttf
              cp ${grab "ChakraPetch-SemiBold.ttf" "0jvklr3dllic3ligrr8aclb0sqswrlm418hl7vxxbnsd43ils9j5"} $out/ChakraPetch-SemiBold.ttf
              cp ${grab "ChakraPetch-Regular.ttf" "11prxjl36nbdabinswh22cxv5qqf6g73hxbb67q1zj55p8wddz4q"} $out/ChakraPetch-Regular.ttf
            '';

          mkShell = extra: pkgs.mkShell {
            packages = core ++ extra;
            # Validation layers are only discoverable if their manifest directory is on
            # the layer path, and a renderer without validation is a renderer whose
            # bugs surface as corrupted pixels instead of as messages.
            VK_LAYER_PATH =
              "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
            # Debug builds are -O0, and _FORTIFY_SOURCE emits a #warning there -
            # which is fatal under -Werror. Drop the hardening flag in the shell
            # rather than weakening our own warning settings.
            hardeningDisable = [ "fortify" "fortify3" ];
            # CMAKE_PREFIX_PATH is deliberately NOT set here: nix's cmake setup hook
            # already puts every buildInput on it, and overriding it hides the rest of
            # them (which is exactly how imgui went missing once).
            # The shell's three voices. Chakra Petch is not packaged in nixpkgs, so it
            # is fetched by hash from google/fonts at a pinned commit - immutable, and a
            # loud failure rather than a silent substitution if it ever moves.
            CB_FONT_DISPLAY = chakraPetch;
            CB_FONT_BODY = "${pkgs.public-sans}/share/fonts/truetype";
            CB_FONT_MONO = "${pkgs.jetbrains-mono}/share/fonts/truetype";
            shellHook = ''
              export CCACHE_DIR="$PWD/.cache/ccache"
              echo "EndspielZero dev shell. See AGENTS.md. Presets: dev asan tsan release coverage bench"
            '';
          };
        in {
          default = mkShell [ ];
          gfx = mkShell gfx;
        });

      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "endspielzero";
          version = "0.0.0";
          src = self;
          nativeBuildInputs = with pkgs; [ cmake ninja ];
          buildInputs = with pkgs; [ catch2_3 tomlplusplus ];
          cmakeBuildType = "Release";
          doCheck = true;
          # Everything but the deep perft counts, which take minutes and are their own
          # label. The wide property sweep is included: it is the test most likely to
          # catch a real generalization bug, and it costs about two minutes.
          checkPhase = ''
            ctest --output-on-failure -L 'unit|property|golden|arch'
          '';
          meta = with pkgs.lib; {
            description = "Generalizable FOSS sandbox for chess-like variants";
            license = licenses.gpl3Plus;
            platforms = platforms.unix;
          };
        };
      });

      checks = forAll (pkgs: {
        # `nix flake check` is the gate. Keep it honest: it builds and tests.
        build = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      });

      formatter = forAll (pkgs: pkgs.nixpkgs-fmt);
    };
}
