#include <gmock/gmock.h>
#include <gtest/gtest-death-test.h>
#include <gtest/gtest.h>
#include <sys/mman.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "oi/OICompiler.h"

namespace fs = std::filesystem;

/* Add the suffix operator _b to easily create array of uint8_t. */
static constexpr uint8_t operator"" _b(unsigned long long n) {
  return uint8_t(n);
}

typedef int (*jitFunc)(void);

TEST(CompilerTest, CompileAndRelocate) {
  auto symbols = std::make_shared<SymbolService>(getpid());

  auto code = R"(
    // Use `extern "C"` to prevent mangled symbol names
    extern "C" {
    static int xs[] = {1, 2, 3, 4, 5};
    int sumXs() {
      // Use `volatile` to prevent the compiler from optimising out the loop
      volatile int sum = 0;
      for (int i = 0; i < 5; i++)
        sum += xs[i];
      return sum;
    }

    int constant() { return 42; }
    }
  )";

  auto tmpdir = fs::temp_directory_path() / "test-XXXXXX";
  EXPECT_NE(mkdtemp(const_cast<char*>(tmpdir.c_str())), nullptr);

  auto sourcePath = tmpdir / "src.cpp";
  auto objectPath = tmpdir / "obj.o";

  OICompiler compiler{symbols, {}};

  EXPECT_TRUE(compiler.compile(code, sourcePath, objectPath));

  const size_t relocSlabSize = 4096;
  void* relocSlab =
      mmap(nullptr, relocSlabSize, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  EXPECT_NE(relocSlab, nullptr);

  auto relocResult =
      compiler.applyRelocs((uintptr_t)relocSlab, {objectPath}, {});
  EXPECT_TRUE(relocResult.has_value());

  auto& [_, segs, jitSymbols] = relocResult.value();

  EXPECT_EQ(segs.size(), 1);
  EXPECT_EQ(jitSymbols.size(), 3);  // 2 functions + 1 static array

  {
    const auto& lastSeg = segs.back();
    auto lastSegLimit = lastSeg.RelocAddr + lastSeg.Size;
    auto relocLimit = (uintptr_t)relocSlab + relocSlabSize;
    EXPECT_LE(lastSegLimit, relocLimit);
  }

  for (const auto& [Base, Reloc, Size] : segs)
    std::memcpy((void*)Reloc, (void*)Base, Size);

  {
    auto symAddr = jitSymbols.at("constant");
    EXPECT_EQ(((jitFunc)symAddr)(), 42);
  }

  {
    auto symAddr = jitSymbols.at("sumXs");
    EXPECT_EQ(((jitFunc)symAddr)(), 15);
  }

  munmap(relocSlab, relocSlabSize);
}

TEST(CompilerTest, CompileAndRelocateMultipleObjs) {
  auto symbols = std::make_shared<SymbolService>(getpid());

  auto codeX = R"(
    extern "C" {
    static int xs[] = {1, 2, 3, 4, 5};
    int sumXs() {
      volatile int sum = 0;
      for (int i = 0; i < 5; i++)
        sum += xs[i];
      return sum;
    }
    }
  )";

  auto codeY = R"(
    extern "C" {
    static int ys[] = {10, 11, 12, 13, 14, 15};
    int sumYs() {
      volatile int sum = 0;
      for (int i = 0; i < 6; i++)
        sum += ys[i];
      return sum;
    }
    }
  )";

  auto tmpdir = fs::temp_directory_path() / "test-XXXXXX";
  EXPECT_NE(mkdtemp(const_cast<char*>(tmpdir.c_str())), nullptr);

  auto sourceXPath = tmpdir / "srcX.cpp";
  auto objectXPath = tmpdir / "objX.o";

  auto sourceYPath = tmpdir / "srcY.cpp";
  auto objectYPath = tmpdir / "objY.o";

  OICompiler compiler{symbols, {}};
  EXPECT_TRUE(compiler.compile(codeX, sourceXPath, objectXPath));
  EXPECT_TRUE(compiler.compile(codeY, sourceYPath, objectYPath));

  const size_t relocSlabSize = 8192;
  void* relocSlab =
      mmap(nullptr, relocSlabSize, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  EXPECT_NE(relocSlab, nullptr);

  auto relocResult = compiler.applyRelocs((uintptr_t)relocSlab,
                                          {objectXPath, objectYPath}, {});
  EXPECT_TRUE(relocResult.has_value());

  auto& [_, segs, jitSymbols] = relocResult.value();

  EXPECT_EQ(segs.size(), 2);
  EXPECT_EQ(jitSymbols.size(), 4);  // 2 functions + 2 static array

  {
    const auto& lastSeg = segs.back();
    auto lastSegLimit = lastSeg.RelocAddr + lastSeg.Size;
    auto relocLimit = (uintptr_t)relocSlab + relocSlabSize;
    EXPECT_LE(lastSegLimit, relocLimit);
  }

  for (const auto& [Base, Reloc, Size] : segs)
    std::memcpy((void*)Reloc, (void*)Base, Size);

  {
    auto symAddr = jitSymbols.at("sumXs");
    EXPECT_EQ(((jitFunc)symAddr)(), 15);
  }

  {
    auto symAddr = jitSymbols.at("sumYs");
    EXPECT_EQ(((jitFunc)symAddr)(), 75);
  }

  munmap(relocSlab, relocSlabSize);
}

