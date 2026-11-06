#include "console_vt.h"

#define VT_MEMMOVE __builtin_memmove
#define VT_MEMSET __builtin_memset

enum {
    ST_GROUND,
    ST_ESC,
    ST_CSI,
    ST_STRING,
    ST_STRING_ESC,
    ST_SKIP
};

#define STRING_LIMIT 1024

static uint32_t min_u32(uint32_t a, uint32_t b){ return a<b ? a : b; }

static uint16_t erase_attr(const struct vt_term *term){
    return (uint16_t)(term->attr & (VT_ATTR_BG_MASK|VT_ATTR_BG_SET));
}

static void draw_cell(struct vt_term *term, uint32_t row, uint32_t column,
                      bool cursor){
    term->ops->draw_cell(term->ctx,row,column,term->chars[row][column],
                         term->attrs[row][column],cursor);
}

static void blank_cells(struct vt_term *term, uint32_t row, uint32_t column,
                        uint32_t count, uint16_t attr){
    for(uint32_t i=0;i<count;i++){
        term->chars[row][column+i]=' ';
        term->attrs[row][column+i]=attr;
    }
}

static void fill_run(struct vt_term *term, uint32_t row, uint32_t column,
                     uint32_t count, uint16_t attr){
    if(!count) return;
    blank_cells(term,row,column,count,attr);
    term->ops->fill(term->ctx,row,column,count,attr);
}

static bool plain_blank(const struct vt_term *term, uint32_t row,
                        uint32_t column){
    return term->chars[row][column]==' '
        && !(term->attrs[row][column]&(VT_ATTR_BG_SET|VT_ATTR_REVERSE));
}

static void redraw_span(struct vt_term *term, uint32_t row, uint32_t from,
                        uint32_t to, bool sparse){
    uint32_t column=from;
    while(column<to){
        if(term->chars[row][column]!=' '){
            draw_cell(term,row,column,false);
            column++;
            continue;
        }
        uint16_t attr=term->attrs[row][column];
        uint32_t run=1;
        while(column+run<to && term->chars[row][column+run]==' '
              && term->attrs[row][column+run]==attr) run++;
        if(!sparse || !plain_blank(term,row,column))
            term->ops->fill(term->ctx,row,column,run,attr);
        column+=run;
    }
}

static void redraw_rows(struct vt_term *term, uint32_t top, uint32_t bottom,
                        bool sparse){
    for(uint32_t row=top;row<=bottom && row<term->rows;row++)
        redraw_span(term,row,0,term->columns,sparse);
}

static void blank_rows(struct vt_term *term, uint32_t from, uint32_t count,
                       uint16_t attr){
    for(uint32_t row=from;row<from+count;row++)
        blank_cells(term,row,0,VT_MAX_COLUMNS,attr);
}

static void scroll_up(struct vt_term *term, uint32_t top, uint32_t bottom,
                      uint32_t count){
    if(top>bottom || bottom>=term->rows || !count) return;
    uint32_t span=bottom-top+1;
    if(count>span) count=span;
    uint16_t attr=erase_attr(term);
    if(span>count){
        VT_MEMMOVE(term->chars[top],term->chars[top+count],
                   (uint64_t)(span-count)*VT_MAX_COLUMNS);
        VT_MEMMOVE(term->attrs[top],term->attrs[top+count],
                   (uint64_t)(span-count)*VT_MAX_COLUMNS*sizeof(uint16_t));
    }
    blank_rows(term,bottom+1-count,count,attr);
    if(term->ops->scroll
       && term->ops->scroll(term->ctx,top,bottom,count,true)){
        if(attr&VT_ATTR_BG_SET){
            for(uint32_t row=bottom+1-count;row<=bottom;row++)
                term->ops->fill(term->ctx,row,0,term->columns,attr);
        }
        return;
    }
    redraw_rows(term,top,bottom,false);
}

