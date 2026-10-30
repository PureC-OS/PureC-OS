#include "drivers/display/console_vt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLUMNS 20
#define ROWS 6

static struct vt_term term;

static struct {
    char glyph[ROWS][COLUMNS];
    uint16_t attr[ROWS][COLUMNS];
    bool inverse[ROWS][COLUMNS];
    char reply[64];
    bool fast_scroll;
    int mode_calls;
    uint32_t last_mode;
    bool last_enable;
} screen;

static int failures;

#define CHECK(condition) \
    do { \
        if(!(condition)){ \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while(0)

static void mock_draw_cell(void *ctx, uint32_t row, uint32_t column, char ch,
                           uint16_t attr, bool cursor){
    (void)ctx;
    CHECK(row<ROWS && column<COLUMNS);
    screen.glyph[row][column]=ch;
    screen.attr[row][column]=attr;
    screen.inverse[row][column]=cursor;
}

static void mock_fill(void *ctx, uint32_t row, uint32_t column, uint32_t count,
                      uint16_t attr){
    (void)ctx;
    CHECK(row<ROWS && column+count<=COLUMNS);
    for(uint32_t i=0;i<count;i++){
        screen.glyph[row][column+i]=' ';
        screen.attr[row][column+i]=attr;
        screen.inverse[row][column+i]=false;
    }
}

static bool mock_scroll(void *ctx, uint32_t top, uint32_t bottom,
                        uint32_t count, bool up){
    (void)ctx;
    if(!screen.fast_scroll || !up || top!=0 || bottom+1!=ROWS) return false;
    for(uint32_t row=0;row+count<ROWS;row++){
        memcpy(screen.glyph[row],screen.glyph[row+count],COLUMNS);
        memcpy(screen.attr[row],screen.attr[row+count],sizeof(screen.attr[row]));
        memcpy(screen.inverse[row],screen.inverse[row+count],sizeof(screen.inverse[row]));
    }
    for(uint32_t row=ROWS-count;row<ROWS;row++){
        memset(screen.glyph[row],' ',COLUMNS);
        memset(screen.attr[row],0,sizeof(screen.attr[row]));
        memset(screen.inverse[row],0,sizeof(screen.inverse[row]));
    }
    return true;
}

static void mock_reply(void *ctx, const char *text){
    (void)ctx;
    strncat(screen.reply,text,sizeof(screen.reply)-strlen(screen.reply)-1);
}

static void mock_mode(void *ctx, uint32_t mode, bool enable){
    (void)ctx;
    screen.mode_calls++;
    screen.last_mode=mode;
    screen.last_enable=enable;
}

static const struct vt_ops ops={
    .draw_cell=mock_draw_cell,
    .fill=mock_fill,
    .scroll=mock_scroll,
    .reply=mock_reply,
    .mode=mock_mode
};

static void reset(void){
    memset(&screen,0,sizeof(screen));
    memset(screen.glyph,' ',sizeof(screen.glyph));
    vt_init(&term,&ops,NULL);
    vt_resize(&term,COLUMNS,ROWS);
    vt_redraw(&term);
}

static void put(const char *text){
    vt_write(&term,text,(uint32_t)strlen(text),VT_FLAG_NL_CR|VT_FLAG_BS_ERASE);
}

static void put_raw(const char *text){
    vt_write(&term,text,(uint32_t)strlen(text),0);
}

static void verify_display(const char *label){
    unsigned cursors=0;
    for(uint32_t row=0;row<ROWS;row++){
        for(uint32_t column=0;column<COLUMNS;column++){
            if(screen.glyph[row][column]!=term.chars[row][column]){
                fprintf(stderr,"%s: glyph mismatch at %u,%u: shown '%c' model '%c'\n",
                        label,row,column,screen.glyph[row][column],
                        term.chars[row][column]);
                failures++;
            }
            if(screen.attr[row][column]!=term.attrs[row][column]){
                fprintf(stderr,"%s: attr mismatch at %u,%u: shown %04x model %04x\n",
                        label,row,column,screen.attr[row][column],
                        term.attrs[row][column]);
                failures++;
            }
            if(screen.inverse[row][column]){
                cursors++;
                uint32_t expected_column=term.column<COLUMNS ? term.column : COLUMNS-1;
                if(row!=term.row || column!=expected_column){
                    fprintf(stderr,"%s: cursor drawn at %u,%u but model at %u,%u\n",
                            label,row,column,term.row,term.column);
                    failures++;
                }
            }
        }
    }
    unsigned expected=term.cursor_visible ? 1u : 0u;
    if(cursors!=expected){
        fprintf(stderr,"%s: %u cursor cells drawn, expected %u\n",label,cursors,expected);
        failures++;
    }
}

static bool row_is(uint32_t row, const char *text){
    char expected[COLUMNS+1];
    memset(expected,' ',COLUMNS);
    memcpy(expected,text,strlen(text));
    return memcmp(term.chars[row],expected,COLUMNS)==0;
}

static void test_text_and_wrap(void){
    reset();
    put("hello");
    CHECK(row_is(0,"hello"));
    CHECK(term.row==0 && term.column==5);
    verify_display("text");
    put("0123456789ABCDEFGHIJK");
    CHECK(row_is(0,"hello0123456789ABCDE"));
    CHECK(row_is(1,"FGHIJK"));
    verify_display("wrap");
}

static void test_newline_modes(void){
    reset();
    put("ab\ncd");
    CHECK(row_is(0,"ab"));
    CHECK(row_is(1,"cd"));
    reset();
    put_raw("ab\ncd");
    CHECK(row_is(0,"ab"));
    CHECK(row_is(1,"  cd"));
    verify_display("lf-only");
}

static void test_cursor_addressing(void){
    reset();
    put("\033[3;5HX");
    CHECK(term.chars[2][4]=='X');
    put("\033[2A\033[3DY");
    CHECK(term.chars[0][2]=='Y');
    put("\033[100;100HZ");
    CHECK(term.chars[ROWS-1][COLUMNS-1]=='Z');
    put("\033[HQ");
    CHECK(term.chars[0][0]=='Q');
    put("\033[4d\033[7GR");
    CHECK(term.chars[3][6]=='R');
    put("\033[1;1H\033[2B\033[3CS");
    CHECK(term.chars[2][3]=='S');
    verify_display("cursor");
}

static void test_erase(void){
    reset();
    put("abcdef\033[1;3H\033[K");
    CHECK(row_is(0,"ab"));
    put("\033[1;1Hxyz\033[1;5H\033[1K");
    CHECK(row_is(0,"     "));
    put("\033[2;1Hrow2\033[3;1Hrow3\033[2;3H\033[J");
    CHECK(row_is(1,"ro"));
    CHECK(row_is(2,""));
    put("\033[2J");
    for(uint32_t row=0;row<ROWS;row++) CHECK(row_is(row,""));
    verify_display("erase");
}

static void test_sgr(void){
    reset();
    put("\033[31mA\033[0mB");
    CHECK(term.attrs[0][0]==(VT_ATTR_FG_SET|1));
    CHECK(term.attrs[0][1]==0);
    put("\033[1;44;33mC");
    CHECK((term.attrs[0][2]&VT_ATTR_BOLD)!=0);
    CHECK((term.attrs[0][2]&VT_ATTR_FG_MASK)==3);
    CHECK((term.attrs[0][2]&VT_ATTR_BG_SET)!=0);
    CHECK(((term.attrs[0][2]&VT_ATTR_BG_MASK)>>VT_ATTR_BG_SHIFT)==4);
    put("\033[m\033[7mD\033[27mE\033[92mF\033[103mG");
    CHECK((term.attrs[0][3]&VT_ATTR_REVERSE)!=0);
    CHECK((term.attrs[0][4]&VT_ATTR_REVERSE)==0);
    CHECK((term.attrs[0][5]&VT_ATTR_FG_MASK)==10);
    CHECK(((term.attrs[0][6]&VT_ATTR_BG_MASK)>>VT_ATTR_BG_SHIFT)==11);
    put("\033[0m\033[38;5;196mH\033[0m\033[48;2;0;0;255mI\033[39;49mJ");
    CHECK((term.attrs[0][7]&VT_ATTR_FG_MASK)==9);
    CHECK(((term.attrs[0][8]&VT_ATTR_BG_MASK)>>VT_ATTR_BG_SHIFT)==12);
    CHECK((term.attrs[0][9]&(VT_ATTR_FG_SET|VT_ATTR_BG_SET))==0);
    verify_display("sgr");
}

static void test_erase_uses_background(void){
    reset();
    put("\033[44m\033[2J");
    for(uint32_t row=0;row<ROWS;row++){
        CHECK((term.attrs[row][0]&VT_ATTR_BG_SET)!=0);
        CHECK((term.attrs[row][COLUMNS-1]&VT_ATTR_BG_SET)!=0);
    }
    verify_display("bce");
}

static void test_scrolling(bool fast){
    reset();
    screen.fast_scroll=fast;
    put("L0\nL1\nL2\nL3\nL4\nL5");
    CHECK(row_is(0,"L0"));
    CHECK(row_is(5,"L5"));
    put("\nL6");
    CHECK(row_is(0,"L1"));
    CHECK(row_is(4,"L5"));
    CHECK(row_is(5,"L6"));
    verify_display(fast ? "scroll-fast" : "scroll-redraw");
}

static void test_scroll_region(void){
    reset();
    put("A\nB\nC\nD\nE\nF");
    put("\033[2;4r");
    CHECK(term.row==0 && term.column==0);
    put("\033[4;1H\nX");
    CHECK(row_is(0,"A"));
    CHECK(row_is(1,"C"));
    CHECK(row_is(2,"D"));
    CHECK(row_is(3,"X"));
    CHECK(row_is(4,"E"));
    CHECK(row_is(5,"F"));
    put("\033[2;1H\033M");
    CHECK(term.row==1);
    CHECK(row_is(1,""));
    CHECK(row_is(2,"C"));
    CHECK(row_is(3,"D"));
    CHECK(row_is(4,"E"));
    put("\033[1;1H\033M");
    CHECK(term.row==0);
    CHECK(row_is(0,"A"));
    verify_display("region");
    put("\033[r");
    CHECK(term.top==0 && term.bottom==ROWS-1);
}

static void test_insert_delete_lines(void){
    reset();
    put("1\n2\n3\n4\n5\n6");
    put("\033[3;1H\033[L");
    CHECK(row_is(1,"2"));
    CHECK(row_is(2,""));
    CHECK(row_is(3,"3"));
    CHECK(row_is(5,"5"));
    put("\033[3;1H\033[2M");
    CHECK(row_is(2,"4"));
    CHECK(row_is(3,"5"));
    CHECK(row_is(4,""));
    verify_display("il-dl");
}

static void test_insert_delete_chars(void){
    reset();
    put("abcdef\033[1;3H\033[2@");
    CHECK(row_is(0,"ab  cdef"));
    put("\033[1;3H\033[2P");
    CHECK(row_is(0,"abcdef"));
    put("\033[1;2H\033[3X");
    CHECK(row_is(0,"a   ef"));
    verify_display("ich-dch");
}

static void test_reports(void){
    reset();
    put("\033[3;5H\033[6n");
    CHECK(strcmp(screen.reply,"\033[3;5R")==0);
    screen.reply[0]='\0';
    put("\033[5n");
    CHECK(strcmp(screen.reply,"\033[0n")==0);
    screen.reply[0]='\0';
    put("\033[c");
    CHECK(strcmp(screen.reply,"\033[?6c")==0);
    screen.reply[0]='\0';
    put("\033[>c");
    CHECK(screen.reply[0]=='\0');
}

static void test_backspace(void){
    reset();
    put("abc\b");
    CHECK(row_is(0,"ab"));
    CHECK(term.column==2);
    reset();
    put_raw("abc\b");
    CHECK(row_is(0,"abc"));
    CHECK(term.column==2);
    verify_display("bs");
}

static void test_tab(void){
    reset();
    put("a\tb");
    CHECK(term.chars[0][8]=='b');
    put("\t\tc");
    CHECK(term.chars[0][COLUMNS-1]=='c');
}

static void test_cursor_visibility(void){
    reset();
    put("x\033[?25l");
    verify_display("cursor-hidden");
    CHECK(!term.cursor_visible);
    put("y\033[?25h");
    verify_display("cursor-shown");
    CHECK(term.cursor_visible);
}

static void test_split_sequences(void){
    reset();
    put("\033[3");
    put(";5");
    put("HZ");
    CHECK(term.chars[2][4]=='Z');
    put("\033");
    put("[1;1H");
    put("Q");
    CHECK(term.chars[0][0]=='Q');
    verify_display("split");
}

static void test_string_sequences(void){
    reset();
    put("\033]0;window title\007Z");
    CHECK(row_is(0,"Z"));
    put("\033P1$r0m\033\\Y");
    CHECK(row_is(0,"ZY"));
    verify_display("osc");
}

static void test_save_restore(void){
    reset();
    put("\033[2;3H\0337\033[31m\033[5;5H\0338A");
    CHECK(term.chars[1][2]=='A');
    CHECK(term.attrs[1][2]==0);
    put("\033[3;3H\033[s\033[1;1H\033[u" "B");
    CHECK(term.chars[2][2]=='B');
}

static void test_modes(void){
    reset();
    put("\033[?1h");
    CHECK(screen.mode_calls==1 && screen.last_mode==1 && screen.last_enable);
    put("\033[?1l");
    CHECK(screen.mode_calls==2 && !screen.last_enable);
    put("\033[?7l");
    put("\033[1;20HAB");
    CHECK(term.chars[0][COLUMNS-1]=='B');
    CHECK(term.row==0);
}

static void test_unknown_is_harmless(void){
    reset();
    put("\033[?2004h\033[>4;2m\033[?1049h\033=\033>\033(B\033[ qOK");
    CHECK(row_is(0,"OK"));
    verify_display("unknown");
}

static void test_cancel(void){
    reset();
    put("\033[3\030X");
    CHECK(term.chars[0][0]=='X');
}

static void test_resize_keeps_content(void){
    reset();
    put("1\n2\n3\n4\n5\n6");
    vt_resize(&term,COLUMNS,4);
    CHECK(term.rows==4);
    CHECK(term.row==3);
    CHECK(term.chars[0][0]=='3');
    CHECK(term.bottom==3);
}

int main(void){
    test_text_and_wrap();
    test_newline_modes();
    test_cursor_addressing();
    test_erase();
    test_sgr();
    test_erase_uses_background();
    test_scrolling(false);
    test_scrolling(true);
    test_scroll_region();
    test_insert_delete_lines();
    test_insert_delete_chars();
    test_reports();
    test_backspace();
    test_tab();
    test_cursor_visibility();
    test_split_sequences();
    test_string_sequences();
    test_save_restore();
    test_modes();
    test_unknown_is_harmless();
    test_cancel();
    test_resize_keeps_content();
    if(failures){
        fprintf(stderr,"console_vt_test: %d failure(s)\n",failures);
        return 1;
    }
    puts("console_vt_test: all passed");
    return 0;
}
