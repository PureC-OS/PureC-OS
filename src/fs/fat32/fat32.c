#include "fat32_abi.h"
#include "../vfs_ops.h"
#include "../../io/storage/block_device.h"
#include "../../lib/string.h"

#define SECTOR 512u
#define BURST 32u
#define MAX_HANDLES 8
#define CLUSTER_END 0x0FFFFFF8u
#define ATTR_VOLUME 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LONG_NAME 0x0F
#define NAME_MAX_LEN 255

struct handle {
  bool used;
  uint32_t first;
  uint32_t size;
  uint32_t position;
  uint32_t current;
  uint32_t current_index;
};

struct volume {
  bool ready;
  uint32_t device;
  uint32_t partition;
  uint32_t sectors_per_cluster;
  uint32_t fat_lba;
  uint32_t data_lba;
  uint32_t root_cluster;
  uint32_t cluster_count;
  uint32_t fat_count;
  uint32_t fat_size;
  uint32_t total_sectors;
  uint32_t fsinfo_lba;
  uint32_t alloc_hint;
  bool fsinfo_touched;
  bool fat_valid;
  uint32_t fat_lba_cached;
  uint8_t fat_sector[SECTOR];
  bool scan_valid;
  uint32_t scan_lba_cached;
  uint8_t scan_sector[SECTOR];
};

struct entry {
  char name[NAME_MAX_LEN + 1];
  char alias[13];
  uint8_t attributes;
  uint32_t cluster;
  uint32_t size;
  uint32_t entry_lba;
  uint32_t entry_offset;
};

struct dir_iter {
  uint32_t cluster;
  uint32_t sector;
  uint32_t index;
  bool long_valid;
  uint8_t long_checksum;
  char long_name[NAME_MAX_LEN + 1];
};

static struct volume volume;
static struct handle handles[MAX_HANDLES];

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool read_run(uint32_t lba, void *buffer, uint32_t count) {
  uint8_t *out = buffer;
  while (count) {
    uint32_t now = count > BURST ? BURST : count;
    if (!block_device_raw_run(volume.device, false, lba, out, now))
      return false;
    lba += now;
    out += now * SECTOR;
    count -= now;
  }
  return true;
}

static bool lba_allowed(uint32_t lba, uint32_t count) {
  return lba >= volume.partition &&
         (uint64_t)lba + count <= (uint64_t)volume.partition + volume.total_sectors;
}

static bool write_run(uint32_t lba, const void *buffer, uint32_t count) {
  const uint8_t *in = buffer;
  if (!lba_allowed(lba, count))
    return false;
  volume.scan_valid = false;
  while (count) {
    uint32_t now = count > BURST ? BURST : count;
    if (!block_device_raw_run(volume.device, true, lba, (void *)in, now))
      return false;
    lba += now;
    in += now * SECTOR;
    count -= now;
  }
  return true;
}

static const uint8_t *scan_sector(uint32_t lba) {
  if (!volume.scan_valid || volume.scan_lba_cached != lba) {
    if (!read_run(lba, volume.scan_sector, 1)) {
      volume.scan_valid = false;
      return 0;
    }
    volume.scan_valid = true;
    volume.scan_lba_cached = lba;
  }
  return volume.scan_sector;
}

static bool cluster_valid(uint32_t cluster) {
  return cluster >= 2 && cluster < volume.cluster_count + 2;
}

static uint32_t cluster_lba(uint32_t cluster) {
  return volume.data_lba + (cluster - 2) * volume.sectors_per_cluster;
}

static bool fat_load(uint32_t relative) {
  uint32_t lba = volume.fat_lba + relative;
  if (volume.fat_valid && volume.fat_lba_cached == lba)
    return true;
  if (!read_run(lba, volume.fat_sector, 1)) {
    volume.fat_valid = false;
    return false;
  }
  volume.fat_valid = true;
  volume.fat_lba_cached = lba;
  return true;
}