static void scroll_down(struct vt_term *term, uint32_t top, uint32_t bottom,
                        uint32_t count){
    if(top>bottom || bottom>=term->rows || !count) return;
    uint32_t span=bottom-top+1;
    if(count>span) count=span;
    uint16_t attr=erase_attr(term);
    if(span>count){
        VT_MEMMOVE(term->chars[top+count],term->chars[top],
                   (uint64_t)(span-count)*VT_MAX_COLUMNS);
        VT_MEMMOVE(term->attrs[top+count],term->attrs[top],
                   (uint64_t)(span-count)*VT_MAX_COLUMNS*sizeof(uint16_t));
    }
    blank_rows(term,top,count,attr);
    if(term->ops->scroll
       && term->ops->scroll(term->ctx,top,bottom,count,false)){
        if(attr&VT_ATTR_BG_SET){
            for(uint32_t row=top;row<top+count;row++)
                term->ops->fill(term->ctx,row,0,term->columns,attr);
        }
        return;
    }
    redraw_rows(term,top,bottom,false);
}

static void linefeed(struct vt_term *term){
    if(term->row==term->bottom) scroll_up(term,term->top,term->bottom,1);
    else if(term->row+1<term->rows) term->row++;
}

static void reverse_index(struct vt_term *term){
    if(term->row==term->top) scroll_down(term,term->top,term->bottom,1);
    else if(term->row>0) term->row--;
}

static void clamp_column(struct vt_term *term){
    if(term->column>=term->columns) term->column=term->columns-1;
}

static void put_char(struct vt_term *term, char character){
    if(term->column>=term->columns){
        if(term->autowrap){
            term->column=0;
            linefeed(term);
        } else {
            term->column=term->columns-1;
        }
    }
    term->chars[term->row][term->column]=character;
    term->attrs[term->row][term->column]=term->attr;
    draw_cell(term,term->row,term->column,false);
    term->column++;
}

static void erase_range(struct vt_term *term, uint32_t row, uint32_t from,
                        uint32_t to){
    if(to>term->columns) to=term->columns;
    if(from>=to) return;
    fill_run(term,row,from,to-from,erase_attr(term));
}

static void erase_display(struct vt_term *term, uint32_t mode){
    clamp_column(term);
    if(mode==0){
        erase_range(term,term->row,term->column,term->columns);
        for(uint32_t row=term->row+1;row<term->rows;row++)
            erase_range(term,row,0,term->columns);
    } else if(mode==1){
        for(uint32_t row=0;row<term->row;row++)
            erase_range(term,row,0,term->columns);
        erase_range(term,term->row,0,term->column+1);
    } else if(mode==2 || mode==3){
        for(uint32_t row=0;row<term->rows;row++)
            erase_range(term,row,0,term->columns);
    }
}

static void erase_line(struct vt_term *term, uint32_t mode){
    clamp_column(term);
    if(mode==0) erase_range(term,term->row,term->column,term->columns);
    else if(mode==1) erase_range(term,term->row,0,term->column+1);
    else if(mode==2) erase_range(term,term->row,0,term->columns);
}

static void insert_chars(struct vt_term *term, uint32_t count){
    clamp_column(term);
    uint32_t room=term->columns-term->column;
    if(count>room) count=room;
    if(!count) return;
    uint32_t row=term->row;
    uint32_t column=term->column;
    VT_MEMMOVE(&term->chars[row][column+count],&term->chars[row][column],
               room-count);
    VT_MEMMOVE(&term->attrs[row][column+count],&term->attrs[row][column],
               (uint64_t)(room-count)*sizeof(uint16_t));
    blank_cells(term,row,column,count,erase_attr(term));
    redraw_span(term,row,column,term->columns,false);
}

static void delete_chars(struct vt_term *term, uint32_t count){
    clamp_column(term);
    uint32_t room=term->columns-term->column;
    if(count>room) count=room;
    if(!count) return;
    uint32_t row=term->row;
    uint32_t column=term->column;
    VT_MEMMOVE(&term->chars[row][column],&term->chars[row][column+count],
               room-count);
    VT_MEMMOVE(&term->attrs[row][column],&term->attrs[row][column+count],
               (uint64_t)(room-count)*sizeof(uint16_t));
    blank_cells(term,row,term->columns-count,count,erase_attr(term));
    redraw_span(term,row,column,term->columns,false);
}

