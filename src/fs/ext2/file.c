#include "include/ext2_types.h"
#include "../types/fs_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_file.h"
#include "../../lib/string.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../mm/pmm.h"

static struct ext2_handle g_handles[EXT2_MAX_OPEN];

void ext2_file_handles_reset(void) {
    memset(g_handles, 0, sizeof(g_handles));
}

int32_t ext2_file_open(const char *path) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) {
        return st;
    }
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) {
        return -1;
    }
    uint16_t mode = ext2_read_u16(ib);
    if ((mode & 0xF000) == EXT2_S_IFDIR) {
        return -6;
    }
    for (int i = 0; i < EXT2_MAX_OPEN; i++) {
        if (!g_handles[i].used) {
            g_handles[i].used = true;
            g_handles[i].inode = ino;
            g_handles[i].size = ext2_read_u32(ib + 4);
            g_handles[i].position = 0;
            return EXT2_DESCRIPTOR_BASE + i;
        }
    }
    return -4;
}

int32_t ext2_file_read(int32_t descriptor, void *buffer, uint32_t count) {
    int idx = descriptor - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    if (!buffer && count) {
        return -3;
    }
    struct ext2_handle *h = &g_handles[idx];
    if (h->position >= h->size) {
        return 0;
    }
    if (h->position + count > h->size) {
        count = h->size - h->position;
    }
    uint8_t ib[256];
    if (!ext2_inode_read(h->inode, ib)) {
        return -1;
    }
    int32_t got = ext2_inode_read_data(ib, h->position, buffer, count);
    if (got > 0) {
        h->position += (uint32_t)got;
    }
    return got;
}

int64_t ext2_file_seek(int32_t descriptor, int64_t offset, uint32_t whence) {
    int idx = descriptor - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return -3;
    }
    struct ext2_handle *h = &g_handles[idx];
    int64_t base = 0;
    if (whence == SEEK_CUR) base = (int64_t)h->position;
    else if (whence == SEEK_END) base = (int64_t)h->size;
    int64_t target = base + offset;
    if (target < 0) {
        return -3;
    }
    // ext2 reads walk block pointers from the inode on every call, so no
    // cached cluster state needs resyncing (unlike FAT32).
    h->position = (uint32_t)(target > 0xFFFFFFFFLL ? 0xFFFFFFFFLL : target);
    return (int64_t)h->position;
}

