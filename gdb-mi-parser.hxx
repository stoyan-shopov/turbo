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

#ifndef GDBMIPARSER_HXX
#define GDBMIPARSER_HXX

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <ctype.h>

/* From the gdb documentation:

27.2.1 GDB/MI Input Syntax

command →

    cli-command | mi-command
cli-command →

    [ token ] cli-command nl, where cli-command is any existing GDB CLI command.
mi-command →

    [ token ] "-" operation ( " " option )* [ " --" ] ( " " parameter )* nl
token →

    "any sequence of digits"
option →

    "-" parameter [ " " parameter ]
parameter →

    non-blank-sequence | c-string
operation →

    any of the operations described in this chapter
non-blank-sequence →

    anything, provided it doesn’t contain special characters such as "-", nl, """ and of course " "
c-string →

    """ seven-bit-iso-c-string-content """
nl →

    CR | CR-LF

Notes:

    The CLI commands are still handled by the MI interpreter; their output is described below.
    The token, when present, is passed back when the command finishes.
    Some MI commands accept optional arguments as part of the parameter list. Each option is identified by a leading ‘-’ (dash) and may be followed by an optional argument parameter. Options occur first in the parameter list and can be delimited from normal parameters using ‘--’ (this is useful when some parameters begin with a dash).

Pragmatics:

    We want easy access to the existing CLI syntax (for debugging).
    We want it to be easy to spot a MI operation.

27.2.2 GDB/MI Output Syntax

The output from GDB/MI consists of zero or more out-of-band records followed, optionally, by a single result record. This result record is for the most recent command. The sequence of output records is terminated by ‘(gdb)’.

If an input command was prefixed with a token then the corresponding output for that command will also be prefixed by that same token.

output →

    ( out-of-band-record )* [ result-record ] "(gdb)" nl
result-record →

    [ token ] "^" result-class ( "," result )* nl
out-of-band-record →

    async-record | stream-record
async-record →

    exec-async-output | status-async-output | notify-async-output
exec-async-output →

    [ token ] "*" async-output nl
status-async-output →

    [ token ] "+" async-output nl
notify-async-output →

    [ token ] "=" async-output nl
async-output →

    async-class ( "," result )*G
result-class →

    "done" | "running" | "connected" | "error" | "exit"
async-class →

    "stopped" | others (where others will be added depending on the needs—this is still in development).
result →

    variable "=" value
variable →

    string
value →

    const | tuple | list
const →

    c-string
tuple →

    "{}" | "{" result ( "," result )* "}"
list →

    "[]" | "[" value ( "," value )* "]" | "[" result ( "," result )* "]"
stream-record →

    console-stream-output | target-stream-output | log-stream-output
console-stream-output →

    "~" c-string nl
target-stream-output →

    "@" c-string nl
log-stream-output →

    "&" c-string nl
nl →

    CR | CR-LF
token →

    any sequence of digits.

Notes:

    All output sequences end in a single line containing a period.
    The token is from the corresponding request. Note that for all async output, while the token is allowed by the grammar and may be output by future versions of GDB for select async output messages, it is generally omitted. Frontends should treat all async output as reporting general changes in the state of the target and there should be no need to associate async output to any prior command.
    status-async-output contains on-going status information about the progress of a slow operation. It can be discarded. All status output is prefixed by ‘+’.
    exec-async-output contains asynchronous state change on the target (stopped, started, disappeared). All async output is prefixed by ‘*’.
    notify-async-output contains supplementary information that the client should handle (e.g., a new breakpoint information). All notify output is prefixed by ‘=’.
    console-stream-output is output that should be displayed as is in the console. It is the textual response to a CLI command. All the console output is prefixed by ‘~’.
    target-stream-output is the output produced by the target program. All the target output is prefixed by ‘@’.
    log-stream-output is output text coming from GDB’s internals, for instance messages that should be displayed as part of an error log. All the log output is prefixed by ‘&’.
    New GDB/MI commands should only output lists containing values.

See GDB/MI Stream Records, for more details about the various output records.

27.5.2 GDB/MI Stream Records

GDB internally maintains a number of output streams: the console, the target, and the log. The output intended for each of these streams is funneled through the GDB/MI interface using stream records.

Each stream record begins with a unique prefix character which identifies its stream (see GDB/MI Output Syntax). In addition to the prefix, each stream record contains a string-output. This is either raw text (with an implicit new line) or a quoted C string (which does not contain an implicit newline).

"~" string-output

    The console output stream contains text that should be displayed in the CLI console window. It contains the textual responses to CLI commands.
"@" string-output

    The target output stream contains any textual output from the running target. This is only present when GDB’s event loop is truly asynchronous, which is currently only the case for remote targets.
"&" string-output

    The log stream contains debugging messages being produced by GDB’s internals.
*/