static uint32_t param(const struct vt_term *term, uint32_t index,
                      uint32_t fallback){
    if(index>=term->param_count) return fallback;
    uint32_t value=term->params[index];
    return value ? value : fallback;
}

static uint16_t approximate_color(uint32_t red, uint32_t green, uint32_t blue){
    uint32_t peak=red>green ? red : green;
    if(blue>peak) peak=blue;
    uint32_t index=(red>127 ? 1u : 0u)|(green>127 ? 2u : 0u)|(blue>127 ? 4u : 0u);
    if(index==0) return peak>64 ? 8 : 0;
    if(index==7) return peak>=224 ? 15 : 7;
    return (uint16_t)(peak>=224 ? index|8u : index);
}

static uint16_t palette_color(uint32_t index){
    if(index<16) return (uint16_t)index;
    if(index<232){
        uint32_t cube=index-16;
        return approximate_color(((cube/36)%6)*51,((cube/6)%6)*51,
                                 (cube%6)*51);
    }
    uint32_t level=8+(index-232)*10;
    return approximate_color(level,level,level);
}

static void set_foreground(struct vt_term *term, uint16_t color){
    term->attr=(uint16_t)((term->attr&~VT_ATTR_FG_MASK)|(color&0xF)
                          |VT_ATTR_FG_SET);
}

static void set_background(struct vt_term *term, uint16_t color){
    term->attr=(uint16_t)((term->attr&~VT_ATTR_BG_MASK)
                          |((color&0xF)<<VT_ATTR_BG_SHIFT)|VT_ATTR_BG_SET);
}

static void select_graphic_rendition(struct vt_term *term){
    if(!term->param_count){
        term->attr=0;
        return;
    }
    for(uint32_t i=0;i<term->param_count;i++){
        uint32_t value=term->params[i];
        if(value==0) term->attr=0;
        else if(value==1) term->attr|=VT_ATTR_BOLD;
        else if(value==22) term->attr&=(uint16_t)~VT_ATTR_BOLD;
        else if(value==4) term->attr|=VT_ATTR_UNDERLINE;
        else if(value==24) term->attr&=(uint16_t)~VT_ATTR_UNDERLINE;
        else if(value==7) term->attr|=VT_ATTR_REVERSE;
        else if(value==27) term->attr&=(uint16_t)~VT_ATTR_REVERSE;
        else if(value>=30 && value<=37) set_foreground(term,(uint16_t)(value-30));
        else if(value==39) term->attr&=(uint16_t)~(VT_ATTR_FG_MASK|VT_ATTR_FG_SET);
        else if(value>=40 && value<=47) set_background(term,(uint16_t)(value-40));
        else if(value==49) term->attr&=(uint16_t)~(VT_ATTR_BG_MASK|VT_ATTR_BG_SET);
        else if(value>=90 && value<=97) set_foreground(term,(uint16_t)(value-90+8));
        else if(value>=100 && value<=107) set_background(term,(uint16_t)(value-100+8));
        else if(value==38 || value==48){
            uint16_t color;
            if(i+2<term->param_count && term->params[i+1]==5){
                color=palette_color(term->params[i+2]);
                i+=2;
            } else if(i+4<term->param_count && term->params[i+1]==2){
                color=approximate_color(term->params[i+2],term->params[i+3],
                                        term->params[i+4]);
                i+=4;
            } else {
                break;
            }
            if(value==38) set_foreground(term,color);
            else set_background(term,color);
        }
    }
}

static void reply_cursor_position(struct vt_term *term){
    if(!term->ops->reply) return;
    char text[24];
    uint32_t length=0;
    text[length++]='\033';
    text[length++]='[';
    uint32_t values[2]={term->row+1,min_u32(term->column,term->columns-1)+1};
    for(uint32_t v=0;v<2;v++){
        char digits[10];
        uint32_t n=0;
        uint32_t number=values[v];
        do { digits[n++]=(char)('0'+number%10); number/=10; } while(number);
        while(n) text[length++]=digits[--n];
        text[length++]=v==0 ? ';' : 'R';
    }
    text[length]='\0';
    term->ops->reply(term->ctx,text);
}

