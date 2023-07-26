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

%{
/* C++ string header, for string ops below */
#include <string>
/* Implementation of yyFlexScanner */
#include "oi/OILexer.h"
#undef  YY_DECL
#define YY_DECL \
  int oi::detail::OIScanner::yylex(oi::detail::OIParser::semantic_type * const lval, \
                           oi::detail::OIParser::location_type *loc )

/* typedef to make the returns for the tokens shorter */
using token = oi::detail::OIParser::token;

/* update location on matching */
#define YY_USER_ACTION loc->step(); loc->columns(yyleng);
%}

%option debug
%option nodefault
%option yyclass="oi::detail::OIScanner"
%option noyywrap
%option c++

%x COMMENT

%%
%{ /** Code executed at the beginning of yylex **/
  yylval = lval;
%}

(arg[0-9]|retval|this) {
            yylval->emplace<std::list<std::string>>(std::list<std::string>{yytext});
            return( token::OI_ARG );
          }
 /*
  * We very much rely on the fact that the probetype rule sits before the
  * function matching rule below as they'll both match. In that case lex will
  * return the first.
  */
(return|entry|global) {
            yylval->emplace<std::string>(yytext);
            return( token::OI_PROBETYPE );
					}

 /* oid uses mangled symbols to specify the function  */
[a-zA-Z_0-9.$]+ {
            yylval->emplace<std::string>(yytext);
            return( token::OI_FUNC );
          }

":"       {
            yylval->emplace<char>(yytext[0]);
            return (token::OI_COLON);
          }

<*>\n     {
            // Update line number
            loc->lines();
          }

,         {
            yylval->emplace<char>(yytext[0]);
            return(token::OI_COMMA);
          }

"//"[^\n]* /* skip one-line comments */

"/*"                  BEGIN(COMMENT); /* skip multi-lines comments */
<COMMENT>[^*\n]*      /* skip comment's content */
<COMMENT>"*"+[^*/\n]* /* skip '*' */
<COMMENT>"*"+"/"      BEGIN(INITIAL);

[ \t]+     /* skip whitespace */

<<EOF>>   {
            if (YYSTATE == COMMENT) {
              throw oi::detail::OIParser::syntax_error(
                *loc, "unterminated /* comment");
            } else {
              return( token::OI_EOF );
            }
          }
%%
