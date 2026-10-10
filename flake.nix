{
  description = "glyphtoo — interactive document composition engine";

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
          glyphtoo = pkgs.stdenv.mkDerivation {
            pname = "glyphtoo";
            version = "0.1.0";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              SDL2
              freetype
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            meta = with pkgs.lib; {
              description = "Interactive document composition engine";
              license = licenses.gpl3Plus;
              platforms = platforms.linux ++ platforms.darwin;
              mainProgram = "glyphtoo";
            };
          };
        in
        {
          # Default package wraps the binary so a DejaVu font is always available.
          default = pkgs.symlinkJoin {
            name = "glyphtoo";
            paths = [ glyphtoo ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              wrapProgram $out/bin/glyphtoo \
                --set-default GLYPHTOO_FONT \
                  "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf"
            '';
            meta = glyphtoo.meta;
          };

          # Unwrapped binary for packaging / debugging
          glyphtoo-unwrapped = glyphtoo;
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
              dejavu_fonts
              gdb
              clang-tools
            ];
            shellHook = ''
              export GLYPHTOO_FONT="${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf"
            '';
          };
        });
    };
}
