{ pkgs ? import <nixpkgs> {} }:

    # imports pkgs into current namespace
    with pkgs;

    stdenv.mkDerivation {

        name = "privilege-elevation-0.0.1";

        # src for local package
        src = ./.;

        # autoreconfHook will run autoreconf --install --force --verbose
        # polkit will be added to the PATH environment
        buildInputs = [ autoreconfHook polkit ];

        meta = {
            description = "Program showcasing polkit privilege elevation program using file descriptor passing.";
            homepage = https://matrix.ai/;
            license = "MIT";
            maintainers = [ "Roger Qiu <roger.qiu@matrix.ai>" ];
            platforms = stdenv.lib.platforms.unix;
        };

    }