/*
 * Sample gdb machine interface log:

arm-none-eabi-gdb --interpreter mi3 blackmagic.elf
(gdb)
target remote localhost:1122
&"target remote localhost:1122\n"
~"Remote debugging using localhost:1122\n"
=thread-group-started,id="i1",pid="42000"
=thread-created,id="1",group-id="i1"
~"f0 (a=27, b=b@entry=2, c=c@entry=5) at main.c:829\n"
~"829\t\treturn a + b * entry_test - c;\n"
*stopped,frame={addr="0x08004b6c",func="f0",args=[{name="a",value="27"},{name="b",value="2"},{name="b@entry",value="2"},{name="c",value="5"},{name="c@entry",value="5"}],file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="829",arch="armv6s-m"},thread-id="1",stopped-threads="all"
^done
(gdb)
bt
&"bt\n"
~"#0  f0 (a=27, b=b@entry=2, c=c@entry=5) at main.c:829\n"
~"#1  0x08004b8a in f3 (a=5, b=b@entry=2, c=<optimized out>) at main.c:834\n"
~"#2  0x08004ba4 in f2 (a=<optimized out>, b=<optimized out>, c=<optimized out>) at main.c:842\n"
~"#3  0x08004bb6 in f1 (a=<optimized out>, b=<optimized out>, c=<optimized out>) at main.c:848\n"
~"#4  0x08004bc0 in gate1 (f=f@entry=0x8004ba7 <f1>, a=a@entry=5, b=b@entry=5, c=c@entry=20) at main.c:853\n"
~"#5  0x08004be4 in gate2 (c=5, b=20, a=5, f=f@entry=0x8004ba7 <f1>) at main.c:858\n"
~"#6  gate3 (f=f@entry=0x8004ba7 <f1>, a=a@entry=5, b=b@entry=0, c=c@entry=4) at main.c:863\n"
~"#7  0x08004c4c in gate4 (c=9, b=4, a=1, f2=<optimized out>, f1=<optimized out>) at main.c:868\n"
~"#8  main (argc=<optimized out>, argv=<optimized out>) at main.c:54\n"
~"Backtrace stopped: Cannot access memory at address 0x20003ffc\n"
^done
(gdb)
-var-create - @ xt_runtime_branch
^done,name="var1",numchild="5",value="{...}",type="const struct word",has_more="0"
(gdb)
-var-list-children --all-values var1
^done,numchild="5",children=[child={name="var1.link",exp="link",numchild="5",value="0x0",type="struct word *"},child={name="var1.1_anonymous",exp="<anonymous union>",numchild="2",value="{...}",type="union {...}"},child={name="var1.2_anonymous",exp="<anonymous struct>",numchild="3",value="{...}",type="struct {...}"},child={name="var1.cfa",exp="cfa",numchild="0",value="0x80089d1 <runtime_branch>",type="void (*)(void)"},child={name="var1.4_anonymous",exp="<anonymous union>",numchild="2",value="{...}",type="union {...}"}],has_more="0"
(gdb)
-var-list-children --all-values var1.1_anonymous
^done,numchild="2",children=[child={name="var1.1_anonymous.name",exp="name",numchild="2",value="0x8014e50 <__compound_literal.4>",type="union cstr *"},child={name="var1.1_anonymous.xname",exp="xname",numchild="1",value="0x8014e50 <__compound_literal.4> \"\\026<<< runtime-branch >>>\"",type="char *"}],has_more="0"
(gdb)
-var-create - @ usb_strings
^done,name="var2",numchild="6",value="[6]",type="const char *[6]",has_more="0"
(gdb)
-var-list-children --all-values var2
^done,numchild="6",children=[child={name="var2.0",exp="0",numchild="1",value="0x8015586 \"Black Sphere Technologies\"",type="const char *"},child={name="var2.1",exp="1",numchild="1",value="0x80155a0 \"Black Magic Probe (vx), (Firmware )\"",type="const char *"},child={name="var2.2",exp="2",numchild="1",value="0x20002114 <serial_no> \"FF00FFFD\"",type="const char *"},child={name="var2.3",exp="3",numchild="1",value="0x80155c4 \"Black Magic GDB Server\"",type="const char *"},child={name="var2.4",exp="4",numchild="1",value="0x80155db \"Black Magic UART Port\"",type="const char *"},child={name="var2.5",exp="5",numchild="1",value="0x80155f1 \"Black Magic Firmware Upgrade (STLINK)\"",type="const char *"}],has_more="0"
(gdb)
-var-create - @ rstack
^done,name="var3",numchild="32",value="[32]",type="cell [32]",has_more="0"
(gdb)
-var-list-children --all-values var3
^done,numchild="32",children=[child={name="var3.0",exp="0",numchild="0",value="0",type="cell"},child={name="var3.1",exp="1",numchild="0",value="0",type="cell"},child={name="var3.2",exp="2",numchild="0",value="0",type="cell"},child={name="var3.3",exp="3",numchild="0",value="0",type="cell"},child={name="var3.4",exp="4",numchild="0",value="0",type="cell"},child={name="var3.5",exp="5",numchild="0",value="0",type="cell"},child={name="var3.6",exp="6",numchild="0",value="0",type="cell"},child={name="var3.7",exp="7",numchild="0",value="0",type="cell"},child={name="var3.8",exp="8",numchild="0",value="0",type="cell"},child={name="var3.9",exp="9",numchild="0",value="0",type="cell"},child={name="var3.10",exp="10",numchild="0",value="0",type="cell"},child={name="var3.11",exp="11",numchild="0",value="0",type="cell"},child={name="var3.12",exp="12",numchild="0",value="0",type="cell"},child={name="var3.13",exp="13",numchild="0",value="0",type="cell"},child={name="var3.14",exp="14",numchild="0",value="0",type="cell"},child={name="var3.15",exp="15",numchild="0",value="0",type="cell"},child={name="var3.16",exp="16",numchild="0",value="0",type="cell"},child={name="var3.17",exp="17",numchild="0",value="0",type="cell"},child={name="var3.18",exp="18",numchild="0",value="0",type="cell"},child={name="var3.19",exp="19",numchild="0",value="0",type="cell"},child={name="var3.20",exp="20",numchild="0",value="0",type="cell"},child={name="var3.21",exp="21",numchild="0",value="0",type="cell"},child={name="var3.22",exp="22",numchild="0",value="0",type="cell"},child={name="var3.23",exp="23",numchild="0",value="0",type="cell"},child={name="var3.24",exp="24",numchild="0",value="0",type="cell"},child={name="var3.25",exp="25",numchild="0",value="0",type="cell"},child={name="var3.26",exp="26",numchild="0",value="0",type="cell"},child={name="var3.27",exp="27",numchild="0",value="0",type="cell"},child={name="var3.28",exp="28",numchild="0",value="0",type="cell"},child={name="var3.29",exp="29",numchild="0",value="0",type="cell"},child={name="var3.30",exp="30",numchild="0",value="0",type="cell"},child={name="var3.31",exp="31",numchild="0",value="0",type="cell"}],has_more="0"
(gdb)
-stack-info-frame
^done,frame={level="0",addr="0x08004b6c",func="f0",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="829",arch="armv6s-m"}
(gdb)
-stack-info-depth
^done,depth="9"
(gdb)
-stack-list-arguments --all-values
^done,stack-args=[frame={level="0",args=[{name="a",value="27"},{name="b",value="2"},{name="b@entry",value="2"},{name="c",value="5"},{name="c@entry",value="5"}]},frame={level="1",args=[{name="a",value="5"},{name="b",value="2"},{name="b@entry",value="2"},{name="c",value="<optimized out>"}]},frame={level="2",args=[{name="a",value="<optimized out>"},{name="b",value="<optimized out>"},{name="c",value="<optimized out>"}]},frame={level="3",args=[{name="a",value="<optimized out>"},{name="b",value="<optimized out>"},{name="c",value="<optimized out>"}]},frame={level="4",args=[{name="f",value="0x8004ba7 <f1>"},{name="f@entry",value="0x8004ba7 <f1>"},{name="a",value="5"},{name="a@entry",value="5"},{name="b",value="5"},{name="b@entry",value="5"},{name="c",value="20"},{name="c@entry",value="20"}]},frame={level="5",args=[{name="c",value="5"},{name="b",value="20"},{name="a",value="5"},{name="f",value="0x8004ba7 <f1>"},{name="f@entry",value="0x8004ba7 <f1>"}]},frame={level="6",args=[{name="f",value="0x8004ba7 <f1>"},{name="f@entry",value="0x8004ba7 <f1>"},{name="a",value="5"},{name="a@entry",value="5"},{name="b",value="0"},{name="b@entry",value="0"},{name="c",value="4"},{name="c@entry",value="4"}]},frame={level="7",args=[{name="c",value="9"},{name="b",value="4"},{name="a",value="1"}]},frame={level="8",args=[{name="argc",value="<optimized out>"},{name="argv",value="<optimized out>"}]}]
(gdb)
-stack-list-frames
^done,stack=[frame={level="0",addr="0x08004b6c",func="f0",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="829",arch="armv6s-m"},frame={level="1",addr="0x08004b8a",func="f3",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="834",arch="armv6s-m"},frame={level="2",addr="0x08004ba4",func="f2",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="842",arch="armv6s-m"},frame={level="3",addr="0x08004bb6",func="f1",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="848",arch="armv6s-m"},frame={level="4",addr="0x08004bc0",func="gate1",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="853",arch="armv6s-m"},frame={level="5",addr="0x08004be4",func="gate2",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="858",arch="armv6s-m"},frame={level="6",addr="0x08004be4",func="gate3",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="863",arch="armv6s-m"},frame={level="7",addr="0x08004c4c",func="gate4",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="868",arch="armv6s-m"},frame={level="8",addr="0x08004c4c",func="main",file="main.c",fullname="C:\\src\\build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug\\troll-test-drive-files\\blackmagic\\src\\main.c",line="54",arch="armv6s-m"}]
(gdb)
-stack-list-variables --all-values
^done,variables=[{name="a",arg="1",value="27"},{name="b",arg="1",value="2"},{name="b@entry",arg="1",value="2"},{name="c",arg="1",value="5"},{name="c@entry",arg="1",value="5"}]
(gdb)

*/

