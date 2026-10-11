{
  description = "xanatoo — interactive document composition engine";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          xanatoo = pkgs.stdenv.mkDerivation {
            pname = "xanatoo";
            version = "0.1.0";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              # CMake custom command runs scripts/rasterize_icons.py (SVG→PNG).
              python3
            ];

            buildInputs = with pkgs; [
              SDL2
              freetype
              fontconfig
              openssl
              # Prefer top-level libx11; xorg.* package set is deprecated.
              libx11
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            meta = with pkgs.lib; {
              description = "Interactive document composition engine";
              license = licenses.gpl3Plus;
              platforms = platforms.linux ++ platforms.darwin;
              mainProgram = "xanatoo";
            };
          };
        in
        {
          # Default package wraps the binary with font + installed icon path.
          default = pkgs.symlinkJoin {
            name = "xanatoo";
            paths = [ xanatoo ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              wrapProgram $out/bin/xanatoo \
                --set-default XANATOO_FONT \
                  "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf" \
                --set-default XANATOO_ICON_DIR \
                  "${xanatoo}/share/xanatoo/icons"
            '';
            meta = xanatoo.meta;
          };

          # Unwrapped binary for packaging / debugging
          xanatoo-unwrapped = xanatoo;
        } // (
          # R36S / ArkOS PortMaster packages (Linux hosts only: cross to aarch64).
          if pkgs.stdenv.hostPlatform.isLinux then
            let
              version = "0.1.0";
              date = "1970-01-01";
              r36s = import ./nix/r36s.nix { inherit pkgs version date; };
            in {
              arkos-sysroot = r36s.sysroot;
              xanatoo-r36s = r36s.r36s;
              xanatoo-r36s-portmaster = r36s.portmaster;
              xanatoo-r36s-portmaster-zip = r36s.portmasterZip;
            }
          else { }
        ));

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              cmake
              pkg-config
              python3
              SDL2
              freetype
              fontconfig
              openssl
              libx11
              dejavu_fonts
              gdb
              clang-tools
            ];
            shellHook = ''
              export XANATOO_FONT="${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf"
            '';
          };
        });
    };
}
