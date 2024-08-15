# object-introspection

[![Matrix Chat](https://img.shields.io/matrix/object-introspection:matrix.org.svg)](https://matrix.to/#/#object-introspection:matrix.org)
[![CppCon 2023 Presentation](https://img.shields.io/youtube/views/6IlTs8YRne0?label=CppCon%202023)](https://youtu.be/6IlTs8YRne0)

![OI Logo](/website/static/img/OIBrandmark.svg)

Object Introspection is a memory profiling technology for C++ objects. It provides the ability to dynamically instrument applications to capture the precise memory occupancy of entire object hierarchies including all containers and dynamic allocations. All this with no code modification or recompilation!

For more information on the technology and how to get started applying it  to your applications please check out the [Object Introspection](https://objectintrospection.org/) website.

## Join the Object Introspection community
See the [CONTRIBUTING](CONTRIBUTING.md) file for how to help out.

## License
Object Introspection is licensed under the [Apache 2.0 License](LICENSE).

## Getting started with Nix

Nix is the easiest way to get started with `oid` as it is non-trivial to build otherwise. Explicit Nix support for Object Introspection as a Library will come down the line, but Nix can currently provide you a reproducible development environment in which to build it.

These examples expect you to have `nix` installed and available with no other dependencies required. Find the installation guide at https://nixos.org/download.html.

We also required flake support. To enable flakes globally run:

    $ mkdir -p ~/.config/nix
    $ echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf

Or suffix every `nix` command with `nix --extra-experimental-features 'nix-command flakes'`.

### Run upstream OID without modifying the source

    $ nix run github:facebookexperimental/object-introspection -- --help

This will download the latest source into your Nix store along with all of its dependencies, running help afterwards.

### Build OID locally

    $ git clone https://github.com/facebookexperimental/object-introspection
    $ nix build
    $ ./result/bin/oid --help

This will build OID from your local sources. Please note that this will NOT pick up changes to `extern/drgn` or `extern/drgn/libdrgn/velfutils`.

### Get a development environment

    $ nix develop
    $ cmake -B build -G Ninja -DFORCE_BOOST_STATIC=Off
    $ ninja -C build
    $ build/oid --help

This command provides a development shell with all the required dependencies. This is the most flexible option and will pick up source changes as CMake normally would.

Sometimes this developer environment can be polluted by things installed on your normal system. If this is an issue, use:

    $ nix develop -i

This removes the environment from your host system and makes the build pure.

### Run the tests

    $ nix develop
    $ cmake -B build -G Ninja -DFORCE_BOOST_STATIC=Off
    $ ninja -C build
    $ ./tools/config_gen.py -c clang++ build/testing.oid.toml
    $ ctest -j --test-dir build/test

Running tests under `nix` is new to the project and may take some time to mature. The CI is the source of truth for now.

### Format source

    $ nix fmt

This formats the Nix, C++, and Python code in the repository.