static void reset_modes(struct vt_term *term){
    term->attr=0;
    term->top=0;
    term->bottom=term->rows-1;
    term->autowrap=true;
    term->cursor_visible=true;
}

static void full_reset(struct vt_term *term){
    reset_modes(term);
    term->row=0;
    term->column=0;
    for(uint32_t row=0;row<term->rows;row++)
        fill_run(term,row,0,term->columns,0);
}

static void set_private_mode(struct vt_term *term, uint32_t mode, bool enable){
    if(mode==25) term->cursor_visible=enable;
    else if(mode==7) term->autowrap=enable;
    if(term->ops->mode) term->ops->mode(term->ctx,mode,enable);
}

static void csi_dispatch(struct vt_term *term, uint8_t final){
    if(term->intermediate) return;
    if(term->prefix=='?'){
        if(final=='h' || final=='l'){
            for(uint32_t i=0;i<term->param_count;i++)
                set_private_mode(term,term->params[i],final=='h');
        }
        return;
    }
    if(term->prefix) return;
    uint32_t n;
    switch(final){
    case 'A':
        clamp_column(term);
        n=param(term,0,1);
        {
            uint32_t limit=term->row>=term->top ? term->top : 0;
            term->row=term->row-limit>n ? term->row-n : limit;
        }
        break;
    case 'B':
    case 'e':
        clamp_column(term);
        n=param(term,0,1);
        {
            uint32_t limit=term->row<=term->bottom ? term->bottom : term->rows-1;
            term->row=limit-term->row>n ? term->row+n : limit;
        }
        break;
    case 'C':
    case 'a':
        clamp_column(term);
        n=param(term,0,1);
        term->column=min_u32(term->column+n,term->columns-1);
        break;
    case 'D':
        clamp_column(term);
        n=param(term,0,1);
        term->column=term->column>n ? term->column-n : 0;
        break;
    case 'E':
        n=param(term,0,1);
        term->column=0;
        term->row=min_u32(term->row+n,term->rows-1);
        break;
    case 'F':
        n=param(term,0,1);
        term->column=0;
        term->row=term->row>n ? term->row-n : 0;
        break;
    case 'G':
    case '`':
        term->column=min_u32(param(term,0,1)-1,term->columns-1);
        break;
    case 'H':
    case 'f':
        term->row=min_u32(param(term,0,1)-1,term->rows-1);
        term->column=min_u32(param(term,1,1)-1,term->columns-1);
        break;
    case 'd':
        term->row=min_u32(param(term,0,1)-1,term->rows-1);
        break;
    case 'J':
        erase_display(term,param(term,0,0));
        break;
    case 'K':
        erase_line(term,param(term,0,0));
        break;
    case 'L':
        if(term->row>=term->top && term->row<=term->bottom){
            scroll_down(term,term->row,term->bottom,param(term,0,1));
            term->column=0;
        }
        break;
    case 'M':
        if(term->row>=term->top && term->row<=term->bottom){
            scroll_up(term,term->row,term->bottom,param(term,0,1));
            term->column=0;
        }
        break;
    case '@':
        insert_chars(term,param(term,0,1));
        break;
    case 'P':
        delete_chars(term,param(term,0,1));
        break;
    case 'X':
        clamp_column(term);
        erase_range(term,term->row,term->column,term->column+param(term,0,1));
        break;
    case 'S':
        scroll_up(term,term->top,term->bottom,param(term,0,1));
        break;
    case 'T':
        scroll_down(term,term->top,term->bottom,param(term,0,1));
        break;
    case 'm':
        select_graphic_rendition(term);
        break;
    case 'r': {
        uint32_t top=param(term,0,1)-1;
        uint32_t bottom=min_u32(param(term,1,term->rows)-1,term->rows-1);
        if(top<bottom){
            term->top=top;
            term->bottom=bottom;
            term->row=0;
            term->column=0;
        }
        break;
    }
    case 's':
        term->saved_row=term->row;
        term->saved_column=term->column;
        term->saved_attr=term->attr;
        break;
    case 'u':
        term->row=min_u32(term->saved_row,term->rows-1);
        term->column=min_u32(term->saved_column,term->columns-1);
        term->attr=term->saved_attr;
        break;
    case 'n':
        if(param(term,0,0)==5){
            if(term->ops->reply) term->ops->reply(term->ctx,"\033[0n");
        } else if(param(term,0,0)==6){
            reply_cursor_position(term);
        }
        break;
    case 'c':
        if(param(term,0,0)==0 && term->ops->reply)
            term->ops->reply(term->ctx,"\033[?6c");
        break;
    case 'h':
    case 'l':
    default:
        break;
    }
}