static int32_t ext2_write_data(uint32_t ino, const uint8_t *data, uint32_t size) {
    struct ext2_volume *vol = ext2_volume();
    uint32_t bsz = vol->block_size;      /* actual block size (1024, 2048, or 4096) */
    uint32_t per = bsz / 4;             /* number of block pointers per indirect block */
    uint8_t ib[256];
    uint8_t old_ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    memcpy(old_ib, ib, 256);
    uint32_t bcnt = (size + bsz - 1) / bsz;
    memset(ib + 40, 0, 60);
    /*
     * These buffers must survive the entire loop, so they cannot alias
     * ext2_scratch_block() which is reused as tmp inside the loop.
     * FS access is serialised by filesystem_syscall_lock(), so static is safe.
     */
    static uint8_t single_buf[4096];
    static uint8_t dind_buf_s[4096];
    static uint8_t tind_buf_s[4096];
    static uint8_t cur_ind_buf[4096];
    uint8_t *single   = single_buf;
    uint8_t *dind_buf = dind_buf_s;
    uint8_t *tind_buf = tind_buf_s;
    uint8_t *cur_ind  = cur_ind_buf;
    memset(single,   0, bsz);
    memset(dind_buf, 0, bsz);
    memset(tind_buf, 0, bsz);
    uint32_t single_blk = 0, dind_blk = 0, tind_blk = 0;
    uint32_t dind_used = 0;  /* current slot index being built in dind_buf */
    uint32_t cur_ind_blk = 0;
    bool fail = false;
    for (uint32_t i = 0; i < bcnt; i++) {
        uint32_t nb = ext2_alloc_block();
        if (!nb) { klogf(KLOG_ERROR,"ext2: alloc block failed i=%u bcnt=%u size=%u",i,bcnt,size); fail = true; break; }
        /* write this logical block's data — ext2_scratch_block() is safe here as
         * single/dind_buf/tind_buf/cur_ind now use their own static buffers */
        uint8_t *tmp = ext2_scratch_block();
        memset(tmp, 0, bsz);
        uint32_t off = i * bsz;
        uint32_t left = size - off;
        uint32_t cur = left > bsz ? bsz : left;
        memcpy(tmp, data + off, cur);
        if (!ext2_write_block(nb, tmp)) { ext2_free_block(nb); fail = true; break; }
        if (i < 12) {
            ext2_write_u32(ib + 40 + i * 4, nb);
        } else if (i < 12 + per) {
            if (!single_blk) { single_blk = ext2_alloc_block(); if (!single_blk) { ext2_free_block(nb); fail = true; break; } }
            ext2_write_u32(single + (i - 12) * 4, nb);
        } else if (i < 12 + per + per * per) {
            if (!dind_blk) { dind_blk = ext2_alloc_block(); if (!dind_blk) { ext2_free_block(nb); fail = true; break; } }
            uint32_t idx = i - (12 + per);
            uint32_t which = idx / per;
            uint32_t inner = idx % per;
            if (which != dind_used) {
                if (cur_ind_blk) {
                    if (!ext2_write_block(cur_ind_blk, cur_ind)) { fail = true; break; }
                    /* Fix: was (which - 1) which is wrong when which jumps; use dind_used = previous slot */
                    ext2_write_u32(dind_buf + dind_used * 4, cur_ind_blk);
                }
                cur_ind_blk = ext2_alloc_block();
                if (!cur_ind_blk) { ext2_free_block(nb); fail = true; break; }
                memset(cur_ind, 0, bsz);
                dind_used = which; /* track current slot index */
            } else if (!cur_ind_blk) {
                cur_ind_blk = ext2_alloc_block();
                if (!cur_ind_blk) { ext2_free_block(nb); fail = true; break; }
                memset(cur_ind, 0, bsz);
                dind_used = 0;
            }
            ext2_write_u32(cur_ind + inner * 4, nb);
            if (inner == per - 1 || i + 1 == bcnt) {
                if (!ext2_write_block(cur_ind_blk, cur_ind)) { fail = true; break; }
                ext2_write_u32(dind_buf + which * 4, cur_ind_blk);
                cur_ind_blk = 0;
            }
        } else {
            if (!tind_blk) { tind_blk = ext2_alloc_block(); if (!tind_blk) { ext2_free_block(nb); fail = true; break; } memset(tind_buf, 0, bsz); }
            uint32_t idx = i - (12 + per + per * per);
            uint32_t d = idx / (per * per);
            uint32_t r = idx % (per * per);
            uint32_t w = r / per;
            uint32_t inner = r % per;
            if (d >= per) { ext2_free_block(nb); fail = true; break; }
            uint32_t dblk = ext2_read_u32(tind_buf + d * 4);
            if (!dblk) {
                dblk = ext2_alloc_block();
                if (!dblk) { ext2_free_block(nb); fail = true; break; }
                /* use scratch_block() for zero buf — safe because tmp was already used above and we don't need it */
                uint8_t *zero = ext2_scratch_block();
                memset(zero, 0, bsz);
                if (!ext2_write_block(dblk, zero)) { ext2_free_block(dblk); ext2_free_block(nb); fail = true; break; }
                ext2_write_u32(tind_buf + d * 4, dblk);
            }
            uint8_t *dbuf2 = ext2_scratch_block2();
            if (!ext2_read_block(dblk, dbuf2)) memset(dbuf2, 0, bsz);
            uint32_t iblk = ext2_read_u32(dbuf2 + w * 4);
            if (!iblk) {
                iblk = ext2_alloc_block();
                if (!iblk) { ext2_free_block(nb); fail = true; break; }
                uint8_t *zero2 = ext2_scratch_block();  /* scratch_block() ≠ scratch_block2() → no alias */
                memset(zero2, 0, bsz);
                if (!ext2_write_block(iblk, zero2)) { ext2_free_block(iblk); ext2_free_block(nb); fail = true; break; }
                ext2_write_u32(dbuf2 + w * 4, iblk);
                if (!ext2_write_block(dblk, dbuf2)) { fail = true; break; }
            }
            uint8_t *sbuf = ext2_scratch_block();
            if (!ext2_read_block(iblk, sbuf)) memset(sbuf, 0, bsz);
            ext2_write_u32(sbuf + inner * 4, nb);
            if (!ext2_write_block(iblk, sbuf)) { fail = true; break; }
        }
    }
    if (fail) {
        // free any partially allocated new blocks to avoid leak
        // Use current ib (partially built) to free what we allocated
        if (single_blk) ext2_write_u32(ib + 40 + 12 * 4, single_blk);
        if (dind_blk) {
            if (cur_ind_blk) {
                // cur_ind not yet flushed - free it
                ext2_free_block(cur_ind_blk);
                // dind_buf still holds previous entries, but not yet written? we wrote dind_buf partly
                // To be safe, free dind already allocated and its children via inode free
            }
            if (dind_used > 0) {
                // ensure dind_buf written? if not, can't free via inode walk - free manually
                // Instead let inode free walk handle: temporarily set dind pointer
                ext2_write_u32(ib + 40 + 13 * 4, dind_blk);
            } else {
                ext2_free_block(dind_blk);
            }
        }
        if (tind_blk) ext2_write_u32(ib + 40 + 14 * 4, tind_blk);
        // if we set pointers, free via helper which understands structure
        // For single, need to ensure pointer set before free walk
        // We'll do a best-effort cleanup using the helper
        ext2_inode_free_blocks(ib);
        return -4;
    }
    if (single_blk) {
        if (!ext2_write_block(single_blk, single)) { ext2_inode_free_blocks(ib); return -1; }
        ext2_write_u32(ib + 40 + 12 * 4, single_blk);
    }
    if (dind_blk) {
        if (cur_ind_blk) {
            if (!ext2_write_block(cur_ind_blk, cur_ind)) { ext2_inode_free_blocks(ib); return -1; }
            ext2_write_u32(dind_buf + dind_used * 4, cur_ind_blk);
        }
        if (!ext2_write_block(dind_blk, dind_buf)) { ext2_inode_free_blocks(ib); return -1; }
        ext2_write_u32(ib + 40 + 13 * 4, dind_blk);
    }
    if (tind_blk) {
        if (!ext2_write_block(tind_blk, tind_buf)) { ext2_inode_free_blocks(ib); return -1; }
        ext2_write_u32(ib + 40 + 14 * 4, tind_blk);
    }
    ext2_write_u32(ib + 4, size);
    ext2_write_u32(ib + 28, bcnt * (bsz / 512));  /* i_blocks: 512-byte units */
    ext2_write_u16(ib + 24, 1);
    // preserve mode type but ensure regular file
    uint16_t old_mode = ext2_read_u16(old_ib);
    if ((old_mode & 0xF000) == 0x4000) ext2_write_u16(ib, 0x41ED);
    else ext2_write_u16(ib, 0x81A4);
    // free old blocks after successful new allocation
    ext2_inode_free_blocks(old_ib);
    return ext2_write_inode(ino, ib) ? (int32_t)size : -1;
}

