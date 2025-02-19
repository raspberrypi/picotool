{
  description = "A flake for compiling picotool from Raspberry Pi";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin"] (system:
      let
        pkgs = import nixpkgs { inherit system; };

        picoSdk = pkgs.fetchFromGitHub {
          owner = "raspberrypi";
          repo = "pico-sdk";
          rev = "master"; 
          sha256 = "0qzj3x7vqrflirgbxmji2m5fqxha7ib95nsg6glhpn7id7lkb9s0";
        };

        commonDeps = [ 
          pkgs.cmake
          pkgs.libusb1
        ];

        macDeps = [
          pkgs.llvmPackages.clang
          pkgs.libiconv
        ];

        linuxDeps = [
          pkgs.gcc
        ];

        buildDeps = if pkgs.stdenv.isDarwin then commonDeps ++ macDeps else commonDeps ++ linuxDeps;

      in {
        devShells.default = pkgs.mkShell {
          buildInputs = buildDeps;
        shellHook = ''
              export PICO_SDK_PATH=${picoSdk}
              echo "PICO_SDK_PATH set to ${picoSdk}"
            '';
        };
      });
}
