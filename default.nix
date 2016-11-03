{ pkgs ? import <nixpkgs> {} }:

    # imports pkgs into current namespace
    with pkgs;

    stdenv.mkDerivation {

        name = "file-descriptor-example-0.0.1";

        src = fetchurl {
            url = https://github.com/...;
            sha256 = "...";
        };

        # don't we need polkit to get pkexec available??
        buildInputs = [ pkgconfig polkit ];

        outputs = [ "out" "dev" ];
        outputBin = "dev";

        preConfigure = ''
            configureFlagsArray+=("--exec-prefix=$dev")
        '';

        meta = {
            description = "Example program showcasing polkit privilege elevation program using file descriptor passing.";
            homepage = https://matrix.ai/;
            license = "MIT";
            maintainers = [ stdenv.lib.maintainers.sander ];
        };

    }