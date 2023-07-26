#include <gmock/gmock.h>
#include <gtest/gtest-death-test.h>
#include <gtest/gtest.h>

#include <sstream>

#include "oi/OILexer.h"
#include "oi/OIParser.h"

using ::testing::HasSubstr;

using namespace oi::detail;

// Utilities
static ParseData parseString(const std::string& script) {
  ParseData pdata;
  std::istringstream input{script};
  OIScanner scanner{&input};
  OIParser parser{scanner, pdata};

  EXPECT_EQ(parser.parse(), 0);

  return pdata;
}

static std::string parseBadString(const std::string& script) {
  ParseData pdata;
  std::istringstream input{script};
  OIScanner scanner{&input};
  OIParser parser{scanner, pdata};

  testing::internal::CaptureStderr();
  EXPECT_NE(parser.parse(), 0);
  return testing::internal::GetCapturedStderr();
}

static void EXPECT_REQ_EQ(const prequest& req,
                          const std::string& type,
                          const std::string& func,
                          const std::vector<std::string>& args) {
  EXPECT_EQ(req.type, type);
  EXPECT_EQ(req.func, func);
  EXPECT_EQ(req.args, args);
}

// Unit Tests
TEST(ParserTest, EntryParsing) {
  {  // Entry probe with arg0
    ParseData pdata = parseString("entry:function:arg0");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "entry", "function", {"arg0"});
  }

  {  // Entry probe with this
    ParseData pdata = parseString("entry:function:this");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "entry", "function", {"this"});
  }
}

TEST(ParserTest, ReturnParsing) {
  {  // Return probe with arg
    ParseData pdata = parseString("return:function:arg0");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "return", "function", {"arg0"});
  }

  {  // Return probe with this
    ParseData pdata = parseString("return:function:this");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "return", "function", {"this"});
  }

  {  // Return probe with retval
    ParseData pdata = parseString("return:function:retval");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "return", "function", {"retval"});
  }
}

TEST(ParserTest, GlobalParsing) {
  ParseData pdata = parseString("global:varName");
  EXPECT_EQ(pdata.numReqs(), 1);
  EXPECT_REQ_EQ(pdata.getReq(0), "global", "varName", {});
}

TEST(ParserTest, MangledFunc) {
  {
    ParseData pdata = parseString(
        "entry:_Z7doStuffR3FooRSt6vectorISt3mapINSt7__cxx1112basic_"
        "stringIcSt11char_traitsIcESaIcEEES8_St4lessIS8_ESaISt4pairIKS8_S8_"
        "EEESaISF_EERS1_IS8_SaIS8_EERS1_ISB_IS8_dESaISM_EE:arg9");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(
        pdata.getReq(0), "entry",
        "_Z7doStuffR3FooRSt6vectorISt3mapINSt7__cxx1112basic_stringIcSt11char_"
        "traitsIcESaIcEEES8_St4lessIS8_ESaISt4pairIKS8_S8_EEESaISF_EERS1_IS8_"
        "SaIS8_EERS1_ISB_IS8_dESaISM_EE",
        {"arg9"});
  }

  {
    ParseData pdata = parseString("entry:func.with.dots.1234:arg0");
    EXPECT_EQ(pdata.numReqs(), 1);
    EXPECT_REQ_EQ(pdata.getReq(0), "entry", "func.with.dots.1234", {"arg0"});
  }
}

TEST(ParserTest, IgnoreWhitespaces) {
  ParseData pdata = parseString(
      "\n"
      "entry\n"
      "  :function \n"
      "  :    arg1   ");
  EXPECT_EQ(pdata.numReqs(), 1);
  EXPECT_REQ_EQ(pdata.getReq(0), "entry", "function", {"arg1"});
}

TEST(ParserTest, IgnoreComments) {
  ParseData pdata = parseString(
      "// Testing comments\n"
      "/* Multi-lines comments\n"
      " * Also work! */\n"
      "entry/* Location of the probe */:"
      "function/* Name of the probed function */:"
      "arg8// Function argument to probe");
  EXPECT_EQ(pdata.numReqs(), 1);
  EXPECT_REQ_EQ(pdata.getReq(0), "entry", "function", {"arg8"});
}

