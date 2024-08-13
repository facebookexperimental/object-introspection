/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

/* It looks like the yyFlexLexerOnce method is not the correct way of doing
 this but it works for now. Fix it in the future. */
#if !defined(yyFlexLexerOnce)
#include <FlexLexer.h>
#endif

/* #pragma once
#include <FlexLexer.h> */

#include "OIParser.tab.hh"
#include "location.hh"

namespace oi::detail {

class OIScanner : public yyFlexLexer {
 public:
  OIScanner(std::istream* in) : yyFlexLexer(in) {};

  virtual ~OIScanner() {};

  // get rid of override virtual function warning
  using FlexLexer::yylex;

  virtual int yylex(OIParser::semantic_type* const lval,
                    OIParser::location_type* location);
  // YY_DECL defined in OILexer.l
  // Method body created by flex in OILexer.yy.cc

 private:
  /* yyval ptr */
  OIParser::semantic_type* yylval = nullptr;
};

}  // namespace oi::detail
