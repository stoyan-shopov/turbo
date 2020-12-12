/*
 * Copyright (C) 2020 Stoyan Shopov <stoyan.shopov@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Lex file based on this source:
 * http://www.quut.com/c/ANSI-C-grammar-l-1999.html
 * Thanks!
 */

D			[0-9]
L			[a-zA-Z_]
H			[a-fA-F0-9]
E			([Ee][+-]?{D}+)
P                       ([Pp][+-]?{D}+)
FS			(f|F|l|L)
IS                      ((u|U)|(u|U)?(l|L|ll|LL)|(l|L|ll|LL)(u|U))

%option header-file="cscanner.hxx" outfile="cscanner.cxx"
%option noyywrap
%option reentrant
%option extra-type="std::string *"

%{
#include <stdio.h>
#include <string>

namespace clex
{
	enum FORMAT_TYPE_ENUM
	{
		INVALID	=	0,
		COMMENT,
		SINGLE_LINE_COMMENT,
		PREPROCESSOR_START,
		PREPROCESSOR,
		KEYWORD_GROUP_A,
		KEYWORD_GROUP_B,
		KEYWORD_GROUP_C,
		KEYWORD_GROUP_D,
		PUNCTUATION,
		NUMBER,
		STRING,
		FORMAT_TYPES_COUNT,
	};
}
using namespace clex;

static void emit(int c, yyscan_t yyscanner);
static void emit_raw_string(const char * s, yyscan_t yyscanner);
static void emit_escaped(int c, yyscan_t yyscanner);
static void emit_format_begin(enum FORMAT_TYPE_ENUM format, yyscan_t yyscanner);
static void emit_format_end(yyscan_t yyscanner);
static void emit_yytext(yyscan_t yyscanner);
static void emit_formatted_yytext(enum FORMAT_TYPE_ENUM format, yyscan_t yyscanner);
static void comment(yyscan_t yyscanner);
static void preprocessor(yyscan_t yyscanner);

%}

%%
"/*"			{ /* */ comment(yyscanner); }
"//"[^\n]*              { emit_formatted_yytext(SINGLE_LINE_COMMENT, yyscanner); }


