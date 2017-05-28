{ pkgs ? import <nixpkgs> {} }:
  with pkgs;
  stdenv.mkDerivation {
    name = "privilege-elevation";
    src = ./.;
    buildInputs = [ autoreconfHook polkit ];
    meta = {
        description = "Program showcasing polkit privilege elevation program using file descriptor passing.";
        homepage = https://matrix.ai/;
        license = "MIT";
        maintainers = [ "Roger Qiu <roger.qiu@matrix.ai>" ];
        platforms = stdenv.lib.platforms.unix;
    };
  }