TEST(ParserTest, MultipleProbes) {
  ParseData pdata = parseString(
      "entry:f1:arg0 "
      "return:f2:arg1 "
      "global:g1 "
      "return:f3:retval");
  EXPECT_EQ(pdata.numReqs(), 4);
  EXPECT_REQ_EQ(pdata.getReq(0), "entry", "f1", {"arg0"});
  EXPECT_REQ_EQ(pdata.getReq(1), "return", "f2", {"arg1"});
  EXPECT_REQ_EQ(pdata.getReq(2), "global", "g1", {});
  EXPECT_REQ_EQ(pdata.getReq(3), "return", "f3", {"retval"});
}

TEST(ParserTest, MultipleArgs) {
  ParseData pdata = parseString("entry:f1:arg0,arg1,arg2");
  EXPECT_EQ(pdata.numReqs(), 1);
  EXPECT_REQ_EQ(pdata.getReq(0), "entry", "f1", {"arg0", "arg1", "arg2"});
}

TEST(ParserTest, MultipleProbesAndArgs) {
  ParseData pdata = parseString(
      "entry:f1:arg0,arg1,arg2 "
      "return:f2:arg9,arg8");
  EXPECT_EQ(pdata.numReqs(), 2);
  EXPECT_REQ_EQ(pdata.getReq(0), "entry", "f1", {"arg0", "arg1", "arg2"});
  EXPECT_REQ_EQ(pdata.getReq(1), "return", "f2", {"arg9", "arg8"});
}

TEST(ParserTest, NoProbe) {
  EXPECT_THAT(
      parseBadString(""),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_EOF, expecting "
                "OI_PROBETYPE at 1.1"));
}

TEST(ParserTest, InvalidProbeType) {
  EXPECT_THAT(
      parseBadString("arg0:f1:arg1"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_ARG, expecting "
                "OI_PROBETYPE at 1.1-4"));
  EXPECT_THAT(
      parseBadString("retval:f1:arg1"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_ARG, expecting "
                "OI_PROBETYPE at 1.1-6"));
  EXPECT_THAT(
      parseBadString("xyz:f1:arg1"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, expecting "
                "OI_PROBETYPE at 1.1-3"));
}

TEST(ParserDeathTest, InvalidFunc) {
  // The lexer exits on error, using a death test for that case
  EXPECT_DEATH(parseString("entry:bad-mangled:arg0"), "flex scanner jammed");

  EXPECT_THAT(
      parseBadString("entry:bad mangled:arg0"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, expecting "
                "OI_EOF or OI_COLON or OI_PROBETYPE at 1.11-17"));
  EXPECT_THAT(
      parseBadString("entry:bad/***/mangled:arg0"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, expecting "
                "OI_EOF or OI_COLON or OI_PROBETYPE at 1.15-21"));
}

TEST(ParserTest, InvalidArg) {
  EXPECT_THAT(parseBadString("entry:f1:arg99"),
              HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, "
                        "expecting OI_ARG at "
                        "1.10-14"));
  EXPECT_THAT(parseBadString("entry:f1:arg-1"),
              HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, "
                        "expecting OI_ARG at "
                        "1.10-12"));
  EXPECT_THAT(
      parseBadString("entry:f1:xyz"),
      HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, expecting "
                "OI_ARG at 1.10-12"));
  EXPECT_THAT(parseBadString("return:f2:ret"),
              HasSubstr("OI Parse Error: syntax error, unexpected OI_FUNC, "
                        "expecting OI_ARG at "
                        "1.11-13"));
}

TEST(ParserTest, UnterminatedComment) {
  EXPECT_THAT(parseBadString("entry:f1:arg0 /*"),
              HasSubstr("OI Parse Error: unterminated /* comment at 1.15-16"));
}