"auto"			{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"_Bool"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"break"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"case"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"char"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"_Complex"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"const"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"continue"		{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"default"		{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"do"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"double"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"else"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"enum"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"extern"		{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"float"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"for"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"goto"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"if"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"_Imaginary"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"inline"		{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"int"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"long"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"register"		{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"restrict"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"return"		{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"short"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"signed"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"sizeof"		{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"static"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"struct"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"switch"		{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"typedef"		{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"union"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"unsigned"		{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"void"			{ emit_formatted_yytext(KEYWORD_GROUP_B, yyscanner); }
"volatile"		{ emit_formatted_yytext(KEYWORD_GROUP_C, yyscanner); }
"while"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }

"true"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }
"false"			{ emit_formatted_yytext(KEYWORD_GROUP_A, yyscanner); }

{L}({L}|{D})*		{ emit_formatted_yytext(KEYWORD_GROUP_D, yyscanner); }

0[xX]{H}+{IS}?		{ emit_formatted_yytext(NUMBER, yyscanner); }
0[0-7]*{IS}?		{ emit_formatted_yytext(NUMBER, yyscanner); }
[1-9]{D}*{IS}?		{ emit_formatted_yytext(NUMBER, yyscanner); }
L?'(\\.|[^\\'\n])+'	{ emit_formatted_yytext(STRING, yyscanner); }

{D}+{E}{FS}?		{ emit_formatted_yytext(NUMBER, yyscanner); }
{D}*"."{D}+{E}?{FS}?	{ emit_formatted_yytext(NUMBER, yyscanner); }
{D}+"."{D}*{E}?{FS}?	{ emit_formatted_yytext(NUMBER, yyscanner); }
0[xX]{H}+{P}{FS}?	{ emit_formatted_yytext(NUMBER, yyscanner); }
0[xX]{H}*"."{H}+{P}{FS}?     { emit_formatted_yytext(NUMBER, yyscanner); }
0[xX]{H}+"."{H}*{P}{FS}?     { emit_formatted_yytext(NUMBER, yyscanner); }


L?\"(\\.|[^\\"\n])*\"	{ emit_formatted_yytext(STRING, yyscanner); }

"#"[^ \t\v\n\f]*	{ preprocessor(yyscanner); }

"..."			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
">>="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"<<="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"+="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"-="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"*="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"/="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"%="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"&="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"^="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"|="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
">>"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"<<"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"++"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"--"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"->"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"&&"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"||"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"<="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
">="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"=="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"!="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
";"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
("{"|"<%")		{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
("}"|"%>")		{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
","			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
":"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"="			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"("			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
")"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
("["|"<:")		{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
("]"|":>")		{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"."			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"&"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"!"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"~"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"-"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"+"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"*"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"/"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"%"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"<"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
">"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"^"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"|"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }
"?"			{ emit_formatted_yytext(PUNCTUATION, yyscanner); }

[ \t\v\n\f]		{ emit(* yytext, yyscanner); }
.			{ emit(* yytext, yyscanner); }

%%

static void emit(int c, yyscan_t yyscanner)
{
	yyget_extra(yyscanner)->operator+=(c);
}

static void emit_raw_string(const char * s, yyscan_t yyscanner)
{
	while (* s)
		emit(* s ++, yyscanner);
}


static void emit_escaped(int c, yyscan_t yyscanner)
{
	switch (c)
	{
		default: emit(c, yyscanner); break;
		case '<': emit_raw_string("&lt;", yyscanner); break;
		case '>': emit_raw_string("&gt;", yyscanner); break;
		case '&': emit_raw_string("&amp;", yyscanner); break;
	}
}

static void emit_format_begin(enum FORMAT_TYPE_ENUM format, yyscan_t yyscanner)
{
static const char * format_strings[FORMAT_TYPES_COUNT] = {
	[FORMAT_TYPE_ENUM::INVALID] =			"inv",
	[FORMAT_TYPE_ENUM::COMMENT] =			"com",
	[FORMAT_TYPE_ENUM::SINGLE_LINE_COMMENT] =	"slc",
	[FORMAT_TYPE_ENUM::PREPROCESSOR_START] =	"pps",
	[FORMAT_TYPE_ENUM::PREPROCESSOR] =		"ppc",
	[FORMAT_TYPE_ENUM::KEYWORD_GROUP_A] =		"kwa",
	[FORMAT_TYPE_ENUM::KEYWORD_GROUP_B] =		"kwb",
	[FORMAT_TYPE_ENUM::KEYWORD_GROUP_C] =		"kwc",
	[FORMAT_TYPE_ENUM::KEYWORD_GROUP_D] =		"kwd",
	[FORMAT_TYPE_ENUM::PUNCTUATION] =		"opt",
	[FORMAT_TYPE_ENUM::NUMBER] =			"num",
	[FORMAT_TYPE_ENUM::STRING] =			"str",
};
	if (format < 0 || format >= FORMAT_TYPES_COUNT)
		format = FORMAT_TYPE_ENUM::INVALID;

	emit_raw_string("<span class=\"hl ", yyscanner);
	emit_raw_string(format_strings[format], yyscanner);
	emit_raw_string("\">", yyscanner);
}

static void emit_format_end(yyscan_t yyscanner)
{
	emit_raw_string("</span>", yyscanner);
}

static void emit_yytext(yyscan_t yyscanner)
{
	const char * p = yyget_text(yyscanner);
	while (* p)
		emit_escaped(* p ++, yyscanner);
}

static void emit_formatted_yytext(enum FORMAT_TYPE_ENUM format, yyscan_t yyscanner)
{
	emit_format_begin(format, yyscanner);
	emit_yytext(yyscanner);
	emit_format_end(yyscanner);
}

/*! !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * THIS IS BUGGY, IT CRASHES BADLY IN CASE OF UNTERMINATED COMMENTS
 * DEBUG THIS
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
static void comment(yyscan_t yyscanner)
{
	char c, prev = 0;
	emit_format_begin(FORMAT_TYPE_ENUM::COMMENT, yyscanner);
	emit_yytext(yyscanner);

	while ((c = yyinput(yyscanner)) != 0)      /* (EOF maps to 0) */
	{
		if (c == '/' && prev == '*')
		{
			emit_escaped(c, yyscanner);
			emit_format_end(yyscanner);
			return;
		}
		if (c == '\n')
		{
			emit_format_end(yyscanner);
			emit_escaped(c, yyscanner);
			emit_format_begin(FORMAT_TYPE_ENUM::COMMENT, yyscanner);
		}
		else
			emit_escaped(c, yyscanner);
		prev = c;
	}
	emit_raw_string("unterminated comment", yyscanner);
	emit_format_end(yyscanner);
}

/*! \todo This is broken for dos line endings (<CR><LF>). */
static void preprocessor(yyscan_t yyscanner)
{
	char c, prev = 0;
	emit_format_begin(FORMAT_TYPE_ENUM::PREPROCESSOR_START, yyscanner);
	emit_yytext(yyscanner);
	emit_format_end(yyscanner);
	emit_format_begin(FORMAT_TYPE_ENUM::PREPROCESSOR, yyscanner);

	while ((c = yyinput(yyscanner)) != 0)      /* (EOF maps to 0) */
	{
		if (c == '\n' && prev == '\\')
		{
			emit_format_end(yyscanner);
			emit(c, yyscanner);
			emit_format_begin(FORMAT_TYPE_ENUM::PREPROCESSOR, yyscanner);
		}
		else if (c == '\n')
		{
			emit_format_end(yyscanner);
			emit_escaped(c, yyscanner);
			return;
		}
		else
			emit_escaped(c, yyscanner);
		prev = c;
	}
	emit_format_end(yyscanner);
}


#ifdef CLEX_TEST_DRIVE
#include <stdio.h>
#define MAX_SCANNED_SOURCE_CODE_FILE_SIZE	10 * 1024 * 1024
/* Reserve one byte for the string null terminator. */
static char test_string[MAX_SCANNED_SOURCE_CODE_FILE_SIZE + 1];

static const char * html_header =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<meta charset=\"ISO-8859-1\">\n"
"<title>Source file</title>\n"
"<link rel=\"stylesheet\" type=\"text/css\" href=\"highlight.css\">\n"
"</head>\n"
"<body class=\"hl\">\n"
"<pre class=\"hl\">"
;

static const char * html_footer =
"</pre>\n"
"</body>\n"
"</html>\n"
"<!--HTML generated by XXX: todo - put an appropriate reference here-->"
;

int main(int argc, char * argv[])
{
FILE * infile;

	if (argc != 2)
	{
		printf("No source code file specified.\nUsage: %s <source-code-file-to-scan>\n", * argv);
		exit(1);
	}
	if (!(infile = fopen(argv[1], "r+b")))
	{
		printf("Failed to open input file %s\n", argv[1]);
		exit(2);
	}

	fread(test_string, 1, MAX_SCANNED_SOURCE_CODE_FILE_SIZE, infile);
	fclose(infile);

	yyscan_t scanner;
	std::string s;
	yylex_init_extra(& s, &scanner);

	s = html_header;
	yy_scan_string(test_string, scanner);
	yylex(scanner);
	yylex_destroy(scanner);
	s += html_footer;
	printf("%s", s.c_str());
	return 0;
}
#endif

