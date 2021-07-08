%{
    const char *_args[1024];
    unsigned    _argcount = 0;
    int         _bufpos = 0;
    int         _ypos = 0;
    char        _linebuf[4096];
%}

WORD    [a-zA-Z_0-9\/\.-]+
SPECIAL [()><|&;*]+
QUOTE   (\'[^\']*\')|(\"[^\"]*\")

%%
	_argcount = 0; _bufpos = 0; _args[0] = NULL;

{WORD}|{SPECIAL}|{QUOTE}	{
    if (_argcount < (unsigned)(sizeof (_args) / sizeof (_args[0])) - 1) {
        _args[_argcount++] = _linebuf + _bufpos;
        _args[_argcount] = NULL;
        for (_ypos = 0; yytext[_ypos] != '\0'; _ypos++) {
            _linebuf[_bufpos++] = yytext[_ypos];
        }
        _linebuf[_bufpos++] = '\0';
    }
}

\n	return (long)_args;

[ \t]+

.

%%

const char * const *get_args() { yylex(); return (_args);}
