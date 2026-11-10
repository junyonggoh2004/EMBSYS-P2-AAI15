#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "application/scheduler.h"
extern bool repl_line_feed(int ch,char *line,size_t cap,size_t *len);
static void check(const char *input,const char *expected) {
    char line[128];size_t n=0;bool complete=false;
    for(;*input;input++) if(repl_line_feed((unsigned char)*input,line,sizeof line,&n)) complete=true;
    assert(complete && !strcmp(line,expected));
}
int main(void) {
    check("STOX\bP\r\n","STOP");
    check("STOX\177P\r\n","STOP");
    check("  ML RESEX\bT  \r\n","ML RESET");
    check("\b\bSTOP\r\n","STOP");
    check("ST\001OP\n","STOP");
    puts("Actual REPL input editing tests PASS");
}
