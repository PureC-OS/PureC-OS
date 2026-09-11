#include "wallpaper.h"
#include "syscall.h"
#include "../drivers/display/gop.h"
#include "../drivers/interrupts/timer.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

#define WP_PATH_CAP 128
#define WP_MAX_FILE_BYTES (4u*1024u*1024u)
#define WP_MAX_DIM 1920u
#define WP_RETRY_MS 5000u

struct wp_buf {
    void *ptr;
    uint64_t phys;
    uint64_t pages;
};

static char g_path[WP_PATH_CAP];
static struct wp_buf g_cache;
static uint32_t g_cache_w;
static uint32_t g_cache_h;
static bool g_failed;
static uint64_t g_last_attempt_ms;

static void wp_free(struct wp_buf *b){
    if(b && b->ptr && b->pages){
        pmm_free_contiguous(b->phys,b->pages);
        b->ptr=0;
        b->phys=0;
        b->pages=0;
    }
}

static bool wp_alloc(struct wp_buf *b, uint64_t bytes){
    if(b) { b->ptr=0; b->phys=0; b->pages=0; }
    if(!b || !bytes) return false;
    uint64_t pages=(bytes+4095ULL)/4096ULL;
    if(!pages || pages>8192ULL) return false; /* 32MB cap */
    uint64_t phys=pmm_allocate_contiguous(pages);
    if(!phys) return false;
    b->ptr=pmm_physical_to_virtual(phys);
    if(!b->ptr) return false;
    b->phys=phys;
    b->pages=pages;
    return true;
}

void wallpaper_set_path(const char *path){
    if(!path) path="";
    uint32_t i=0;
    while(path[i] && i+1<sizeof(g_path)){ g_path[i]=path[i]; i++; }
    g_path[i]='\0';
    /* Invalidate cache; actual (re)load happens lazily in wallpaper_draw
     * when the framebuffer size is known. */
    wp_free(&g_cache);
    g_cache_w=0;
    g_cache_h=0;
    g_failed=false;
    g_last_attempt_ms=0;
}

bool wallpaper_active(void){ return g_path[0]!='\0'; }

/* ---------- file loading ---------- */

static bool wp_load_file(const char *path, struct wp_buf *out,
                         uint32_t *out_size){
    if(out){ out->ptr=0; out->phys=0; out->pages=0; }
    if(out_size) *out_size=0;
    if(!path || !path[0] || !out || !out_size) return false;
    if(!wp_alloc(out,WP_MAX_FILE_BYTES)) return false;
    int64_t fd=userspace_syscall(SYS_OPEN,(uint64_t)path,0,0);
    if(fd<0){ wp_free(out); return false; }
    uint8_t *dst=(uint8_t*)out->ptr;
    uint32_t total=0;
    for(;;){
        if(total>=WP_MAX_FILE_BYTES) break;
        int64_t n=userspace_syscall(SYS_READ,(uint64_t)fd,
            (uint64_t)(dst+total),
            (uint64_t)(WP_MAX_FILE_BYTES-total));
        if(n<=0) break;
        total+=(uint32_t)n;
    }
    (void)userspace_syscall(SYS_CLOSE,(uint64_t)fd,0,0);
    if(!total){ wp_free(out); return false; }
    *out_size=total;
    return true;
}

/* ---------- little-endian helpers ---------- */

