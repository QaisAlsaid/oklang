{
  description = "OKLang";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      clangStdenv = pkgs.clangStdenv;

      ok = clangStdenv.mkDerivation {
        pname = "ok";
        version = "0.0.0";
        src = self;
        nativeBuildInputs = with pkgs; [ cmake ninja ];
        cmakeFlags = [ "-GNinja" ];
      };
    in
    {
      packages.${system} = {
        default = ok;
        inherit ok;
      };

      checks.${system} = {
        inherit ok;
        regressions = pkgs.runCommand "oklang-regressions" {
          nativeBuildInputs = [ ok ];
        } ''
          mkdir -p $out
          oktest-regression ${ok}/bin/ok ${self}/tests ${ok}/bin/ok_debug | tee $out/summary.txt
        '';
      };

      devShells.${system}.default = pkgs.mkShell.override { stdenv = clangStdenv; } {
        packages = with pkgs; [ cmake ninja lldb pkg-config git ];
      };
    };
}