static uint32_t fat_next(uint32_t cluster) {
  if (!cluster_valid(cluster))
    return CLUSTER_END;
  uint32_t offset = cluster * 4;
  if (!fat_load(offset / SECTOR))
    return CLUSTER_END;
  return le32(volume.fat_sector + offset % SECTOR) & 0x0FFFFFFFu;
}

static char lower(char c) {
  return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool equal_nocase(const char *a, const char *b) {
  while (*a && *b) {
    if (lower(*a) != lower(*b))
      return false;
    a++;
    b++;
  }
  return *a == *b;
}

static uint8_t short_checksum(const uint8_t *name) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++)
    sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]);
  return sum;
}

static void build_alias(const uint8_t *raw, char out[13]) {
  uint8_t flags = raw[12];
  uint32_t n = 0;
  uint32_t base = 8;
  while (base && raw[base - 1] == ' ')
    base--;
  for (uint32_t i = 0; i < base; i++) {
    char c = (char)raw[i];
    if (i == 0 && (uint8_t)c == 0x05)
      c = (char)0xE5;
    out[n++] = (flags & 0x08) ? lower(c) : c;
  }
  uint32_t ext = 3;
  while (ext && raw[8 + ext - 1] == ' ')
    ext--;
  if (ext) {
    out[n++] = '.';
    for (uint32_t i = 0; i < ext; i++) {
      char c = (char)raw[8 + i];
      out[n++] = (flags & 0x10) ? lower(c) : c;
    }
  }
  out[n] = 0;
}

static void store_long_part(struct dir_iter *it, const uint8_t *raw) {
  static const uint8_t offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
  uint32_t seq = raw[0] & 0x1Fu;
  if (!seq)
    return;
  if (raw[0] & 0x40) {
    memset(it->long_name, 0, sizeof(it->long_name));
    it->long_valid = true;
    it->long_checksum = raw[13];
  }
  if (!it->long_valid || raw[13] != it->long_checksum)
    return;
  uint32_t base = (seq - 1) * 13;
  for (uint32_t k = 0; k < 13; k++) {
    uint16_t unit = le16(raw + offsets[k]);
    if (base + k >= NAME_MAX_LEN)
      return;
    if (unit == 0x0000 || unit == 0xFFFF)
      continue;
    it->long_name[base + k] = unit < 0x80 ? (char)unit : '?';
  }
}