static uint16_t wp_u16le(const uint8_t *p){
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

static uint32_t wp_u32le(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)
        |((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static int32_t wp_i32le(const uint8_t *p){ return (int32_t)wp_u32le(p); }

static uint32_t wp_u32be(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)
        |((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

/* ---------- BMP ---------- */

static bool wp_decode_bmp(const uint8_t *data, uint32_t size,
                          uint32_t *dst, uint32_t dst_w, uint32_t dst_h){
    if(size<54 || !data || !dst || !dst_w || !dst_h) return false;
    if(wp_u16le(data)!=0x4D42) return false;
    uint32_t data_offset=wp_u32le(data+10);
    int32_t width=wp_i32le(data+18);
    int32_t height=wp_i32le(data+22);
    uint16_t bpp=wp_u16le(data+28);
    uint32_t compression=wp_u32le(data+30);
    if(width<=0 || (uint32_t)width>WP_MAX_DIM) return false;
    bool top_down=false;
    if(height<0){ top_down=true; height=-height; }
    if(height<=0 || (uint32_t)height>WP_MAX_DIM) return false;
    uint32_t src_w=(uint32_t)width;
    uint32_t src_h=(uint32_t)height;
    if(bpp!=24 && bpp!=32 && bpp!=8) return false;
    if(compression!=0 && compression!=3) return false;
    if(data_offset>=size) return false;
    uint32_t row_stride=0;
    if(bpp==24) row_stride=(src_w*3u+3u)&~3u;
    else if(bpp==32) row_stride=src_w*4u;
    else row_stride=(src_w+3u)&~3u;
    const uint8_t *palette=data+54;
    if(bpp==8){
        if(data_offset<54 || data_offset>size) return false;
        if(data_offset-54<4) return false;
    }
    for(uint32_t dy=0;dy<dst_h;dy++){
        uint32_t src_y=(dy*src_h)/dst_h;
        uint32_t file_row=top_down ? src_y : (src_h-1u-src_y);
        uint64_t row_off=(uint64_t)data_offset+(uint64_t)file_row*row_stride;
        uint32_t px_bytes=(bpp==24) ? src_w*3u
            : (bpp==32) ? src_w*4u : src_w;
        if(row_off+px_bytes>size) return false;
        const uint8_t *row=data+row_off;
        for(uint32_t dx=0;dx<dst_w;dx++){
            uint32_t src_x=(dx*src_w)/dst_w;
            uint32_t color=0;
            if(bpp==24){
                uint8_t b=row[src_x*3u+0];
                uint8_t g=row[src_x*3u+1];
                uint8_t r=row[src_x*3u+2];
                color=((uint32_t)r<<16)|((uint32_t)g<<8)|b;
            } else if(bpp==32){
                uint8_t b=row[src_x*4u+0];
                uint8_t g=row[src_x*4u+1];
                uint8_t r=row[src_x*4u+2];
                color=((uint32_t)r<<16)|((uint32_t)g<<8)|b;
            } else {
                uint8_t idx=row[src_x];
                uint64_t pal_off=(uint64_t)idx*4u+2u;
                if(54u+pal_off>=data_offset) return false;
                uint8_t b=palette[idx*4u+0];
                uint8_t g=palette[idx*4u+1];
                uint8_t r=palette[idx*4u+2];
                color=((uint32_t)r<<16)|((uint32_t)g<<8)|b;
            }
            dst[(uint64_t)dy*dst_w+dx]=color;
        }
    }
    return true;
}

/* ---------- PPM P6 ---------- */

static bool wp_decode_ppm(const uint8_t *data, uint32_t size,
                          uint32_t *dst, uint32_t dst_w, uint32_t dst_h){
    if(size<15 || !data || !dst || data[0]!='P' || data[1]!='6')
        return false;
    uint32_t idx=2;
    while(idx<size && (data[idx]==' '||data[idx]=='\n'||data[idx]=='\r'||data[idx]=='\t')) idx++;
    if(idx<size && data[idx]=='#'){
        while(idx<size && data[idx]!='\n') idx++;
        idx++;
    }
    uint32_t w=0;
    while(idx<size && data[idx]>='0' && data[idx]<='9'){
        w=w*10u+(uint32_t)(data[idx]-'0');
        idx++;
    }
    while(idx<size && (data[idx]==' '||data[idx]=='\n'||data[idx]=='\r'||data[idx]=='\t')) idx++;
    uint32_t h=0;
    while(idx<size && data[idx]>='0' && data[idx]<='9'){
        h=h*10u+(uint32_t)(data[idx]-'0');
        idx++;
    }
    while(idx<size && (data[idx]==' '||data[idx]=='\n'||data[idx]=='\r'||data[idx]=='\t')) idx++;
    while(idx<size && data[idx]>='0' && data[idx]<='9') idx++;
    if(idx<size && (data[idx]==' '||data[idx]=='\n'||data[idx]=='\r'||data[idx]=='\t')) idx++;
    if(!w || w>WP_MAX_DIM || !h || h>WP_MAX_DIM) return false;
    for(uint32_t dy=0;dy<dst_h;dy++){
        uint32_t src_y=(dy*h)/dst_h;
        for(uint32_t dx=0;dx<dst_w;dx++){
            uint32_t src_x=(dx*w)/dst_w;
            uint64_t off=(uint64_t)idx+((uint64_t)src_y*w+src_x)*3u;
            if(off+3>size) return false;
            uint8_t r=data[off], g=data[off+1], b=data[off+2];
            dst[(uint64_t)dy*dst_w+dx]=((uint32_t)r<<16)|((uint32_t)g<<8)|b;
        }
    }
    return true;
}

/* ---------- PNG (8-bit, types 0/2/6, non-interlaced) ---------- */

struct wp_bit_reader {
    const uint8_t *data;
    uint32_t size;
    uint32_t byte_pos;
    uint32_t bit_buf;
    uint32_t bit_count;
};

static void wp_br_init(struct wp_bit_reader *br, const uint8_t *data,
                       uint32_t size){
    br->data=data;
    br->size=size;
    br->byte_pos=0;
    br->bit_buf=0;
    br->bit_count=0;
}

static bool wp_br_fill(struct wp_bit_reader *br, uint32_t need){
    while(br->bit_count<need){
        if(br->byte_pos>=br->size) return false;
        br->bit_buf|=(uint32_t)br->data[br->byte_pos++]<<br->bit_count;
        br->bit_count+=8;
    }
    return true;
}

static bool wp_br_bits(struct wp_bit_reader *br, uint32_t count,
                       uint32_t *out){
    if(!count){ *out=0; return true; }
    if(!wp_br_fill(br,count)) return false;
    *out=br->bit_buf&(count>=32 ? 0xFFFFFFFFu : ((1u<<count)-1u));
    br->bit_buf>>=count;
    br->bit_count-=count;
    return true;
}

static void wp_br_align(struct wp_bit_reader *br){
    br->bit_buf=0;
    br->bit_count=0;
}

#define WP_HUFF_NODES 1152

struct wp_huff {
    int16_t left[WP_HUFF_NODES];
    int16_t right[WP_HUFF_NODES];
    int16_t symbol[WP_HUFF_NODES];
    int16_t root;
    int node_count;
};

static void wp_huff_init(struct wp_huff *h){
    for(int i=0;i<WP_HUFF_NODES;i++){
        h->left[i]=-1;
        h->right[i]=-1;
        h->symbol[i]=-1;
    }
    h->root=0;
    h->node_count=1;
}

static bool wp_huff_build(struct wp_huff *h, const uint8_t *lengths,
                          uint32_t count){
    wp_huff_init(h);
    uint16_t bl_count[16]={0};
    for(uint32_t i=0;i<count;i++){
        if(lengths[i]>15) return false;
        if(lengths[i]) bl_count[lengths[i]]++;
    }
    uint16_t next_code[16]={0};
    uint16_t code=0;
    for(int bits=1;bits<16;bits++){
        code=(uint16_t)((code+bl_count[bits-1])<<1);
        next_code[bits]=code;
    }
    for(uint32_t n=0;n<count;n++){
        uint8_t len=lengths[n];
        if(!len) continue;
        uint16_t c=next_code[len]++;
        int node=h->root;
        for(int b=len-1;b>=0;b--){
            int bit=(c>>b)&1;
            int16_t *edge=bit ? &h->right[node] : &h->left[node];
            if(b==0){
                if(*edge!=-1) return false;
                if(h->node_count>=WP_HUFF_NODES) return false;
                *edge=(int16_t)h->node_count;
                h->symbol[h->node_count]=(int16_t)n;
                h->node_count++;
            } else {
                if(*edge==-1){
                    if(h->node_count>=WP_HUFF_NODES) return false;
                    *edge=(int16_t)h->node_count;
                    h->node_count++;
                }
                node=*edge;
            }
        }
    }
    return true;
}

static bool wp_huff_decode(struct wp_bit_reader *br, struct wp_huff *h,
                           uint32_t *out){
    int node=h->root;
    for(;;){
        uint32_t bit;
        if(!wp_br_bits(br,1,&bit)) return false;
        node=bit ? h->right[node] : h->left[node];
        if(node<0 || node>=h->node_count) return false;
        if(h->symbol[node]>=0){ *out=(uint32_t)h->symbol[node]; return true; }
    }
}

static const uint16_t wp_len_base[29]={
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t wp_len_extra[29]={
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t wp_dist_base[30]={
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t wp_dist_extra[30]={
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static struct wp_huff g_wp_lit;
static struct wp_huff g_wp_dist;
static struct wp_huff g_wp_cl;
static uint8_t g_wp_dyn[288+32];
static uint8_t g_wp_fixed_lit[288];
static uint8_t g_wp_fixed_dist[32];
static bool g_wp_fixed_ready;

static bool wp_inflate(const uint8_t *in, uint32_t in_size,
                       uint8_t *out, uint32_t out_size,
                       uint32_t *out_written){
    *out_written=0;
    if(in_size<6) return false;
    if((in[0]&0x0F)!=8) return false;
    if((((uint32_t)in[0]<<8)|in[1])%31!=0) return false;
    if(in[1]&0x20) return false;
    struct wp_bit_reader br;
    wp_br_init(&br,in+2,in_size-2-4);
    uint32_t out_pos=0;
    bool final=false;
    if(!g_wp_fixed_ready){
        for(int i=0;i<=143;i++) g_wp_fixed_lit[i]=8;
        for(int i=144;i<=255;i++) g_wp_fixed_lit[i]=9;
        for(int i=256;i<=279;i++) g_wp_fixed_lit[i]=7;
        for(int i=280;i<=287;i++) g_wp_fixed_lit[i]=8;
        for(int i=0;i<32;i++) g_wp_fixed_dist[i]=5;
        g_wp_fixed_ready=true;
    }
    while(!final){
        uint32_t bfinal,btype;
        if(!wp_br_bits(&br,1,&bfinal)) return false;
        if(!wp_br_bits(&br,2,&btype)) return false;
        final=bfinal!=0;
        struct wp_huff *lit=&g_wp_lit;
        struct wp_huff *dist=&g_wp_dist;
        if(btype==0){
            wp_br_align(&br);
            uint32_t consumed=br.byte_pos;
            const uint8_t *raw=in+2+consumed;
            uint32_t raw_left=(in_size-2-4)-consumed;
            if(raw_left<4) return false;
            uint32_t len=raw[0]|((uint32_t)raw[1]<<8);
            uint32_t nlen=raw[2]|((uint32_t)raw[3]<<8);
            if((len^nlen)!=0xFFFF) return false;
            if(raw_left-4<len) return false;
            if(out_pos+len>out_size) return false;
            for(uint32_t i=0;i<len;i++) out[out_pos++]=raw[4+i];
            br.byte_pos+=4+len;
            continue;
        } else if(btype==1){
            if(!wp_huff_build(lit,g_wp_fixed_lit,288)) return false;
            if(!wp_huff_build(dist,g_wp_fixed_dist,32)) return false;
        } else if(btype==2){
            uint32_t hlit,hdist,hclen;
            if(!wp_br_bits(&br,5,&hlit)) return false;
            if(!wp_br_bits(&br,5,&hdist)) return false;
            if(!wp_br_bits(&br,4,&hclen)) return false;
            hlit+=257; hdist+=1; hclen+=4;
            if(hlit>288 || hdist>32) return false;
            static const uint8_t cl_order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t cl_len[19]={0};
            for(uint32_t i=0;i<hclen;i++){
                uint32_t v;
                if(!wp_br_bits(&br,3,&v)) return false;
                cl_len[cl_order[i]]=(uint8_t)v;
            }
            if(!wp_huff_build(&g_wp_cl,cl_len,19)) return false;
            uint32_t total=hlit+hdist;
            if(total>288+32) return false;
            for(uint32_t i=0;i<total;){
                uint32_t sym;
                if(!wp_huff_decode(&br,&g_wp_cl,&sym)) return false;
                if(sym<=15){
                    g_wp_dyn[i++]=(uint8_t)sym;
                } else if(sym==16){
                    uint32_t rep;
                    if(!i || !wp_br_bits(&br,2,&rep)) return false;
                    rep+=3;
                    if(i+rep>total) return false;
                    uint8_t prev=g_wp_dyn[i-1];
                    for(uint32_t k=0;k<rep;k++) g_wp_dyn[i++]=prev;
                } else if(sym==17){
                    uint32_t rep;
                    if(!wp_br_bits(&br,3,&rep)) return false;
                    rep+=3;
                    if(i+rep>total) return false;
                    for(uint32_t k=0;k<rep;k++) g_wp_dyn[i++]=0;
                } else if(sym==18){
                    uint32_t rep;
                    if(!wp_br_bits(&br,7,&rep)) return false;
                    rep+=11;
                    if(i+rep>total) return false;
                    for(uint32_t k=0;k<rep;k++) g_wp_dyn[i++]=0;
                } else {
                    return false;
                }
            }
            if(!wp_huff_build(lit,g_wp_dyn,hlit)) return false;
            if(!wp_huff_build(dist,g_wp_dyn+hlit,hdist)) return false;
        } else {
            return false;
        }
        for(;;){
            uint32_t sym;
            if(!wp_huff_decode(&br,lit,&sym)) return false;
            if(sym<256){
                if(out_pos>=out_size) return false;
                out[out_pos++]=(uint8_t)sym;
            } else if(sym==256){
                break;
            } else if(sym<=285){
                uint32_t li=sym-257;
                uint32_t len=wp_len_base[li];
                uint32_t eb;
                if(!wp_br_bits(&br,wp_len_extra[li],&eb)) return false;
                len+=eb;
                uint32_t dsym;
                if(!wp_huff_decode(&br,dist,&dsym)) return false;
                if(dsym>29) return false;
                uint32_t dist_val=wp_dist_base[dsym];
                if(!wp_br_bits(&br,wp_dist_extra[dsym],&eb)) return false;
                dist_val+=eb;
                if(!dist_val || dist_val>out_pos) return false;
                if(out_pos+len>out_size) return false;
                for(uint32_t k=0;k<len;k++){
                    out[out_pos]=out[out_pos-dist_val];
                    out_pos++;
                }
            } else {
                return false;
            }
        }
    }
    {
        uint32_t s1=1,s2=0;
        for(uint32_t i=0;i<out_pos;i++){
            s1=(s1+out[i])%65521u;
            s2=(s2+s1)%65521u;
        }
        uint32_t expect=wp_u32be(in+in_size-4);
        if(((s2<<16)|s1)!=expect) return false;
    }
    *out_written=out_pos;
    return true;
}

static uint8_t wp_paeth(uint8_t a, uint8_t b, uint8_t c){
    int p=(int)a+(int)b-(int)c;
    int pa=p-(int)a; if(pa<0) pa=-pa;
    int pb=p-(int)b; if(pb<0) pb=-pb;
    int pc=p-(int)c; if(pc<0) pc=-pc;
    if(pa<=pb && pa<=pc) return a;
    if(pb<=pc) return b;
    return c;
}

static bool wp_decode_png(const uint8_t *data, uint32_t size,
                          uint32_t *dst, uint32_t dst_w, uint32_t dst_h){
    static const uint8_t sig[8]={137,80,78,71,13,10,26,10};
    if(size<57 || !data || !dst) return false;
    for(int i=0;i<8;i++) if(data[i]!=sig[i]) return false;
    uint32_t pos=8;
    uint32_t width=0,height=0;
    uint8_t bit_depth=0,color_type=0,interlace=0;
    bool have_ihdr=false;
    uint32_t idat_total=0;
    while(pos+8<=size){
        uint32_t len=wp_u32be(data+pos);
        const uint8_t *type=data+pos+4;
        if(pos+8+len+4<pos || pos+8+len+4>size) return false;
        const uint8_t *chunk=data+pos+8;
        if(type[0]=='I'&&type[1]=='H'&&type[2]=='D'&&type[3]=='R'){
            if(len!=13 || have_ihdr) return false;
            width=wp_u32be(chunk);
            height=wp_u32be(chunk+4);
            bit_depth=chunk[8];
            color_type=chunk[9];
            if(chunk[10]!=0 || chunk[11]!=0) return false;
            interlace=chunk[12];
            have_ihdr=true;
        } else if(type[0]=='I'&&type[1]=='D'&&type[2]=='A'&&type[3]=='T'){
            if(!have_ihdr) return false;
            idat_total+=len;
        } else if(type[0]=='I'&&type[1]=='E'&&type[2]=='N'&&type[3]=='D'){
            break;
        }
        pos+=8+len+4;
    }
    if(!have_ihdr || !idat_total) return false;
    if(!width || width>WP_MAX_DIM || !height || height>WP_MAX_DIM)
        return false;
    if(bit_depth!=8 || interlace!=0) return false;
    uint32_t channels=0;
    if(color_type==0) channels=1;
    else if(color_type==2) channels=3;
    else if(color_type==6) channels=4;
    else return false;
    uint64_t stride=(uint64_t)width*channels+1u;
    uint64_t raw_size=stride*height;
    if(!raw_size || raw_size>16u*1024u*1024u) return false;
    struct wp_buf idat={0}, raw={0};
    if(!wp_alloc(&idat,idat_total)){ return false; }
    if(!wp_alloc(&raw,(uint32_t)raw_size)){ wp_free(&idat); return false; }
    pos=8;
    uint32_t copied=0;
    uint8_t *idat_p=(uint8_t*)idat.ptr;
    while(pos+8<=size && copied<idat_total){
        uint32_t len=wp_u32be(data+pos);
        const uint8_t *type=data+pos+4;
        const uint8_t *chunk=data+pos+8;
        if(type[0]=='I'&&type[1]=='D'&&type[2]=='A'&&type[3]=='T'){
            if(copied+len>idat_total){ wp_free(&idat); wp_free(&raw); return false; }
            for(uint32_t i=0;i<len;i++) idat_p[copied++]=chunk[i];
        } else if(type[0]=='I'&&type[1]=='E'&&type[2]=='N'&&type[3]=='D'){
            break;
        }
        pos+=8+len+4;
    }
    bool ok=false;
    if(copied==idat_total){
        uint32_t inflated=0;
        if(wp_inflate(idat_p,idat_total,(uint8_t*)raw.ptr,
                (uint32_t)raw_size,&inflated)
            && inflated==raw_size){
            ok=true;
            uint8_t *raw_p=(uint8_t*)raw.ptr;
            for(uint32_t y=0;y<height && ok;y++){
                uint8_t *row=raw_p+(uint64_t)y*stride;
                uint8_t filter=row[0];
                if(filter>4){ ok=false; break; }
                uint8_t *prev=y ? raw_p+(uint64_t)(y-1)*stride : 0;
                for(uint32_t i=1;i<stride;i++){
                    uint8_t a=i>channels ? row[i-channels] : 0;
                    uint8_t b=prev ? prev[i] : 0;
                    uint8_t c=(prev && i>channels) ? prev[i-channels] : 0;
                    uint8_t v=row[i];
                    switch(filter){
                        case 0: break;
                        case 1: v=(uint8_t)(v+a); break;
                        case 2: v=(uint8_t)(v+b); break;
                        case 3: v=(uint8_t)(v+(a+b)/2); break;
                        default: v=(uint8_t)(v+wp_paeth(a,b,c)); break;
                    }
                    row[i]=v;
                }
            }
            if(ok){
                for(uint32_t dy=0;dy<dst_h;dy++){
                    uint32_t src_y=(dy*height)/dst_h;
                    uint8_t *row=raw_p+(uint64_t)src_y*stride;
                    for(uint32_t dx=0;dx<dst_w;dx++){
                        uint32_t src_x=(dx*width)/dst_w;
                        uint8_t *px=row+1+(uint64_t)src_x*channels;
                        uint32_t color;
                        if(channels==1)
                            color=((uint32_t)px[0]<<16)|((uint32_t)px[0]<<8)|px[0];
                        else
                            color=((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2];
                        dst[(uint64_t)dy*dst_w+dx]=color;
                    }
                }
            }
        }
    }
    wp_free(&idat);
    wp_free(&raw);
    return ok;
}

/* ---------- load + draw ---------- */

static bool wp_try_load(uint32_t scr_w, uint32_t scr_h){
    struct wp_buf file={0};
    uint32_t file_size=0;
    if(!wp_load_file(g_path,&file,&file_size)){
        klogf(KLOG_WARN,"wallpaper: cannot read '%s'",g_path);
        return false;
    }
    struct wp_buf screen={0};
    uint64_t screen_bytes=(uint64_t)scr_w*scr_h*4u;
    if(!screen_bytes || screen_bytes>32u*1024u*1024u){
        wp_free(&file);
        return false;
    }
    if(!wp_alloc(&screen,screen_bytes)){
        klogf(KLOG_WARN,"wallpaper: out of memory for %ux%u",
            scr_w,scr_h);
        wp_free(&file);
        return false;
    }
    const uint8_t *data=(const uint8_t*)file.ptr;
    uint32_t *dst=(uint32_t*)screen.ptr;
    bool decoded=wp_decode_bmp(data,file_size,dst,scr_w,scr_h);
    if(!decoded && file_size>=8 && data[0]=='P' && data[1]=='6')
        decoded=wp_decode_ppm(data,file_size,dst,scr_w,scr_h);
    if(!decoded)
        decoded=wp_decode_png(data,file_size,dst,scr_w,scr_h);
    wp_free(&file);
    if(!decoded){
        klogf(KLOG_WARN,
            "wallpaper: unsupported format '%s' (BMP 24/32/8, PPM P6, PNG 8-bit RGB/RGBA/Gray)",
            g_path);
        wp_free(&screen);
        return false;
    }
    wp_free(&g_cache);
    g_cache=screen;
    g_cache_w=scr_w;
    g_cache_h=scr_h;
    klogf(KLOG_OK,"wallpaper: loaded '%s' -> %ux%u",g_path,scr_w,scr_h);
    return true;
}

bool wallpaper_draw(void){
    if(!g_path[0]) return false;
    if(!pmm_is_ready()) return false;
    uint32_t scr_w=gop_get_width();
    uint32_t scr_h=gop_get_height();
    if(!scr_w || !scr_h) return false;
    if(g_cache.ptr && g_cache_w==scr_w && g_cache_h==scr_h)
        return gop_blit_cover((const uint32_t*)g_cache.ptr,scr_w,scr_h);
    if(g_failed){
        uint64_t now=timer_ticks();
        if(now-g_last_attempt_ms<WP_RETRY_MS) return false;
    }
    g_last_attempt_ms=timer_ticks();
    if(wp_try_load(scr_w,scr_h)){
        g_failed=false;
        return gop_blit_cover((const uint32_t*)g_cache.ptr,scr_w,scr_h);
    }
    g_failed=true;
    wp_free(&g_cache);
    g_cache_w=0;
    g_cache_h=0;
    return false;
}