static int32_t ext2_create_file_internal(const char *path) {
    const char *p=path;
    while(*p=='/') p++;
    if(!*p) return -3;
    char parent[256]={0}, base[64]={0};
    const char *slash=0;
    for(const char *t=p; *t; t++) if(*t=='/') slash=t;
    if(slash){
        uint32_t plen=slash-p;
        if(plen>=sizeof(parent)) return -3;
        memcpy(parent, "/",1);
        memcpy(parent+1, p, plen);
        parent[plen+1]='\0';
        const char *b=slash+1;
        uint32_t blen=strlen(b);
        if(blen>=sizeof(base)) return -3;
        memcpy(base,b,blen);
    } else {
        parent[0]='/'; base[0]='\0';
        uint32_t blen=strlen(p);
        if(blen>=sizeof(base)) return -3;
        memcpy(base,p,blen);
    }
    // actually simplify: use dir_resolve for parent
    uint32_t par_ino;
    char pardir[256];
    if(slash){
        uint32_t l=slash-p;
        pardir[0]='/';
        memcpy(pardir+1,p,l);
        pardir[l+1]='\0';
    } else {
        pardir[0]='/'; pardir[1]='\0';
    }
    if(slash) {
        int32_t st=ext2_dir_resolve(pardir,&par_ino);
        if(st<0) {
            st=ext2_file_create_dir(pardir);
            if(st<0 && st!=-5) return st;
            st=ext2_dir_resolve(pardir,&par_ino);
            if(st<0) return st;
        }
    } else par_ino=2;
    uint32_t exist;
    if(ext2_dir_find(par_ino, base, &exist, 0)==0) return -5;
    uint32_t nino=ext2_alloc_inode();
    if(!nino) return -4;
    uint8_t ib[256]={0};
    ext2_write_u16(ib, 0x81A4);
    ext2_write_u32(ib+4, 0);
    ext2_write_u16(ib+24,1);
    if(!ext2_write_inode(nino, ib)) return -1;
    int32_t r=ext2_dir_add_entry(par_ino, base, nino, 1);
    return r<0 ? r : 0;
}

