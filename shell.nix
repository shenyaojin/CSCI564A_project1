let
  pkgs = import <nixpkgs> {};
  py3WithPackages = pkgs.python3.withPackages (
    ps: with ps; [ scipy pynvim ]
  );
in
  with pkgs; mkShell {
    buildInputs = [
      # Core
      coreutils
      curl
      gnumake
      gnutar
      gzip
      rnix-lsp

      # C/C++
      clang
      gcc
      gdb
      valgrind

      # Python
      py3WithPackages
      py3WithPackages.pkgs.flake8
    ];
  }
