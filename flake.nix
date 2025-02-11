# {
#   description = "A flake for compiling picotool from Raspberry Pi";

#   inputs = {
#     nixpkgs.url = "github:nixos/nixpkgs";
#     flake-utils.url = "github:numtide/flake-utils";
#   };

#   outputs = { self, flake-utils, nixpkgs }:
#   flake-utils.lib.eachSystem [ flake-utils.lib.system.x86_64-linux]
#   (system:
#     let
#       pkgs = nixpkgs.legacyPackages.${system};
      
#       dependencies =  with pkgs; [
#         libusb1
#         cmake
#         pkg-config
#         libgcc
#       ];
        
  
#   in {

#   }
#   );


# }
{
  description = "A flake for compiling picotool from Raspberry Pi";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        picoSdk = pkgs.fetchFromGitHub {
          owner = "raspberrypi";
          repo = "pico-sdk";
          rev = "master"; 
          sha256 = "0qzj3x7vqrflirgbxmji2m5fqxha7ib95nsg6glhpn7id7lkb9s0";
        };
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.cmake
            pkgs.gcc
            pkgs.libusb1
          ];
        shellHook = ''
              export PICO_SDK_PATH=${picoSdk}
              echo "PICO_SDK_PATH set to ${picoSdk}"
            '';
        };
      });
}