static void control(struct vt_term *term, uint8_t byte, uint32_t flags){
    switch(byte){
    case '\b':
        if(flags&VT_FLAG_BS_ERASE){
            if(term->column){
                term->column--;
                if(term->column>=term->columns) term->column=term->columns-1;
                term->chars[term->row][term->column]=' ';
                term->attrs[term->row][term->column]=0;
                draw_cell(term,term->row,term->column,false);
            }
        } else {
            clamp_column(term);
            if(term->column) term->column--;
        }
        break;
    case '\t':
        clamp_column(term);
        term->column=min_u32((term->column/8+1)*8,term->columns-1);
        break;
    case '\n':
    case '\v':
    case '\f':
        if(flags&VT_FLAG_NL_CR) term->column=0;
        else clamp_column(term);
        linefeed(term);
        break;
    case '\r':
        term->column=0;
        break;
    default:
        break;
    }
}

static void begin_csi(struct vt_term *term){
    term->state=ST_CSI;
    term->param_count=0;
    term->have_param=false;
    term->prefix=0;
    term->intermediate=0;
    VT_MEMSET(term->params,0,sizeof(term->params));
}

static void escape_dispatch(struct vt_term *term, uint8_t byte){
    term->state=ST_GROUND;
    switch(byte){
    case '[':
        begin_csi(term);
        break;
    case ']':
    case 'P':
    case 'X':
    case '^':
    case '_':
        term->state=ST_STRING;
        term->string_length=0;
        break;
    case '(':
    case ')':
    case '*':
    case '+':
    case '#':
    case '%':
        term->state=ST_SKIP;
        break;
    case '7':
        term->saved_row=term->row;
        term->saved_column=term->column;
        term->saved_attr=term->attr;
        break;
    case '8':
        term->row=min_u32(term->saved_row,term->rows-1);
        term->column=min_u32(term->saved_column,term->columns-1);
        term->attr=term->saved_attr;
        break;
    case 'M':
        reverse_index(term);
        break;
    case 'D':
        linefeed(term);
        break;
    case 'E':
        term->column=0;
        linefeed(term);
        break;
    case 'c':
        full_reset(term);
        break;
    default:
        break;
    }
}

static void csi_byte(struct vt_term *term, uint8_t byte, uint32_t flags){
    if(byte==0x1B){ term->state=ST_ESC; return; }
    if(byte==0x18 || byte==0x1A){ term->state=ST_GROUND; return; }
    if(byte<0x20){ control(term,byte,flags); return; }
    if(byte>='0' && byte<='9'){
        if(!term->have_param){
            term->have_param=true;
            if(term->param_count<VT_MAX_PARAMS) term->param_count++;
        }
        if(term->param_count){
            uint32_t value=term->params[term->param_count-1]*10u+(byte-'0');
            term->params[term->param_count-1]=(uint16_t)(value>65535u ? 65535u : value);
        }
        return;
    }
    if(byte==';' || byte==':'){
        if(!term->have_param && term->param_count<VT_MAX_PARAMS)
            term->param_count++;
        term->have_param=false;
        return;
    }
    if(byte>=0x3C && byte<=0x3F){
        if(!term->param_count && !term->have_param) term->prefix=byte;
        return;
    }
    if(byte>=0x20 && byte<=0x2F){
        term->intermediate=byte;
        return;
    }
    if(byte>=0x40 && byte<=0x7E){
        term->state=ST_GROUND;
        csi_dispatch(term,byte);
    }
}