class GdbMiParser
{
private:
	std::string mi_string;
	/* Current position in the string - the offset from which to fetch the next token. */
	size_t mi_pos = 0;
	enum TOKEN_TYPE
	{
		INVALID		= 0,
		EQUALS,
		COMMA,
		NEWLINE,
		LEFT_CURLY_BRACE,
		RIGHT_CURLY_BRACE,
		LEFT_SQUARE_BRACKET,
		RIGHT_SQUARE_BRACKET,
		STRING,
		CSTRING,
	};
	std::string tokenText;
	enum TOKEN_TYPE nextToken(void)
	{
		tokenText.clear();
		if (mi_pos >= mi_string.size())
			return INVALID;
		enum TOKEN_TYPE code = INVALID;
		size_t pos = mi_pos;
		switch (mi_string.at(mi_pos))
		{
			/* Single character tokens. */
			case '[': code = LEFT_SQUARE_BRACKET; if (0)
			case ']': code = RIGHT_SQUARE_BRACKET; if (0)
			case '{': code = LEFT_CURLY_BRACE; if (0)
			case '}': code = RIGHT_CURLY_BRACE; if (0)
			case ',': code = COMMA; if (0)
			case '=': code = EQUALS; if (0)
			case '\n': code = NEWLINE;
				tokenText = mi_string.at(pos);
				break;
			case '"':
				{
					/* Note: this code will return the literal string constant AS IS, without
					 * performing escaped characters processing. */
					std::string s(1, mi_string.at(pos ++));
					char c;
					while (pos < mi_string.size()) switch(s += (c = mi_string.at(pos ++)), c)
					{
						default:
							continue;
						case '\\':
							/* escaped characters */
							if (pos == mi_string.size())
								return INVALID;
							s += mi_string.at(pos ++);
							continue;
						case '"':
							goto out;

					}
out:
					if (c != '"')
						return INVALID;
					tokenText = s;
					code = CSTRING;
				}
				break;
			default:
				{
					std::string s;
					char c;
					while (pos < mi_string.size() && (isalnum(c = mi_string.at(pos)) || c =='_' || c == '-'))
						s += c, pos ++;
					if (!s.size())
						return INVALID;
					tokenText = s;
					code = STRING;
				}
				break;
		}
		return code;
	}
public:
	enum RESULT_CLASS_ENUM
	{
		INVALID_RESULT_CLASS		= 0,
		DONE,
		STOPPED,
		RUNNING,
		CONNECTED,
		ERROR,
		EXIT,
	};
	struct MIList;
	struct MITuple;
	struct MIConstant;
	struct MIValue
	{
		virtual const struct MIList * asList(void) const { return 0; }
		virtual const struct MITuple * asTuple(void) const { return 0; }
		virtual const struct MIConstant * asConstant(void) const { return 0; }
	};
	struct MIResult
	{
		std::string variable;
		std::shared_ptr<MIValue> value;
	};

