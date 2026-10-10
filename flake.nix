{
  description = "xanatoo — interactive document composition engine";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
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
            ];

            buildInputs = with pkgs; [
              SDL2
              freetype
              fontconfig
              xorg.libX11
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
          # Default package wraps the binary so a DejaVu font is always available.
          default = pkgs.symlinkJoin {
            name = "xanatoo";
            paths = [ xanatoo ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              wrapProgram $out/bin/xanatoo \
                --set-default XANATOO_FONT \
                  "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf"
            '';
            meta = xanatoo.meta;
          };

          # Unwrapped binary for packaging / debugging
          xanatoo-unwrapped = xanatoo;
        });

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              cmake
              pkg-config
              SDL2
              freetype
              fontconfig
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
