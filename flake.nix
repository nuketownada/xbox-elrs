{
  description = "Xbox 360 Wireless Racing Wheel to ELRS Transmitter Bridge";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    nixpkgs-esp-dev = {
      url = "github:mirrexagon/nixpkgs-esp-dev";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = inputs@{ flake-parts, nixpkgs-esp-dev, nixpkgs, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" ];

      perSystem = { system, pkgs, ... }: 
      let
        pkgsWithEsp = import nixpkgs {
          inherit system;
          overlays = [ nixpkgs-esp-dev.overlays.default ];
          config.permittedInsecurePackages = [
            "python3.13-ecdsa-0.19.1"
          ];
        };

        xbox-log = pkgs.writeShellScriptBin "xbox-log" ''
          echo "Listening for UDP logs on port 3333..."
          echo "Press Ctrl+C to stop"
          ${pkgs.socat}/bin/socat -u UDP-LISTEN:3333,fork STDOUT
        '';

        xbox-ota = pkgs.writeShellScriptBin "xbox-ota" ''
          device="''${1:-xbox-elrs.local}"
          firmware="''${2:-build/xbox-elrs.bin}"
          
          if [ ! -f "$firmware" ]; then
            echo "Error: Firmware not found: $firmware"
            echo "Run 'idf.py build' first"
            exit 1
          fi

          size=$(${pkgs.coreutils}/bin/stat -c%s "$firmware")
          echo "Pushing $firmware ($size bytes) to $device:3334..."
          
          {
            ${pkgs.coreutils}/bin/printf '%02x%02x%02x%02x' \
              $((size & 0xFF)) \
              $(((size >> 8) & 0xFF)) \
              $(((size >> 16) & 0xFF)) \
              $(((size >> 24) & 0xFF)) | ${pkgs.xxd}/bin/xxd -r -p
            ${pkgs.coreutils}/bin/cat "$firmware"
          } | ${pkgs.netcat-gnu}/bin/nc -w 30 "$device" 3334
          
          echo "Done"
        '';

        xbox-ping = pkgs.writeShellScriptBin "xbox-ping" ''
          device="''${1:-xbox-elrs.local}"
          echo "PING" | ${pkgs.netcat-gnu}/bin/nc -u -w 2 "$device" 3334
        '';

        xbox-reboot = pkgs.writeShellScriptBin "xbox-reboot" ''
          device="''${1:-xbox-elrs.local}"
          echo "REBOOT" | ${pkgs.netcat-gnu}/bin/nc -u -w 2 "$device" 3334
        '';

        xbox-fuzz = pkgs.writeShellScriptBin "xbox-fuzz" ''
          set -euo pipefail
          target="''${1:-all}"
          seconds="''${2:-60}"

          build_dir="''${FUZZ_BUILD_DIR:-fuzz-build}"

          if [ ! -d "$build_dir" ]; then
            echo "Building fuzz targets..."
            CC=clang cmake -B "$build_dir" fuzz
            cmake --build "$build_dir" -j$(nproc)
          fi

          run_fuzzer() {
            local name=$1
            local corpus="fuzz/corpus-$name"
            mkdir -p "$corpus"
            echo "Fuzzing $name for ''${seconds}s..."
            "./$build_dir/$name" "$corpus" -max_total_time="$seconds" -print_final_stats=1
          }

          if [ "$target" = "all" ]; then
            for t in fuzz_parse_report fuzz_mixer fuzz_pack_channels; do
              run_fuzzer "$t"
            done
          else
            run_fuzzer "$target"
          fi
        '';

      in {
        devShells.default = pkgsWithEsp.mkShell {
          buildInputs = [ pkgsWithEsp.esp-idf-xtensa ];

          packages = [
            # Project tools
            xbox-log
            xbox-ota
            xbox-ping
            xbox-reboot

            # Serial/debug
            pkgs.picocom
            pkgs.minicom

            # Logic analyzer
            pkgs.sigrok-cli
            pkgs.pulseview

            # General utilities
            pkgs.jq
          ];

          shellHook = ''
            echo ""
            echo "══════════════════════════════════════════════════════════════"
            echo "  Xbox 360 Racing Wheel → ELRS Transmitter Bridge"
            echo "  ESP32-S3 + USB Host + CRSF"
            echo "══════════════════════════════════════════════════════════════"
            echo ""
            echo "  Build & Flash"
            echo "    idf.py set-target esp32s3       Set target (first time)"
            echo "    idf.py menuconfig               Set WiFi credentials"
            echo "    idf.py build                    Build firmware"
            echo "    idf.py -p /dev/ttyACM0 flash    Flash via USB (boot mode)"
            echo ""
            echo "  Wireless Development"
            echo "    xbox-log                        Receive UDP logs"
            echo "    xbox-ota [ip]                   Push OTA update"
            echo "    xbox-ping [ip]                  Check device is alive"
            echo "    xbox-reboot [ip]                Reboot device"
            echo ""
            echo "  Defaults to xbox-elrs.local if no IP specified"
            echo ""
            echo "══════════════════════════════════════════════════════════════"
            echo ""
          '';
        };

        devShells.fuzz = pkgs.mkShell {
          packages = [
            pkgs.llvmPackages.clang
            pkgs.cmake
            pkgs.gnumake
            xbox-fuzz
          ];

          shellHook = ''
            echo ""
            echo "══════════════════════════════════════════════════════════════"
            echo "  Xbox-ELRS Fuzz Environment"
            echo "══════════════════════════════════════════════════════════════"
            echo ""
            echo "  Quick start:"
            echo "    cmake -B fuzz-build fuzz && cmake --build fuzz-build -j\$(nproc)"
            echo "    ./fuzz-build/test_watchdog                    Run watchdog test"
            echo "    ./fuzz-build/fuzz_parse_report corpus/ -max_total_time=60"
            echo "    ./fuzz-build/fuzz_mixer corpus/ -max_total_time=60"
            echo "    ./fuzz-build/fuzz_pack_channels corpus/ -max_total_time=60"
            echo ""
            echo "  Or use the helper:"
            echo "    xbox-fuzz [target|all] [seconds]"
            echo ""
            echo "══════════════════════════════════════════════════════════════"
            echo ""
          '';
        };
      };
    };
}