TEST(CompilerTest, LocateOpcodes) {
  const std::array retInsts = {
      std::array{0xC2_b}, /* Return from near procedure, with immediate value */
      std::array{0xC3_b}, /* Return from near procedure */
      std::array{0xCA_b}, /* Return from far procedure, with immediate value */
      std::array{0xCB_b}, /* Return from far procedure */
  };

  { /* Can trivially locate 0xC2? */
    const std::array insts = {0xC2_b, 0xAA_b, 0xBB_b};
    auto locs = OICompiler::locateOpcodes(insts, retInsts);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 1);
    ASSERT_EQ(locs->at(0), 0);
  }

  { /* Can trivially locate 0xC3? */
    const std::array insts = {0xC3_b};
    auto locs = OICompiler::locateOpcodes(insts, retInsts);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 1);
    ASSERT_EQ(locs->at(0), 0);
  }

  { /* Can trivially locate 0xCA? */
    const std::array insts = {0xCA_b, 0xAA_b, 0xBB_b};
    auto locs = OICompiler::locateOpcodes(insts, retInsts);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 1);
    ASSERT_EQ(locs->at(0), 0);
  }

  { /* Can trivially locate 0xCB? */
    const std::array insts = {0xCB_b};
    auto locs = OICompiler::locateOpcodes(insts, retInsts);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 1);
    ASSERT_EQ(locs->at(0), 0);
  }

  // clang-format off
  const std::array insts = {
    0x55_b,                             /* push   rbp */
    0xba_b,0x22_b,0x00_b,0x00_b,0x00_b, /* mov    edx,0x22 */
    0x48_b,0x89_b,0xe5_b,               /* mov    rbp,rsp */
    0x41_b,0x54_b,                      /* push   r12 */
    0x49_b,0x89_b,0xf4_b,               /* mov    r12,rsi */
    0xbe_b,0x78_b,0xcd_b,0x20_b,0x00_b, /* mov    esi,0x214710 */
    0x48_b,0x83_b,0xec_b,0x18_b,        /* sub    rsp,0x18 */
    0x89_b,0x7d_b,0xec_b,               /* mov    DWORD PTR [rbp-0x14],edi */
    0xbf_b,0xc0_b,0xe4_b,0x2c_b,0x00_b, /* mov    edi,0x3fa6c0 */
    0xe8_b,0xac_b,0x0e_b,0x07_b,0x00_b, /* call   0x3eb260 <_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l@plt> */
    0x4c_b,0x89_b,0xe6_b,               /* mov    rsi,r12 */
    0x48_b,0x8d_b,0x7d_b,0xec_b,        /* lea    rdi,[rbp-0x14] */
    0xe8_b,0x40_b,0x96_b,0x02_b,0x00_b, /* call   0x2ade60 <_ZN7testing14InitGoogleMockEPiPPc> */
    0xe8_b,0x3b_b,0x3b_b,0x04_b,0x00_b, /* call   0x2c80d0 <_ZN7testing8UnitTest11GetInstanceEv> */
    0x48_b,0x89_b,0xc7_b,               /* mov    rdi,rax */
    0xe8_b,0x73_b,0x18_b,0x06_b,0x00_b, /* call   0x2e5e10 <_ZN7testing8UnitTest3RunEv> */
    0x48_b,0x83_b,0xc4_b,0x18_b,        /* add    rsp,0x18 */
    0x41_b,0x5c_b,                      /* pop    r12 */
    0x5d_b,                             /* pop    rbp */
    0xc3_b,                             /* ret */
  };
  // clang-format on

  { /* Empty needles is ok? */
    const std::array<std::array<uint8_t, 1>, 0> needles;
    auto locs = OICompiler::locateOpcodes(insts, needles);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 0);
  }

  { /* Empty text is ok? */
    const std::array<uint8_t, 0> emptyText;
    auto locs = OICompiler::locateOpcodes(emptyText, retInsts);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 0);
  }

  { /* Doesn't locate bytes within an instruction */
    const std::array<std::array<uint8_t, 1>, 1> needles = {{{0x00_b}}};
    auto locs = OICompiler::locateOpcodes(insts, needles);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 0);
  }

  { /* Large range of differently sized needles */
    const std::array needles = {
        std::vector{0x41_b, 0x54_b}, /* push r12:     1 instance */
        std::vector{0x48_b, 0x83_b, 0xec_b,
                    0x18_b},         /* sub rsp,0x18: 1 instance */
        std::vector(1, 0xe8_b),      /* call:         4 instances */
        std::vector{0x41_b, 0x5c_b}, /* pop r12:      1 instance */
    };
    auto locs = OICompiler::locateOpcodes(insts, needles);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 7);
    ASSERT_EQ(locs->at(0), 9);
    ASSERT_EQ(locs->at(1), 19);
    ASSERT_EQ(locs->at(2), 31);
    ASSERT_EQ(locs->at(3), 43);
    ASSERT_EQ(locs->at(4), 48);
    ASSERT_EQ(locs->at(5), 56);
    ASSERT_EQ(locs->at(6), 65);
  }

  { /* No double counting */
    const std::array needles = {
        std::vector(1, 0xba_b),
        std::vector(1, 0xba_b),
        std::vector{0xba_b, 0x22_b},
    };
    auto locs = OICompiler::locateOpcodes(insts, needles);
    ASSERT_TRUE(locs.has_value());
    ASSERT_EQ(locs->size(), 1);
    ASSERT_EQ(locs->at(0), 1);
  }
}
