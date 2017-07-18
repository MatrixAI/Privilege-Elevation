{ pkgs ? import <nixpkgs> {} }:
  with pkgs;
  stdenv.mkDerivation {
    name = "privilege-elevation";
    buildInputs = [ autoreconfHook ];
  }
