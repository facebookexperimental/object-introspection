{
  description = "Object level memory profiler for C++";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    flake-utils.url = "github:numtide/flake-utils";

    treefmt-nix.url = "github:numtide/treefmt-nix";
    treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      treefmt-nix,
      ...
    }@inputs:
    flake-utils.lib.eachSystem [ flake-utils.lib.system.x86_64-linux ] (
      system:
      let
        defaultLlvmVersion = 16;
        pkgs = import nixpkgs { inherit system; };

        drgnSrc = pkgs.fetchFromGitHub {
          owner = "JakeHillion";
          repo = "drgn";
          rev = "b1f8c3e8526611b6720800250ba858a713dd9e4f";
          hash = "sha256-5WhMHgx/RKtqjxGx4AyiqVKMot5xulr+6c8i2E9IxiA=";
          fetchSubmodules = true;
        };

        mkOidPackage =
          llvmVersion:
          let
            llvmPackages = pkgs."llvmPackages_${toString llvmVersion}";
          in
          llvmPackages.stdenv.mkDerivation rec {
            name = "oid";

            src = self;

            nativeBuildInputs = with pkgs; [
              autoconf
              automake
              bison
              cmake
              flex
              gettext
              git
              hexdump
              libtool
              ninja
              pkgconf
              python312
              python312Packages.setuptools
              python312Packages.toml
              glibcLocales
            ];

            buildInputs =
              (with llvmPackages; [
                llvmPackages.libclang
                llvmPackages.llvm
                llvmPackages.openmp
              ])
              ++ (with pkgs; [
                boost
                bzip2
                curl
                double-conversion
                elfutils
                flex
                folly
                folly.fmt
                gflags
                glog
                gtest
                icu
                jemalloc
                libarchive
                libmicrohttpd
                liburing
                libxml2
                lzma
                msgpack
                range-v3
                rocksdb_8_11
                sqlite
                tomlplusplus
                zstd
              ]);

            cmakeFlags = [
              "-Ddrgn_SOURCE_DIR=${drgnSrc}"
              "-DFORCE_BOOST_STATIC=Off"
            ];

            outputs = [ "out" ];
          };

        mkOidDevShell =
          llvmVersion:
          let
            llvmPackages = pkgs."llvmPackages_${toString llvmVersion}";
          in
          pkgs.mkShell.override { stdenv = llvmPackages.stdenv; } {
            inputsFrom = [ self.packages.${system}."oid-llvm${toString llvmVersion}" ];
            buildInputs = [ ];
          };
      in
      {
        packages = rec {
          default = self.packages.${system}."oid-llvm${toString defaultLlvmVersion}";

          oid-llvm15 = mkOidPackage 15;
          oid-llvm16 = mkOidPackage 16;
        };
        devShells = rec {
          default = self.devShells.${system}."oid-llvm${toString defaultLlvmVersion}";

          oid-llvm15 = mkOidDevShell 15;
          oid-llvm16 = mkOidDevShell 16;
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/oid";
        };
      }
    )
    // flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        treefmtEval = treefmt-nix.lib.evalModule pkgs (pkgs: {
          projectRootFile = "flake.nix";
          settings.global.excludes = [ "./extern/**" ];

          programs.nixfmt.enable = true;
          programs.clang-format.enable = true;
          programs.black.enable = true;
          programs.isort.enable = true;
        });
      in
      {
        formatter = treefmtEval.config.build.wrapper;
        checks.formatting = treefmtEval.config.build.check self;
      }
    );
}