	struct MIList : MIValue
	{
		std::vector<std::shared_ptr<MIValue>>	values;
		std::vector<MIResult>	results;
		virtual const struct MIList * asList(void) const override { return this; }
	};
	struct MITuple : MIValue
	{
		std::unordered_map<std::string, std::shared_ptr<MIValue>>  map;
		virtual const struct MITuple * asTuple(void) const override { return this; }
	};
	struct MIConstant : MIValue
	{
		std::string constant_string;
		std::string constant() const { return constant_string; }
		virtual const struct MIConstant * asConstant(void) const override { return this; }
	};
private:
	bool parseConstant(MIConstant & constant)
	{
		bool res = false;
		if (nextToken() == CSTRING)
		{
			size_t token_length = tokenText.length();
			mi_pos += token_length;
			/* The string literal text is returned AS IS, no escaped characters processing is done.
			 * Process any escaped characters here. */
			constant.constant_string = std::string();
			/* When processing the string, ignore the enclosing double quotes.
			 * Also, consider the string to be already validated by the string parsing code
			 * in 'nextToken()', so no validation is performed here. */
			size_t i = 1;
			while (i < token_length - 1)
			{
				if (tokenText.at(i) == '\\')
				{
					char c;
					switch (c = tokenText.at(++ i))
					{
						case 't': c = '\t'; break;
					}
					constant.constant_string += c;
				}
				else
					constant.constant_string += tokenText.at(i);
				i ++;
			}
			res = true;
		}
		return res;
	}
	bool parseList(MIList & list)
	{
		/* list: "[]" | "[" value ( "," value )* "]" | "[" result ( "," result )* "]" */
		bool res = false;
		int saved_pos = mi_pos;
		do
		{
			if (nextToken() != LEFT_SQUARE_BRACKET)
				break;
			mi_pos += tokenText.length();
			if (nextToken() == RIGHT_SQUARE_BRACKET)
			{
				/* empty list */
				mi_pos += tokenText.length();
				res = true;
				break;
			}

			std::shared_ptr<MIValue> v;
			enum TOKEN_TYPE c;
			if (parseValue(v))
			{
				/* try to parse a list of values */
				list.values.push_back(v);
				while (1)
				{
					if ((c = nextToken()) == RIGHT_SQUARE_BRACKET)
					{
						mi_pos += tokenText.length();
						res = true;
						break;
					}
					else if (c != COMMA)
						break;
					mi_pos += tokenText.length();
					if (!parseValue(v))
						break;
					list.values.push_back(v);
				}
				break;
			}
			MIResult r;
			if (parseResult(r))
			{
				/* try to parse a list of results */
				list.results.push_back(r);
				while (1)
				{
					if ((c = nextToken()) == RIGHT_SQUARE_BRACKET)
					{
						mi_pos += tokenText.length();
						res = true;
						break;
					}
					else if (c != COMMA)
						break;
					mi_pos += tokenText.length();
					if (!parseResult(r))
						break;
					list.results.push_back(r);
				}
				break;
			}
		}
		while (0);
		if (res == false)
			/* Undo any lookahead performed. */
			mi_pos = saved_pos;
		return res;
	}
	bool parseTuple(MITuple & tuple)
	{
		/* tuple: "{}" | "{" result ( "," result )* "}" */
		bool res = false;
		int saved_pos = mi_pos;
		do
		{
			if (nextToken() != LEFT_CURLY_BRACE)
				break;
			mi_pos += tokenText.length();
			if (nextToken() == RIGHT_CURLY_BRACE)
			{
				/* empty tuple */
				mi_pos += tokenText.length();
				res = true;
				break;
			}

			MIResult r;
			if (parseResult(r))
			{
				tuple.map.operator [](r.variable) = r.value;
				enum TOKEN_TYPE c;
				while (1)
				{
					if ((c = nextToken()) == RIGHT_CURLY_BRACE)
					{
						mi_pos += tokenText.length();
						res = true;
						break;
					}
					else if (c != COMMA)
						break;
					mi_pos += tokenText.length();
					if (!parseResult(r))
						break;
					tuple.map.operator [](r.variable) = r.value;
				}
				break;
			}
		}
		while (0);
		if (res == false)
			/* Undo any lookahead performed. */
			mi_pos = saved_pos;
		return res;
	}
	bool parseValue(std::shared_ptr<MIValue> & value)
	{
		/* value: const | tuple | list */
		MIConstant c;
		if (parseConstant(c))
		{
			value = std::make_shared<MIConstant>(c);
			return true;
		}
		MITuple t;
		if (parseTuple(t))
		{
			value = std::make_shared<MITuple>(t);
			return true;
		}
		MIList l;
		if (parseList(l))
		{
			value = std::make_shared<MIList>(l);
			return true;
		}
		return false;
	}

