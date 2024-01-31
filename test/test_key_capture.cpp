#include <gtest/gtest.h>

#include "oi/type_graph/KeyCapture.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(KeyCaptureTest, InClass) {
  std::vector<OICodeGen::Config::KeyToCapture> keysToCapture = {
      {"MyClass", "b"},
  };
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  test(KeyCapture::createPass(keysToCapture, containerInfos),
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 8)
[1]     Container: std::map (size: 24)
          Param
            Primitive: int32_t
          Param
            Primitive: int32_t
      Member: c (offset: 8)
        [1]
)",
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 8)
        CaptureKeys
[1]       Container: std::map (size: 24)
            Param
              Primitive: int32_t
            Param
              Primitive: int32_t
      Member: c (offset: 8)
        [1]
)");
}

TEST(KeyCaptureTest, MapInMap) {
  std::vector<OICodeGen::Config::KeyToCapture> keysToCapture = {
      {"MyClass", "a"},
  };
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  test(KeyCapture::createPass(keysToCapture, containerInfos),
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 8)
[1]     Container: std::map (size: 24)
          Param
            Primitive: int32_t
          Param
[2]         Container: std::map (size: 24)
              Param
                Primitive: int32_t
              Param
                Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 8)
        CaptureKeys
[1]       Container: std::map (size: 24)
            Param
              Primitive: int32_t
            Param
[2]           Container: std::map (size: 24)
                Param
                  Primitive: int32_t
                Param
                  Primitive: int32_t
)");
}

TEST(KeyCaptureTest, Typedef) {
  std::vector<OICodeGen::Config::KeyToCapture> keysToCapture = {
      {"MyClass", "a"},
  };
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  test(KeyCapture::createPass(keysToCapture, containerInfos),
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 8)
[1]     Typedef: MyTypeDef 
[2]       Container: std::map (size: 24)
            Param
              Primitive: int32_t
            Param
              Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 12)
      Member: a (offset: 8)
        CaptureKeys
[1]       Typedef: MyTypeDef 
[2]         Container: std::map (size: 24)
              Param
                Primitive: int32_t
              Param
                Primitive: int32_t
)");
}

TEST(KeyCaptureTest, TopLevel) {
  std::vector<OICodeGen::Config::KeyToCapture> keysToCapture = {
      {{}, {}, true},
  };
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  test(KeyCapture::createPass(keysToCapture, containerInfos),
       R"(
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
)",
       R"(
    CaptureKeys
[0]   Container: std::map (size: 24)
        Param
          Primitive: int32_t
        Param
          Primitive: int32_t
)");
}

TEST(KeyCaptureTest, TopLevelNotCaptured) {
  std::vector<OICodeGen::Config::KeyToCapture> keysToCapture = {
      {"MyClass", "a"},
  };
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  testNoChange(KeyCapture::createPass(keysToCapture, containerInfos), R"(
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
)");
}