static void feed(struct vt_term *term, uint8_t byte, uint32_t flags){
    switch(term->state){
    case ST_ESC:
        escape_dispatch(term,byte);
        return;
    case ST_CSI:
        csi_byte(term,byte,flags);
        return;
    case ST_STRING:
        if(byte==0x07 || byte==0x18 || byte==0x1A
           || ++term->string_length>STRING_LIMIT) term->state=ST_GROUND;
        else if(byte==0x1B) term->state=ST_STRING_ESC;
        return;
    case ST_STRING_ESC:
        term->state=ST_GROUND;
        if(byte!='\\') escape_dispatch(term,byte);
        return;
    case ST_SKIP:
        term->state=ST_GROUND;
        return;
    default:
        break;
    }
    if(byte==0x1B){ term->state=ST_ESC; return; }
    if(byte<0x20){ control(term,byte,flags); return; }
    if(byte==0x7F) return;
    put_char(term,(char)byte);
}

void vt_cursor_hide(struct vt_term *term){
    if(!term->cursor_drawn) return;
    term->cursor_drawn=false;
    if(term->cursor_row<term->rows && term->cursor_column<term->columns)
        draw_cell(term,term->cursor_row,term->cursor_column,false);
}

void vt_cursor_show(struct vt_term *term){
    if(!term->cursor_visible || term->row>=term->rows) return;
    uint32_t column=min_u32(term->column,term->columns-1);
    draw_cell(term,term->row,column,true);
    term->cursor_drawn=true;
    term->cursor_row=term->row;
    term->cursor_column=column;
}

void vt_write(struct vt_term *term, const char *data, uint32_t count,
              uint32_t flags){
    if(!term->columns || !term->rows) return;
    vt_cursor_hide(term);
    for(uint32_t i=0;i<count;i++) feed(term,(uint8_t)data[i],flags);
    vt_cursor_show(term);
}

void vt_clear(struct vt_term *term){
    for(uint32_t row=0;row<VT_MAX_ROWS;row++)
        blank_cells(term,row,0,VT_MAX_COLUMNS,0);
    term->row=0;
    term->column=0;
    term->cursor_drawn=false;
}

void vt_init(struct vt_term *term, const struct vt_ops *ops, void *ctx){
    term->ops=ops;
    term->ctx=ctx;
    term->state=ST_GROUND;
    term->param_count=0;
    term->have_param=false;
    term->prefix=0;
    term->intermediate=0;
    term->attr=0;
    term->saved_row=0;
    term->saved_column=0;
    term->saved_attr=0;
    term->autowrap=true;
    term->cursor_visible=true;
    term->cursor_drawn=false;
    term->columns=0;
    term->rows=0;
    term->top=0;
    term->bottom=0;
    vt_clear(term);
}

void vt_resize(struct vt_term *term, uint32_t columns, uint32_t rows){
    if(columns>VT_MAX_COLUMNS) columns=VT_MAX_COLUMNS;
    if(rows>VT_MAX_ROWS) rows=VT_MAX_ROWS;
    if(!columns || !rows) return;
    bool full_region=term->top==0 && term->bottom+1>=term->rows;
    term->columns=columns;
    term->rows=rows;
    if(full_region || term->bottom>=rows){
        term->top=0;
        term->bottom=rows-1;
    }
    if(term->row>=rows){
        uint32_t shift=term->row-(rows-1);
        VT_MEMMOVE(term->chars[0],term->chars[shift],
                   (uint64_t)(VT_MAX_ROWS-shift)*VT_MAX_COLUMNS);
        VT_MEMMOVE(term->attrs[0],term->attrs[shift],
                   (uint64_t)(VT_MAX_ROWS-shift)*VT_MAX_COLUMNS*sizeof(uint16_t));
        blank_rows(term,VT_MAX_ROWS-shift,shift,0);
        term->row=rows-1;
    }
    if(term->column>=columns) term->column=columns-1;
}

void vt_redraw(struct vt_term *term){
    if(!term->columns || !term->rows) return;
    term->cursor_drawn=false;
    redraw_rows(term,0,term->rows-1,true);
    vt_cursor_show(term);
}
