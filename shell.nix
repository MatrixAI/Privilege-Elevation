{ pkgs ? import <nixpkgs> {} }:
  with pkgs;
  stdenv.mkDerivation {
    name = "privilege-elevation";
    src = ./.;
    buildInputs = [ autoreconfHook polkit ];
  }
