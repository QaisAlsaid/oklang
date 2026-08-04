{
  description = "OKLang";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-26.05";
  };

  outputs = { self, nixpkgs }:
  let
    system = "x86_64-linux";
    pkgs = import nixpkgs {
      inherit system;
    };
    clangStdenv = pkgs.clangStdenv;

    okc = clangStdenv.mkDerivation {
      pname = "okc";
      version = "0.0.0";
      src = self;

      nativeBuildInputs = with pkgs; [
        cmake
        ninja
      ];

      cmakeFlags = [
        "-GNinja"
      ];
    };
  in {
    packages.${system} = {
      default = okc;
      inherit okc;
    };

    checks.${system} = {
      inherit okc;
      regressions = pkgs.runCommand "oklang-regressions" {
        nativeBuildInputs = [ okc ];
      } ''
        mkdir -p $out
        oktest-regression ${okc}/bin/okc ${self}/tests ${okc}/bin/okc_debug | tee $out/summary.txt
      '';
    };

    devShells.${system}.default = pkgs.mkShell.override { stdenv = clangStdenv; } {
      packages = with pkgs; [
        cmake
        ninja
        lldb
        pkg-config
        git
      ];
    };
  };
}