static bool dir_next(struct dir_iter *it, struct entry *out) {
  while (cluster_valid(it->cluster)) {
    const uint8_t *sector = scan_sector(cluster_lba(it->cluster) + it->sector);
    if (!sector) {
      it->cluster = CLUSTER_END;
      return false;
    }
    const uint8_t *raw = sector + it->index * 32;
    uint32_t previous_cluster = it->cluster;
    uint32_t previous_sector = it->sector;
    uint32_t previous_index = it->index;
    it->index++;
    if (it->index == SECTOR / 32) {
      it->index = 0;
      it->sector++;
      if (it->sector == volume.sectors_per_cluster) {
        it->sector = 0;
        it->cluster = fat_next(it->cluster);
      }
    }
    if (raw[0] == 0x00) {
      it->cluster = CLUSTER_END;
      return false;
    }
    uint32_t here_lba = cluster_lba(previous_cluster) + previous_sector;
    uint32_t here_offset = previous_index * 32;
    if (raw[0] == 0xE5) {
      it->long_valid = false;
      continue;
    }
    uint8_t attributes = raw[11];
    if ((attributes & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
      store_long_part(it, raw);
      continue;
    }
    if (attributes & ATTR_VOLUME) {
      it->long_valid = false;
      continue;
    }
    build_alias(raw, out->alias);
    if (it->long_valid && it->long_name[0] &&
        it->long_checksum == short_checksum(raw))
      memcpy(out->name, it->long_name, sizeof(out->name));
    else
      memcpy(out->name, out->alias, sizeof(out->alias));
    out->name[NAME_MAX_LEN] = 0;
    it->long_valid = false;
    out->attributes = attributes;
    out->cluster = ((uint32_t)le16(raw + 20) << 16) | le16(raw + 26);
    out->size = le32(raw + 28);
    out->entry_lba = here_lba;
    out->entry_offset = here_offset;
    return true;
  }
  return false;
}

static bool is_dot(const struct entry *e) {
  return strcmp(e->alias, ".") == 0 || strcmp(e->alias, "..") == 0;
}

static void iter_start(struct dir_iter *it, uint32_t cluster) {
  memset(it, 0, sizeof(*it));
  it->cluster = cluster;
}

static int32_t find_child(uint32_t dir_cluster, const char *name,
                          struct entry *out) {
  struct dir_iter it;
  iter_start(&it, dir_cluster);
  while (dir_next(&it, out)) {
    if (is_dot(out))
      continue;
    if (equal_nocase(out->name, name) || equal_nocase(out->alias, name))
      return 0;
  }
  return FS_ERROR_NOT_FOUND;
}

static int32_t resolve(const char *path, struct entry *out) {
  memset(out, 0, sizeof(*out));
  out->attributes = ATTR_DIRECTORY;
  out->cluster = volume.root_cluster;
  while (*path) {
    while (*path == '/')
      path++;
    if (!*path)
      break;
    char part[NAME_MAX_LEN + 1];
    uint32_t n = 0;
    while (*path && *path != '/') {
      if (n >= NAME_MAX_LEN)
        return FS_ERROR_INVALID;
      part[n++] = *path++;
    }
    part[n] = 0;
    if (strcmp(part, ".") == 0)
      continue;
    if (!(out->attributes & ATTR_DIRECTORY))
      return FS_ERROR_NOT_DIR;
    struct entry next;
    int32_t status = find_child(out->cluster ? out->cluster : volume.root_cluster,
                                part, &next);
    if (status < 0)
      return status;
    *out = next;
    if ((out->attributes & ATTR_DIRECTORY) && out->cluster == 0)
      out->cluster = volume.root_cluster;
  }
  return 0;
}

static struct handle *handle_of(int32_t descriptor) {
  if (descriptor < 0 || descriptor >= MAX_HANDLES || !handles[descriptor].used)
    return 0;
  return &handles[descriptor];
}

static int32_t fat32_open(void *context, const char *path) {
  (void)context;
  struct entry found;
  int32_t status = resolve(path, &found);
  if (status < 0)
    return status;
  if (found.attributes & ATTR_DIRECTORY)
    return FS_ERROR_NOT_FILE;
  for (int i = 0; i < MAX_HANDLES; i++) {
    if (handles[i].used)
      continue;
    handles[i].used = true;
    handles[i].first = found.cluster;
    handles[i].size = found.size;
    handles[i].position = 0;
    handles[i].current = found.cluster;
    handles[i].current_index = 0;
    return i;
  }
  return FS_ERROR_BUSY;
}

static int32_t fat32_read(void *context, int32_t descriptor, void *buffer,
                          uint32_t count) {
  (void)context;
  struct handle *h = handle_of(descriptor);
  if (!h || !buffer)
    return FS_ERROR_INVALID;
  if (h->position >= h->size)
    return 0;
  if (count > h->size - h->position)
    count = h->size - h->position;
  uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR;
  uint8_t *out = buffer;
  uint32_t done = 0;
  while (done < count) {
    uint32_t index = h->position / cluster_bytes;
    if (index < h->current_index || !cluster_valid(h->current)) {
      h->current = h->first;
      h->current_index = 0;
    }
    while (h->current_index < index && cluster_valid(h->current)) {
      h->current = fat_next(h->current);
      h->current_index++;
    }
    if (!cluster_valid(h->current))
      break;
    uint32_t within = h->position % cluster_bytes;
    uint32_t sector_in_cluster = within / SECTOR;
    uint32_t offset = within % SECTOR;
    uint32_t lba = cluster_lba(h->current) + sector_in_cluster;
    uint32_t left = count - done;
    if (offset == 0 && left >= SECTOR) {
      uint32_t sectors = left / SECTOR;
      uint32_t room = volume.sectors_per_cluster - sector_in_cluster;
      if (sectors > room)
        sectors = room;
      if (sectors > BURST)
        sectors = BURST;
      if (!read_run(lba, out + done, sectors))
        return done ? (int32_t)done : FS_ERROR_IO;
      done += sectors * SECTOR;
      h->position += sectors * SECTOR;
      continue;
    }
    const uint8_t *sector = scan_sector(lba);
    if (!sector)
      return done ? (int32_t)done : FS_ERROR_IO;
    uint32_t chunk = SECTOR - offset;
    if (chunk > left)
      chunk = left;
    memcpy(out + done, sector + offset, chunk);
    done += chunk;
    h->position += chunk;
  }
  return (int32_t)done;
}

static int64_t fat32_seek(void *context, int32_t descriptor, int64_t offset,
                          uint32_t whence) {
  (void)context;
  struct handle *h = handle_of(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  int64_t base = whence == SEEK_SET   ? 0
                 : whence == SEEK_CUR ? (int64_t)h->position
                 : whence == SEEK_END ? (int64_t)h->size
                                      : -1;
  if (base < 0)
    return FS_ERROR_INVALID;
  int64_t target = base + offset;
  if (target < 0 || target > (int64_t)h->size)
    return FS_ERROR_INVALID;
  h->position = (uint32_t)target;
  return target;
}

static int32_t fat32_close(void *context, int32_t descriptor) {
  (void)context;
  struct handle *h = handle_of(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  h->used = false;
  return 0;
}

static int32_t fat32_stat(void *context, const char *path,
                          struct file_stat_info *out) {
  (void)context;
  struct entry found;
  int32_t status = resolve(path, &found);
  if (status < 0)
    return status;
  out->size = (found.attributes & ATTR_DIRECTORY) ? 0 : found.size;
  out->is_directory = (found.attributes & ATTR_DIRECTORY) ? 1 : 0;
  out->reserved = 0;
  return 0;
}

static int32_t list_names(const char *path, void *entries, uint32_t capacity,
                          bool long_names) {
  struct entry dir;
  int32_t status = resolve(path, &dir);
  if (status < 0)
    return status;
  if (!(dir.attributes & ATTR_DIRECTORY))
    return FS_ERROR_NOT_DIR;
  struct dir_iter it;
  iter_start(&it, dir.cluster ? dir.cluster : volume.root_cluster);
  struct entry e;
  uint32_t count = 0;
  while (count < capacity && dir_next(&it, &e)) {
    if (is_dot(&e))
      continue;
    uint8_t attributes = (e.attributes & ATTR_DIRECTORY) ? FS_ATTRIBUTE_DIRECTORY : 0;
    uint32_t size = (e.attributes & ATTR_DIRECTORY) ? 0 : e.size;
    if (long_names) {
      struct fs_directory_entry_long *slot =
          &((struct fs_directory_entry_long *)entries)[count];
      memset(slot, 0, sizeof(*slot));
      strncpy(slot->name, e.name, FS_LONG_NAME_CAPACITY - 1);
      slot->size = size;
      slot->attributes = attributes;
    } else {
      struct fs_directory_entry *slot =
          &((struct fs_directory_entry *)entries)[count];
      memset(slot, 0, sizeof(*slot));
      strncpy(slot->name, e.alias, FS_DIRECTORY_NAME_CAPACITY - 1);
      slot->size = size;
      slot->attributes = attributes;
    }
    count++;
  }
  return (int32_t)count;
}

static int32_t fat32_list(void *context, const char *path,
                          struct fs_directory_entry *entries, uint32_t capacity) {
  (void)context;
  return list_names(path, entries, capacity, false);
}

static int32_t fat32_list_long(void *context, const char *path,
                               struct fs_directory_entry_long *entries,
                               uint32_t capacity) {
  (void)context;
  return list_names(path, entries, capacity, true);
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void touch_fsinfo(void) {
  if (volume.fsinfo_touched || !volume.fsinfo_lba)
    return;
  volume.fsinfo_touched = true;
  uint8_t info[SECTOR];
  if (!read_run(volume.fsinfo_lba, info, 1))
    return;
  if (le32(info) != 0x41615252u || le32(info + 484) != 0x61417272u)
    return;
  put32(info + 488, 0xFFFFFFFFu);
  put32(info + 492, 0xFFFFFFFFu);
  (void)write_run(volume.fsinfo_lba, info, 1);
}

static bool fat_set(uint32_t cluster, uint32_t value) {
  if (!cluster_valid(cluster))
    return false;
  uint32_t offset = cluster * 4;
  uint32_t relative = offset / SECTOR;
  if (!fat_load(relative))
    return false;
  uint8_t *entry = volume.fat_sector + offset % SECTOR;
  put32(entry, (le32(entry) & 0xF0000000u) | (value & 0x0FFFFFFFu));
  for (uint32_t copy = 0; copy < volume.fat_count; copy++) {
    if (!write_run(volume.fat_lba + copy * volume.fat_size + relative,
                   volume.fat_sector, 1))
      return false;
  }
  return true;
}

static uint32_t fat_alloc(void) {
  uint32_t total = volume.cluster_count;
  uint32_t start = volume.alloc_hint;
  if (start < 2 || start >= total + 2)
    start = 2;
  for (uint32_t i = 0; i < total; i++) {
    uint32_t cluster = 2 + (start - 2 + i) % total;
    if (fat_next(cluster) != 0)
      continue;
    if (!fat_set(cluster, 0x0FFFFFFFu))
      return 0;
    volume.alloc_hint = cluster + 1;
    return cluster;
  }
  return 0;
}

static void chain_free(uint32_t cluster) {
  uint32_t guard = volume.cluster_count + 2;
  while (cluster_valid(cluster) && guard--) {
    uint32_t next = fat_next(cluster);
    if (!fat_set(cluster, 0))
      return;
    cluster = next;
  }
}

static int32_t update_entry(const struct entry *e) {
  if (!e->entry_lba)
    return FS_ERROR_INVALID;
  uint8_t sector[SECTOR];
  if (!read_run(e->entry_lba, sector, 1))
    return FS_ERROR_IO;
  uint8_t *raw = sector + e->entry_offset;
  put16(raw + 20, (uint16_t)(e->cluster >> 16));
  put16(raw + 26, (uint16_t)e->cluster);
  put32(raw + 28, e->size);
  if (!write_run(e->entry_lba, sector, 1))
    return FS_ERROR_IO;
  return 0;
}

static bool file_is_open(uint32_t first) {
  for (int i = 0; i < MAX_HANDLES; i++)
    if (handles[i].used && first && handles[i].first == first)
      return true;
  return false;
}

static int32_t file_write_at(struct entry *e, uint32_t offset,
                             const uint8_t *data, uint32_t count) {
  if (offset > e->size)
    return FS_ERROR_INVALID;
  if ((uint64_t)offset + count > 0xFFFFFFFFull)
    return FS_ERROR_NO_SPACE;
  touch_fsinfo();
  uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR;
  uint32_t end = offset + count;
  uint32_t need = (end + cluster_bytes - 1) / cluster_bytes;
  uint32_t have = 0;
  uint32_t last = 0;
  uint32_t walk = e->cluster;
  while (have < need && cluster_valid(walk)) {
    last = walk;
    have++;
    walk = fat_next(walk);
  }
  bool grown = false;
  while (have < need) {
    uint32_t fresh = fat_alloc();
    if (!fresh)
      return FS_ERROR_NO_SPACE;
    if (last) {
      if (!fat_set(last, fresh))
        return FS_ERROR_IO;
    } else {
      e->cluster = fresh;
    }
    last = fresh;
    have++;
    grown = true;
  }
  uint32_t position = offset;
  const uint8_t *source = data;
  uint32_t current = e->cluster;
  uint32_t index = 0;
  while (position < end) {
    uint32_t wanted = position / cluster_bytes;
    while (index < wanted) {
      current = fat_next(current);
      index++;
    }
    if (!cluster_valid(current))
      return FS_ERROR_IO;
    uint32_t within = position % cluster_bytes;
    uint32_t sector_in_cluster = within / SECTOR;
    uint32_t sector_offset = within % SECTOR;
    uint32_t lba = cluster_lba(current) + sector_in_cluster;
    uint32_t left = end - position;
    if (sector_offset == 0 && left >= SECTOR) {
      uint32_t sectors = left / SECTOR;
      uint32_t room = volume.sectors_per_cluster - sector_in_cluster;
      if (sectors > room)
        sectors = room;
      if (sectors > BURST)
        sectors = BURST;
      if (!write_run(lba, source, sectors))
        return FS_ERROR_IO;
      source += sectors * SECTOR;
      position += sectors * SECTOR;
      continue;
    }
    uint8_t scratch[SECTOR];
    uint32_t chunk = SECTOR - sector_offset;
    if (chunk > left)
      chunk = left;
    if (position - sector_offset < e->size) {
      if (!read_run(lba, scratch, 1))
        return FS_ERROR_IO;
    } else {
      memset(scratch, 0, SECTOR);
    }
    memcpy(scratch + sector_offset, source, chunk);
    if (!write_run(lba, scratch, 1))
      return FS_ERROR_IO;
    source += chunk;
    position += chunk;
  }
  (void)grown;
  if (end > e->size)
    e->size = end;
  return update_entry(e);
}

static int32_t file_truncate(struct entry *e) {
  touch_fsinfo();
  uint32_t first = e->cluster;
  e->cluster = 0;
  e->size = 0;
  int32_t status = update_entry(e);
  if (status < 0)
    return status;
  chain_free(first);
  return 0;
}

static bool short_char_ok(char c) {
  if (c >= 'A' && c <= 'Z')
    return true;
  if (c >= 'a' && c <= 'z')
    return true;
  if (c >= '0' && c <= '9')
    return true;
  return c == '_' || c == '-' || c == '$' || c == '~' || c == '!' || c == '#' ||
         c == '%' || c == '&' || c == '@' || c == '^' || c == '(' || c == ')';
}

static int32_t short_name_of(const char *leaf, uint8_t raw[11], uint8_t *flags) {
  uint32_t length = (uint32_t)strlen(leaf);
  if (!length)
    return FS_ERROR_INVALID;
  if (length > 12)
    return FS_ERROR_UNSUPPORTED;
  int32_t dot = -1;
  for (uint32_t i = 0; i < length; i++) {
    if (leaf[i] == '.') {
      if (dot >= 0)
        return FS_ERROR_UNSUPPORTED;
      dot = (int32_t)i;
    } else if (!short_char_ok(leaf[i])) {
      return FS_ERROR_UNSUPPORTED;
    }
  }
  uint32_t base_length = dot < 0 ? length : (uint32_t)dot;
  uint32_t ext_length = dot < 0 ? 0 : length - (uint32_t)dot - 1;
  if (!base_length || base_length > 8 || ext_length > 3)
    return FS_ERROR_UNSUPPORTED;
  bool base_lower = false, base_upper = false, ext_lower = false, ext_upper = false;
  memset(raw, ' ', 11);
  for (uint32_t i = 0; i < base_length; i++) {
    char c = leaf[i];
    if (c >= 'a' && c <= 'z') {
      base_lower = true;
      c = (char)(c - 32);
    } else if (c >= 'A' && c <= 'Z') {
      base_upper = true;
    }
    raw[i] = (uint8_t)c;
  }
  for (uint32_t i = 0; i < ext_length; i++) {
    char c = leaf[base_length + 1 + i];
    if (c >= 'a' && c <= 'z') {
      ext_lower = true;
      c = (char)(c - 32);
    } else if (c >= 'A' && c <= 'Z') {
      ext_upper = true;
    }
    raw[8 + i] = (uint8_t)c;
  }
  if ((base_lower && base_upper) || (ext_lower && ext_upper))
    return FS_ERROR_UNSUPPORTED;
  *flags = (uint8_t)((base_lower ? 0x08 : 0) | (ext_lower ? 0x10 : 0));
  return 0;
}

static bool dir_free_slot(uint32_t dir_cluster, uint32_t *lba, uint32_t *offset) {
  uint32_t cluster = dir_cluster;
  uint32_t last = 0;
  uint32_t guard = volume.cluster_count + 2;
  while (cluster_valid(cluster) && guard--) {
    for (uint32_t s = 0; s < volume.sectors_per_cluster; s++) {
      const uint8_t *sector = scan_sector(cluster_lba(cluster) + s);
      if (!sector)
        return false;
      for (uint32_t i = 0; i < SECTOR / 32; i++) {
        uint8_t first = sector[i * 32];
        if (first == 0x00 || first == 0xE5) {
          *lba = cluster_lba(cluster) + s;
          *offset = i * 32;
          return true;
        }
      }
    }
    last = cluster;
    cluster = fat_next(cluster);
  }
  if (!last)
    return false;
  uint32_t fresh = fat_alloc();
  if (!fresh)
    return false;
  uint8_t zero[SECTOR];
  memset(zero, 0, SECTOR);
  for (uint32_t s = 0; s < volume.sectors_per_cluster; s++)
    if (!write_run(cluster_lba(fresh) + s, zero, 1))
      return false;
  if (!fat_set(last, fresh))
    return false;
  *lba = cluster_lba(fresh);
  *offset = 0;
  return true;
}

static int32_t split_parent(const char *path, struct entry *parent, char *leaf) {
  uint32_t length = (uint32_t)strlen(path);
  while (length && path[length - 1] == '/')
    length--;
  uint32_t cut = length;
  while (cut && path[cut - 1] != '/')
    cut--;
  uint32_t leaf_length = length - cut;
  if (!leaf_length || leaf_length > NAME_MAX_LEN)
    return FS_ERROR_INVALID;
  memcpy(leaf, path + cut, leaf_length);
  leaf[leaf_length] = 0;
  char directory[NAME_MAX_LEN * 4];
  if (cut >= sizeof(directory))
    return FS_ERROR_INVALID;
  memcpy(directory, path, cut);
  directory[cut] = 0;
  int32_t status = resolve(directory, parent);
  if (status < 0)
    return status;
  if (!(parent->attributes & ATTR_DIRECTORY))
    return FS_ERROR_NOT_DIR;
  if (!parent->cluster)
    parent->cluster = volume.root_cluster;
  return 0;
}

static int32_t create_entry(const char *path, struct entry *out) {
  struct entry parent;
  char leaf[NAME_MAX_LEN + 1];
  int32_t status = split_parent(path, &parent, leaf);
  if (status < 0)
    return status;
  struct entry existing;
  if (find_child(parent.cluster, leaf, &existing) == 0)
    return FS_ERROR_EXISTS;
  uint8_t raw[11];
  uint8_t flags = 0;
  status = short_name_of(leaf, raw, &flags);
  if (status < 0)
    return status;
  touch_fsinfo();
  uint32_t lba = 0, offset = 0;
  if (!dir_free_slot(parent.cluster, &lba, &offset))
    return FS_ERROR_NO_SPACE;
  uint8_t sector[SECTOR];
  if (!read_run(lba, sector, 1))
    return FS_ERROR_IO;
  uint8_t *entry = sector + offset;
  memset(entry, 0, 32);
  memcpy(entry, raw, 11);
  entry[11] = 0x20;
  entry[12] = flags;
  put16(entry + 16, 0x0021);
  put16(entry + 18, 0x0021);
  put16(entry + 24, 0x0021);
  if (!write_run(lba, sector, 1))
    return FS_ERROR_IO;
  memset(out, 0, sizeof(*out));
  build_alias(entry, out->alias);
  memcpy(out->name, out->alias, sizeof(out->alias));
  out->attributes = 0x20;
  out->entry_lba = lba;
  out->entry_offset = offset;
  return 0;
}

static int32_t obtain_file(const char *path, struct entry *out) {
  int32_t status = resolve(path, out);
  if (status == FS_ERROR_NOT_FOUND)
    return create_entry(path, out);
  if (status < 0)
    return status;
  if (out->attributes & ATTR_DIRECTORY)
    return FS_ERROR_NOT_FILE;
  return 0;
}

static int32_t fat32_create_file(void *context, const char *path) {
  (void)context;
  struct entry created;
  return create_entry(path, &created);
}

static int32_t fat32_write_file(void *context, const char *path,
                                const void *buffer, uint32_t count) {
  (void)context;
  struct entry e;
  int32_t status = obtain_file(path, &e);
  if (status < 0)
    return status;
  if (file_is_open(e.cluster))
    return FS_ERROR_BUSY;
  status = file_truncate(&e);
  if (status < 0)
    return status;
  if (count) {
    status = file_write_at(&e, 0, buffer, count);
    if (status < 0)
      return status;
  }
  return (int32_t)count;
}

static int32_t fat32_append_file(void *context, const char *path,
                                 const void *buffer, uint32_t count) {
  (void)context;
  struct entry e;
  int32_t status = obtain_file(path, &e);
  if (status < 0)
    return status;
  if (file_is_open(e.cluster))
    return FS_ERROR_BUSY;
  if (!count)
    return 0;
  status = file_write_at(&e, e.size, buffer, count);
  if (status < 0)
    return status;
  return (int32_t)count;
}

static bool fat32_is_ready(void *context) {
  (void)context;
  return volume.ready;
}

const struct vfs_fs_ops fat32_ops = {
    .name = "fat32",
    .open = fat32_open,
    .read = fat32_read,
    .seek = fat32_seek,
    .close = fat32_close,
    .stat = fat32_stat,
    .list = fat32_list,
    .list_long = fat32_list_long,
    .create_file = fat32_create_file,
    .write_file = fat32_write_file,
    .append_file = fat32_append_file,
    .is_ready = fat32_is_ready,
};

uint32_t fat32_abi(void) {
  return FAT32_MODULE_ABI;
}

void *fat32_attach(uint32_t device_index, uint32_t partition_lba) {
  static uint8_t boot[SECTOR];
  volume.ready = false;
  volume.fat_valid = false;
  volume.scan_valid = false;
  memset(handles, 0, sizeof(handles));
  volume.device = device_index;
  volume.partition = partition_lba;
  if (!read_run(partition_lba, boot, 1))
    return 0;
  if (boot[510] != 0x55 || boot[511] != 0xAA)
    return 0;
  uint32_t spc = boot[13];
  uint32_t reserved = le16(boot + 14);
  uint32_t fats = boot[16];
  uint32_t total = le16(boot + 19) ? le16(boot + 19) : le32(boot + 32);
  uint32_t fat_size = le32(boot + 36);
  uint32_t root = le32(boot + 44);
  if (le16(boot + 11) != SECTOR || !spc || (spc & (spc - 1)) || spc > 128)
    return 0;
  if (!reserved || !fats || le16(boot + 17) != 0 || le16(boot + 22) != 0)
    return 0;
  if (!fat_size || !total || root < 2)
    return 0;
  uint64_t overhead = (uint64_t)reserved + (uint64_t)fats * fat_size;
  if (overhead >= total)
    return 0;
  uint32_t clusters = (uint32_t)((total - overhead) / spc);
  if (clusters < 65525)
    return 0;
  volume.sectors_per_cluster = spc;
  volume.fat_lba = partition_lba + reserved;
  volume.data_lba = partition_lba + reserved + fats * fat_size;
  volume.root_cluster = root;
  volume.cluster_count = clusters;
  volume.fat_count = fats;
  volume.fat_size = fat_size;
  volume.total_sectors = total;
  volume.fsinfo_lba = le16(boot + 48) ? partition_lba + le16(boot + 48) : 0;
  volume.alloc_hint = 2;
  volume.fsinfo_touched = false;
  volume.ready = true;
  return &volume;
}
