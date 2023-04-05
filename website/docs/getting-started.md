---
sidebar_position: 2
---

# Getting Started

## Dependencies

First ensure your system has all the dependencies installed. We have tested on Ubuntu 22.04.01 (Jammy) OpenSuse and Fedora 37 and the dependencies are listed below. Please let us know if you have issues with these or any other Linux repos and we'll do our best to look into it.

### Ubuntu 22.04.1 (Jammy)

```
$ sudo apt-get update
$ sudo apt-get install -y bison autopoint build-essential clang-12 cmake flex gawk libboost-all-dev libbz2-dev libcap2-bin libclang-12-dev libcurl4-gnutls-dev libdouble-conversion-dev libdw-dev libfmt-dev libgflags-dev libgmock-dev libgoogle-glog-dev libgtest-dev libjemalloc-dev libmsgpack-dev libzstd-dev llvm-12-dev ninja-build pkg-config python3-setuptools sudo xsltproc libboost-all-dev
$ sudo pip3 install toml
$ cmake -G Ninja -B build/
```

### OpenSuse Tumbleweed

```
$ sudo zypper in git ninja cmake llvm12-devel clang12-devel binutils-gold gcc gcc-c++ libcap-progs sudo gflags-devel-static gflags-devel bison libboost_{system,filesystem,thread,regex,serialization}-devel msgpack-c-devel libzstd-devel flex gtest gmock python3-toml python3-devel python3-setuptools gettext-tools findutils zlib-devel libcurl-devel libbz2-devel libdw-devel libdwarf-devel jemalloc-devel msgpack-cxx-devel double-conversion-devel fmt-devel
$ sudo apt install libdw-dev libclang-dev llvm-dev libboost-all-dev libgtest-dev libbz2-dev libgflags-dev libzstd-dev libcurl4-gnutls-dev ninja-build python3-toml
```

It seems like static versions of clang/llvm are not packaged in opensuse, but we default to looking for the static versions. To fix this, please add the following to your cmake generation command below.

```
-DFORCE_BOOST_STATIC=Off -DFORCE_LLVM_STATIC=Off
```

### Fedora 37

```
$ sudo dnf install boost-static elfutils-devel clang clang12-devel llvm12-devel llvm12-static gtest-devel gflags-devel gmock-devel cmake autoconf automake libtool python3-devel bzip2-devel fmt-devel gettext-devel libcurl-devel ninja-build python3-toml bison bison-devel flex msgpack-devel jemalloc-devel double-conversion-static
$ cmake -G Ninja -B build/ -DLLVM_DIR=/usr/lib64/llvm12/lib/cmake/llvm -DClang_DIR=/usr/lib64/llvm12/lib/cmake/clang -DFORCE_LLVM_STATIC=Off
```

### Clone the OI repo:

```
$ git clone git@github.com:facebookexperimental/object-introspection.git
Cloning into 'object-introspection'...
remote: Enumerating objects: 8694, done.
remote: Counting objects: 100% (116/116), done.
remote: Compressing objects: 100% (93/93), done.
remote: Total 8694 (delta 36), reused 69 (delta 19), pack-reused 8578
Receiving objects: 100% (8694/8694), 2.60 MiB | 4.59 MiB/s, done.
Resolving deltas: 100% (6339/6339), done.
```

### Compile and run tests

Now compile the `oid` binary:

```
$ cd object-introspection/
$ cmake --build build/ -j
```

```
$ ~/object-introspection# ls -l build/oid
-rwxr-xr-x 1 root root 86818272 Dec 16 18:08 build/oid
```

OI needs a system specific configuration file generating so do that now using the `tools/config_gen.py` script:

```
$ ~/object-introspection# tools/config_gen.py -c clang++-12 build/oid-cfg.toml
$ ls -l build/oid-cfg.toml
-rw-r--r-- 1 root root 2978 Dec 16 18:18 build/oid-cfg.toml
```

You're now ready to introspect objects!
