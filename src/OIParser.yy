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

/*
 * This is the bison grammar responsible for generating the ObjectIntrospection::OIParser class.
 * This class gives us a number of things worth calling out here if only to
 * remind me later :-):
 *
 * - A variant interface that replaces the C union interface for the
 * parsers semantic values. Enabled by setting 'api.value.type variant' below.
 */
%skeleton "lalr1.cc"
%defines
%define api.namespace {ObjectIntrospection}
%define api.parser.class {OIParser}
%define parse.trace
%define parse.error verbose
%define parse.lac full

%code requires{
  #include <list>
  namespace ObjectIntrospection {
    class OIScanner;
  }
  class ParseData;
}

/*
 * ObjectIntrospection::OI_Parser constructor parameters. The scanner object is produced
 * by flex and is derived from the yyFlexLexer class. The parser calls
 * its yylex() implementation to generate input tokens. The ParseData
 * object is spopulated by the lexer/parser introspection specifications
 * specified in the input file.
 */
%parse-param { OIScanner  &scanner  }
%parse-param { ParseData  &pdata  }

%code{
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <glog/logging.h>
#include "OIParser.h"
#include "OILexer.h"

#undef yylex
#define yylex scanner.yylex
}

%define api.value.type variant
%define parse.assert
%locations

%token <char>OI_COLON
%token <std::string>OI_PROBETYPE
%token <std::string>OI_FUNC
%token <std::list<std::string>>OI_ARG
%token <char>OI_COMMA
%token OI_EOF 0

%type <std::list<std::string>> oi_args

%%

script: oi_blocks OI_EOF

oi_blocks: oi_block | oi_blocks oi_block

oi_args: OI_ARG OI_COMMA oi_args
          {
            $$ = std::move($3);
            $$.push_front(std::move($1.front()));
          }
        | OI_ARG;

oi_block: OI_PROBETYPE OI_COLON OI_FUNC OI_COLON oi_args
          {
            pdata.addReq(std::move($1), std::move($3), std::move($5));
          }
        | OI_PROBETYPE OI_COLON OI_FUNC
          {
            pdata.addReq(std::move($1), std::move($3), {});
          }
        ;
%%


void
ObjectIntrospection::OIParser::error(const location_type &l, const std::string &err_message)
{
  LOG(ERROR) << "OI Parse Error: " << err_message << " at " << l;
}