	bool parseResult(MIResult & result)
	{
		/* result: variable "=" value */
		bool res = false;
		int saved_pos = mi_pos;
		do
		{
			if (nextToken() != STRING)
				break;
			result.variable = tokenText;
			mi_pos += tokenText.length();
			if (nextToken() != EQUALS)
				break;
			mi_pos += tokenText.length();
			res = parseValue(result.value);
		}
		while (0);
		if (res == false)
			/* Undo any lookahead performed. */
			mi_pos = saved_pos;
		return res;
	}
public:
	enum RESULT_CLASS_ENUM parse(const std::string & gdbMiString, std::vector<MIResult> & results)
	{
		/* result-record: [ token ] "^" result-class ( "," result )* nl
		 * result-class: "done" | "running" | "connected" | "error" | "exit" */
		mi_pos = 0;
		mi_string = gdbMiString;
		if (!mi_string.size() || (mi_string.at(mi_pos) != '^' && mi_string.at(mi_pos) != '*'))
			return INVALID_RESULT_CLASS;
		mi_pos ++;
		if (nextToken() != STRING)
			return INVALID_RESULT_CLASS;
		enum RESULT_CLASS_ENUM result_class = INVALID_RESULT_CLASS;
		if (tokenText == "done")
			result_class = DONE;
		else if (tokenText == "running")
			result_class = RUNNING;
		else if (tokenText == "connected")
			result_class = CONNECTED;
		else if (tokenText == "error")
			result_class = ERROR;
		else if (tokenText == "exit")
			result_class = EXIT;
		else if (tokenText == "stopped")
			result_class = STOPPED;
		else
			return INVALID_RESULT_CLASS;
		mi_pos += tokenText.length();
		while (mi_pos != mi_string.length())
		{
			if (nextToken() != COMMA)
			{
error:
				results.clear();
				return INVALID_RESULT_CLASS;
			}
			mi_pos += tokenText.length();
			MIResult result;
			if (!parseResult(result))
				goto error;
			results.push_back(result);
		}
		return result_class;
	}
};

#endif // GDBMIPARSER_HXX
