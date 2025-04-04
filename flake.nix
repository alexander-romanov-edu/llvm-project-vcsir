{
  description = "lldb-bug-repro";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    {
      self,
      flake-parts,
      ...
    }@inputs:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      perSystem =
        { pkgs, ... }:
        {
          packages = {
            default = pkgs.hello;
          };
          devShells.default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [cmake lldb_20 ccache];
            buildInputs = with pkgs; [zlib];
          };
        };
    };
}