int32_t ext2_file_close(int32_t d) {
    int idx = d - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    g_handles[idx].used = false;
    return 0;
}

int32_t ext2_file_create(const char *path) {
    return ext2_create_file_internal(path);
}

int32_t ext2_file_write(const char *path, const void *buffer, uint32_t count) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) {
        st = ext2_create_file_internal(path);
        if (st < 0 && st != -5) return st;
        st = ext2_dir_resolve(path, &ino);
    }
    if (st < 0) return st;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint16_t mode = ext2_read_u16(ib);
    if ((mode & 0xF000) == 0x4000) return -6;
    if (count == 0) return ext2_write_data(ino, (const uint8_t*)buffer, 0);
    return ext2_write_data(ino, (const uint8_t*)buffer, count);
}

int32_t ext2_file_append(const char *path, const void *buffer, uint32_t count) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) return ext2_file_write(path, buffer, count);
    if (count == 0) return 0;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint32_t old = ext2_read_u32(ib + 4);
    if (old == 0) return ext2_write_data(ino, (const uint8_t*)buffer, count);
    uint32_t nsize = old + count;
    // guard against overflow
    if (nsize < old) return -3;
    // allocate temporary buffer using pmm to avoid scratch overflow
    uint64_t pages = (nsize + 4095) / 4096;
    if (pages == 0) pages = 1;
    // limit to 64 MiB to avoid huge allocation starvation
    if (nsize > 64 * 1024 * 1024) return -4;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if (!phys) return -4;
    uint8_t *combined = (uint8_t*)pmm_physical_to_virtual(phys);
    int32_t r = ext2_inode_read_data(ib, 0, combined, old);
    if (r < 0 || (uint32_t)r != old) {
        pmm_free_contiguous(phys, pages);
        return -1;
    }
    memcpy(combined + old, buffer, count);
    int32_t res = ext2_write_data(ino, combined, nsize);
    pmm_free_contiguous(phys, pages);
    return res;
}

int32_t ext2_file_create_dir(const char *path) {
    const char *p = path;
    while (*p == '/') p++;
    if (!*p) return -3;
    const char *slash = 0;
    for (const char *t = p; *t; t++) if (*t == '/') slash = t;
    char pardir[256], base[64] = {0};
    if (slash) {
        uint32_t l = slash - p;
        pardir[0] = '/'; memcpy(pardir + 1, p, l); pardir[l + 1] = '\0';
        const char *b = slash + 1;
        uint32_t bl = strlen(b);
        if (bl >= sizeof(base)) return -3;
        memcpy(base, b, bl);
    } else {
        pardir[0] = '/'; pardir[1] = '\0';
        uint32_t bl = strlen(p);
        if (bl >= sizeof(base)) return -3;
        memcpy(base, p, bl);
    }
    uint32_t par;
    int32_t st = ext2_dir_resolve(pardir, &par);
    if (st < 0) return st;
    uint32_t exist;
    if (ext2_dir_find(par, base, &exist, 0) == 0) return -5;
    return ext2_dir_create(par, base);
}
