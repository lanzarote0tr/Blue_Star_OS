#include "FAT.h"
#include "heap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAT_TYPE_12 12U
#define FAT_TYPE_16 16U
#define FAT_TYPE_32 32U

#define FAT_MIN_SECTOR_SIZE 512U
#define FAT_MAX_SECTOR_SIZE 4096U

#define FAT_DIRENT_SIZE 32U
#define FAT_LFN_ENTRY_CHARS 13U
#define FAT_LFN_MAX_CHARS 255U
#define FAT_LFN_MAX_ENTRIES ((FAT_LFN_MAX_CHARS + FAT_LFN_ENTRY_CHARS - 1U) / FAT_LFN_ENTRY_CHARS)
#define FAT_SEGMENT_MAX_UTF8_BYTES (FAT_LFN_MAX_CHARS * 3U + 1U)

#define FAT_ATTR_READ_ONLY 0x01U
#define FAT_ATTR_HIDDEN    0x02U
#define FAT_ATTR_SYSTEM    0x04U
#define FAT_ATTR_VOLUME_ID 0x08U
#define FAT_ATTR_DIRECTORY 0x10U
#define FAT_ATTR_ARCHIVE   0x20U
#define FAT_ATTR_LFN       0x0FU

#define FAT_CLUSTER_FREE   0U

#define FAT12_BAD_CLUSTER 0x0FF7U
#define FAT16_BAD_CLUSTER 0xFFF7U
#define FAT32_BAD_CLUSTER 0x0FFFFFF7U

#define FAT12_EOC_MIN 0x0FF8U
#define FAT16_EOC_MIN 0xFFF8U
#define FAT32_EOC_MIN 0x0FFFFFF8U

#define FAT12_EOC_MARK 0x0FFFU
#define FAT16_EOC_MARK 0xFFFFU
#define FAT32_EOC_MARK 0x0FFFFFFFU

typedef struct {
    uint8_t is_root_fixed;
    uint32_t start_cluster;
} fat_directory_t;

typedef struct {
    uint32_t sector;
    uint16_t offset;
} fat_dirent_location_t;

typedef struct {
    uint8_t found;
    fat_dirent_location_t found_loc;
    uint8_t found_entry[FAT_DIRENT_SIZE];

    uint8_t free_found;
    fat_dirent_location_t free_loc;

    uint8_t reached_end_marker;
    uint32_t last_cluster;
} fat_dir_search_t;

typedef struct {
    uint8_t active;
    uint8_t expected_ord;
    uint8_t max_ord;
    uint8_t checksum;
    uint16_t name[FAT_LFN_MAX_CHARS];
} fat_lfn_state_t;

static uint16_t fat_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t fat_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void fat_store16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void fat_store32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void fat_mem_set(void *dst, uint8_t value, size_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < bytes; i++) {
        d[i] = value;
    }
}

static void fat_mem_copy(void *dst, const void *src, size_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || bytes == 0) {
        return;
    }

    if (d < s) {
        for (size_t i = 0; i < bytes; i++) {
            d[i] = s[i];
        }
        return;
    }

    for (size_t i = bytes; i > 0; i--) {
        d[i - 1] = s[i - 1];
    }
}

static int fat_mem_cmp(const void *a, const void *b, size_t bytes)
{
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;

    for (size_t i = 0; i < bytes; i++) {
        if (p[i] != q[i]) {
            return (p[i] < q[i]) ? -1 : 1;
        }
    }

    return 0;
}

static size_t fat_str_len(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static uint8_t fat_to_upper(uint8_t c)
{
    if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
        return (uint8_t)(c - ((uint8_t)'a' - (uint8_t)'A'));
    }
    return c;
}

static uint16_t fat_to_upper_ascii_u16(uint16_t c)
{
    if (c >= (uint16_t)'a' && c <= (uint16_t)'z') {
        return (uint16_t)(c - ((uint16_t)'a' - (uint16_t)'A'));
    }
    return c;
}

static uint8_t fat_short_name_checksum(const uint8_t short_name[11])
{
    uint8_t sum = 0;

    for (size_t i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + short_name[i]);
    }

    return sum;
}

static bool fat_utf8_decode_one(const char *src, size_t remaining, uint16_t *codepoint_out, size_t *bytes_out)
{
    uint8_t b0;

    if (src == NULL || codepoint_out == NULL || bytes_out == NULL || remaining == 0U) {
        return false;
    }

    b0 = (uint8_t)src[0];
    if (b0 < 0x80U) {
        *codepoint_out = (uint16_t)b0;
        *bytes_out = 1U;
        return true;
    }

    if ((b0 & 0xE0U) == 0xC0U) {
        uint8_t b1;
        uint16_t cp;

        if (remaining < 2U) {
            return false;
        }

        b1 = (uint8_t)src[1];
        if ((b1 & 0xC0U) != 0x80U) {
            return false;
        }

        cp = (uint16_t)(((uint16_t)(b0 & 0x1FU) << 6) | (uint16_t)(b1 & 0x3FU));
        if (cp < 0x80U) {
            return false;
        }

        *codepoint_out = cp;
        *bytes_out = 2U;
        return true;
    }

    if ((b0 & 0xF0U) == 0xE0U) {
        uint8_t b1;
        uint8_t b2;
        uint16_t cp;

        if (remaining < 3U) {
            return false;
        }

        b1 = (uint8_t)src[1];
        b2 = (uint8_t)src[2];
        if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
            return false;
        }

        cp = (uint16_t)(((uint16_t)(b0 & 0x0FU) << 12) |
                        ((uint16_t)(b1 & 0x3FU) << 6) |
                        (uint16_t)(b2 & 0x3FU));
        if (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU)) {
            return false;
        }

        *codepoint_out = cp;
        *bytes_out = 3U;
        return true;
    }

    return false;
}

static bool fat_utf8_to_utf16(const char *utf8, uint16_t *utf16_out, size_t max_units, size_t *units_out)
{
    size_t pos = 0;
    size_t out = 0;
    size_t len;

    if (utf8 == NULL || utf16_out == NULL || units_out == NULL || max_units == 0U) {
        return false;
    }

    len = fat_str_len(utf8);
    while (pos < len) {
        uint16_t cp;
        size_t consumed;

        if (out >= max_units) {
            return false;
        }

        if (!fat_utf8_decode_one(utf8 + pos, len - pos, &cp, &consumed)) {
            return false;
        }

        utf16_out[out++] = cp;
        pos += consumed;
    }

    *units_out = out;
    return true;
}

static uint32_t fat_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static size_t fat_min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static bool fat_is_power_of_two_u32(uint32_t x)
{
    return x != 0U && (x & (x - 1U)) == 0U;
}

static bool fat_is_valid_short_name_char(uint8_t c)
{
    if ((c >= (uint8_t)'A' && c <= (uint8_t)'Z') ||
        (c >= (uint8_t)'0' && c <= (uint8_t)'9')) {
        return true;
    }

    switch (c) {
    case (uint8_t)'$':
    case (uint8_t)'%':
    case (uint8_t)'\'':
    case (uint8_t)'-':
    case (uint8_t)'_':
    case (uint8_t)'@':
    case (uint8_t)'~':
    case (uint8_t)'`':
    case (uint8_t)'!':
    case (uint8_t)'(':
    case (uint8_t)')':
    case (uint8_t)'{':
    case (uint8_t)'}':
    case (uint8_t)'^':
    case (uint8_t)'#':
    case (uint8_t)'&':
        return true;
    default:
        return false;
    }
}

static bool fat_make_short_name_83(const char *segment, uint8_t out_name[11])
{
    size_t base_len = 0;
    size_t ext_len = 0;
    bool seen_dot = false;

    if (segment == NULL || segment[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < 11; i++) {
        out_name[i] = (uint8_t)' ';
    }

    for (size_t i = 0; segment[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)segment[i];

        if (ch == (uint8_t)'.') {
            if (seen_dot || base_len == 0) {
                return false;
            }
            seen_dot = true;
            continue;
        }

        if (ch == (uint8_t)' ' || ch == (uint8_t)'/' || ch == (uint8_t)'\\') {
            return false;
        }

        ch = fat_to_upper(ch);
        if (!fat_is_valid_short_name_char(ch)) {
            return false;
        }

        if (!seen_dot) {
            if (base_len >= 8) {
                return false;
            }
            out_name[base_len++] = ch;
            continue;
        }

        if (ext_len >= 3) {
            return false;
        }
        out_name[8 + ext_len] = ch;
        ext_len++;
    }

    if (base_len == 0) {
        return false;
    }

    if (seen_dot && ext_len == 0) {
        return false;
    }

    return true;
}

static bool fat_segment_has_lowercase_ascii(const char *segment)
{
    for (size_t i = 0; segment[i] != '\0'; i++) {
        uint8_t c = (uint8_t)segment[i];
        if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
            return true;
        }
    }
    return false;
}

static bool fat_is_valid_lfn_char(uint16_t c)
{
    if (c < 0x0020U || c == 0x007FU) {
        return false;
    }

    switch (c) {
    case (uint16_t)'\"':
    case (uint16_t)'*':
    case (uint16_t)'/':
    case (uint16_t)':':
    case (uint16_t)'<':
    case (uint16_t)'>':
    case (uint16_t)'?':
    case (uint16_t)'\\':
    case (uint16_t)'|':
        return false;
    default:
        return true;
    }
}

static fat_status_t fat_segment_to_utf16_name(const char *segment, uint16_t out_name[FAT_LFN_MAX_CHARS], size_t *out_len)
{
    size_t len;

    if (segment == NULL || out_name == NULL || out_len == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fat_utf8_to_utf16(segment, out_name, FAT_LFN_MAX_CHARS, &len)) {
        return FAT_ERR_NOT_SUPPORTED;
    }

    if (len == 0U || len > FAT_LFN_MAX_CHARS) {
        return FAT_ERR_NOT_SUPPORTED;
    }

    for (size_t i = 0; i < len; i++) {
        if (!fat_is_valid_lfn_char(out_name[i])) {
            return FAT_ERR_NOT_SUPPORTED;
        }
    }

    if (out_name[len - 1] == (uint16_t)' ' || out_name[len - 1] == (uint16_t)'.') {
        return FAT_ERR_NOT_SUPPORTED;
    }

    *out_len = len;
    return FAT_OK;
}

static void fat_lfn_state_reset(fat_lfn_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->active = 0U;
    state->expected_ord = 0U;
    state->max_ord = 0U;
    state->checksum = 0U;
}

static void fat_lfn_entry_extract_units(const uint8_t *entry, uint16_t units[FAT_LFN_ENTRY_CHARS])
{
    static const uint8_t kOffsets[FAT_LFN_ENTRY_CHARS] = {
        1U, 3U, 5U, 7U, 9U,
        14U, 16U, 18U, 20U, 22U, 24U,
        28U, 30U
    };

    for (size_t i = 0; i < FAT_LFN_ENTRY_CHARS; i++) {
        units[i] = fat_le16(entry + kOffsets[i]);
    }
}

static void fat_lfn_state_consume_entry(fat_lfn_state_t *state, const uint8_t *entry)
{
    uint8_t ord_raw;
    uint8_t ord;
    uint8_t is_last;
    uint16_t units[FAT_LFN_ENTRY_CHARS];
    size_t base_index;

    if (state == NULL || entry == NULL) {
        return;
    }

    ord_raw = entry[0];
    ord = (uint8_t)(ord_raw & 0x1FU);
    is_last = (uint8_t)(ord_raw & 0x40U);

    if (ord == 0U || ord > FAT_LFN_MAX_ENTRIES || entry[12] != 0U) {
        fat_lfn_state_reset(state);
        return;
    }

    if (is_last) {
        fat_mem_set(state->name, 0xFFU, sizeof(state->name));
        state->active = 1U;
        state->expected_ord = ord;
        state->max_ord = ord;
        state->checksum = entry[13];
    } else {
        if (!state->active || state->expected_ord == 0U || ord + 1U != state->expected_ord || state->checksum != entry[13]) {
            fat_lfn_state_reset(state);
            return;
        }
        state->expected_ord = ord;
    }

    fat_lfn_entry_extract_units(entry, units);
    base_index = (size_t)(ord - 1U) * FAT_LFN_ENTRY_CHARS;
    for (size_t i = 0; i < FAT_LFN_ENTRY_CHARS; i++) {
        size_t idx = base_index + i;
        if (idx < FAT_LFN_MAX_CHARS) {
            state->name[idx] = units[i];
        }
    }
}

static bool fat_lfn_state_get_name(
    const fat_lfn_state_t *state,
    const uint8_t short_name[11],
    const uint16_t **name_out,
    size_t *len_out)
{
    size_t max_units;
    size_t len = 0;

    if (state == NULL || short_name == NULL || name_out == NULL || len_out == NULL) {
        return false;
    }

    if (!state->active || state->expected_ord != 1U || state->max_ord == 0U) {
        return false;
    }

    if (state->checksum != fat_short_name_checksum(short_name)) {
        return false;
    }

    max_units = (size_t)state->max_ord * FAT_LFN_ENTRY_CHARS;
    if (max_units > FAT_LFN_MAX_CHARS) {
        max_units = FAT_LFN_MAX_CHARS;
    }

    for (; len < max_units; len++) {
        uint16_t c = state->name[len];
        if (c == 0x0000U || c == 0xFFFFU) {
            break;
        }
    }

    if (len == 0U) {
        return false;
    }

    *name_out = state->name;
    *len_out = len;
    return true;
}

static bool fat_lfn_name_matches_segment(const uint16_t *lfn_name, size_t lfn_len, const char *segment)
{
    uint16_t segment_name[FAT_LFN_MAX_CHARS];
    size_t segment_len;

    if (lfn_name == NULL || segment == NULL) {
        return false;
    }

    if (!fat_utf8_to_utf16(segment, segment_name, FAT_LFN_MAX_CHARS, &segment_len)) {
        return false;
    }

    if (segment_len != lfn_len) {
        return false;
    }

    for (size_t i = 0; i < lfn_len; i++) {
        if (fat_to_upper_ascii_u16(lfn_name[i]) != fat_to_upper_ascii_u16(segment_name[i])) {
            return false;
        }
    }

    return true;
}

static bool fat_path_has_trailing_slash(const char *path)
{
    size_t len;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    len = fat_str_len(path);
    if (len == 0) {
        return false;
    }

    while (len > 0 && path[len - 1] == '/') {
        len--;
    }

    return len != fat_str_len(path);
}

static bool fat_path_is_root_only(const char *path)
{
    size_t i = 0;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    while (path[i] == '/') {
        i++;
    }

    return path[i] == '\0';
}

static fat_status_t fat_path_next_segment(
    const char *path,
    size_t *cursor,
    char *segment,
    size_t segment_capacity,
    uint8_t *is_last)
{
    size_t i;
    size_t len = 0;

    if (path == NULL || cursor == NULL || segment == NULL || is_last == NULL || segment_capacity < 2) {
        return FAT_ERR_INVALID_ARG;
    }

    i = *cursor;
    while (path[i] == '/') {
        i++;
    }

    if (path[i] == '\0') {
        return FAT_ERR_NOT_FOUND;
    }

    while (path[i] != '\0' && path[i] != '/') {
        if (len + 1 >= segment_capacity) {
            return FAT_ERR_NOT_SUPPORTED;
        }
        segment[len++] = path[i++];
    }
    segment[len] = '\0';

    while (path[i] == '/') {
        i++;
    }

    *cursor = i;
    *is_last = (path[i] == '\0') ? 1U : 0U;
    return FAT_OK;
}

static uint32_t fat_cluster_size_bytes(const fat_fs_t *fs)
{
    return (uint32_t)fs->bytes_per_sector * (uint32_t)fs->sectors_per_cluster;
}

static uint32_t fat_cluster_limit(const fat_fs_t *fs)
{
    return fs->cluster_count + 2U;
}

static bool fat_cluster_is_data(const fat_fs_t *fs, uint32_t cluster)
{
    return cluster >= 2U && cluster < fat_cluster_limit(fs);
}

static uint32_t fat_eoc_mark_value(const fat_fs_t *fs)
{
    if (fs->fat_type == FAT_TYPE_12) {
        return FAT12_EOC_MARK;
    }
    if (fs->fat_type == FAT_TYPE_16) {
        return FAT16_EOC_MARK;
    }
    return FAT32_EOC_MARK;
}

static bool fat_value_is_eoc(const fat_fs_t *fs, uint32_t value)
{
    if (fs->fat_type == FAT_TYPE_12) {
        return (value & 0x0FFFU) >= FAT12_EOC_MIN;
    }
    if (fs->fat_type == FAT_TYPE_16) {
        return (value & 0xFFFFU) >= FAT16_EOC_MIN;
    }
    return (value & 0x0FFFFFFFU) >= FAT32_EOC_MIN;
}

static bool fat_value_is_bad_cluster(const fat_fs_t *fs, uint32_t value)
{
    if (fs->fat_type == FAT_TYPE_12) {
        return (value & 0x0FFFU) == FAT12_BAD_CLUSTER;
    }
    if (fs->fat_type == FAT_TYPE_16) {
        return (value & 0xFFFFU) == FAT16_BAD_CLUSTER;
    }
    return (value & 0x0FFFFFFFU) == FAT32_BAD_CLUSTER;
}

static uint32_t fat_cluster_to_sector(const fat_fs_t *fs, uint32_t cluster)
{
    return fs->first_data_sector + ((cluster - 2U) * (uint32_t)fs->sectors_per_cluster);
}

static bool fat_sector_to_cluster(const fat_fs_t *fs, uint32_t sector, uint32_t *cluster_out)
{
    uint32_t rel;
    uint32_t cluster;

    if (fs == NULL || cluster_out == NULL) {
        return false;
    }

    if (sector < fs->first_data_sector || fs->sectors_per_cluster == 0U) {
        return false;
    }

    rel = sector - fs->first_data_sector;
    cluster = (rel / (uint32_t)fs->sectors_per_cluster) + 2U;
    if (!fat_cluster_is_data(fs, cluster)) {
        return false;
    }

    *cluster_out = cluster;
    return true;
}

static void fat_reset_file(fat_file_t *file)
{
    fat_mem_set(file, 0, sizeof(*file));
}

static fat_status_t fat_disk_read(fat_fs_t *fs, uint32_t sector, uint32_t count, void *buffer)
{
    uint64_t lba;

    if (fs == NULL || buffer == NULL || count == 0U) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->device.read_blocks == NULL) {
        return FAT_ERR_IO;
    }

    lba = fs->partition_lba + (uint64_t)sector;
    if (!fs->device.read_blocks(fs->device.user, lba, count, buffer)) {
        return FAT_ERR_IO;
    }

    return FAT_OK;
}

static fat_status_t fat_disk_write(fat_fs_t *fs, uint32_t sector, uint32_t count, const void *buffer)
{
    uint64_t lba;

    if (fs == NULL || buffer == NULL || count == 0U) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->device.write_blocks == NULL) {
        return FAT_ERR_ACCESS;
    }

    lba = fs->partition_lba + (uint64_t)sector;
    if (!fs->device.write_blocks(fs->device.user, lba, count, buffer)) {
        return FAT_ERR_IO;
    }

    return FAT_OK;
}

static fat_status_t fat_cache_flush_internal(fat_fs_t *fs)
{
    fat_status_t status;

    if (fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->cache_valid || !fs->cache_dirty) {
        return FAT_OK;
    }

    status = fat_disk_write(fs, fs->cache_sector, 1U, fs->sector_cache);
    if (status != FAT_OK) {
        return status;
    }

    fs->cache_dirty = 0;
    return FAT_OK;
}

static fat_status_t fat_cache_load_sector(fat_fs_t *fs, uint32_t sector)
{
    fat_status_t status;

    if (fs == NULL || fs->sector_cache == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->cache_valid && fs->cache_sector == sector) {
        return FAT_OK;
    }

    status = fat_cache_flush_internal(fs);
    if (status != FAT_OK) {
        return status;
    }

    status = fat_disk_read(fs, sector, 1U, fs->sector_cache);
    if (status != FAT_OK) {
        fs->cache_valid = 0;
        return status;
    }

    fs->cache_valid = 1;
    fs->cache_dirty = 0;
    fs->cache_sector = sector;
    return FAT_OK;
}

static fat_status_t fat_read_fat_bytes(
    fat_fs_t *fs,
    uint32_t fat_byte_offset,
    uint8_t *dst,
    uint32_t byte_count)
{
    fat_status_t status;
    uint64_t fat_total_bytes;

    if (fs == NULL || dst == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    fat_total_bytes = (uint64_t)fs->sectors_per_fat * (uint64_t)fs->bytes_per_sector;
    if ((uint64_t)fat_byte_offset + (uint64_t)byte_count > fat_total_bytes) {
        return FAT_ERR_CORRUPTED;
    }

    while (byte_count > 0U) {
        uint32_t sector_in_fat = fat_byte_offset / (uint32_t)fs->bytes_per_sector;
        uint32_t sector = fs->first_fat_sector + sector_in_fat;
        uint32_t offset = fat_byte_offset % (uint32_t)fs->bytes_per_sector;
        uint32_t chunk = fat_min_u32(byte_count, (uint32_t)fs->bytes_per_sector - offset);

        status = fat_cache_load_sector(fs, sector);
        if (status != FAT_OK) {
            return status;
        }

        fat_mem_copy(dst, fs->sector_cache + offset, chunk);

        dst += chunk;
        fat_byte_offset += chunk;
        byte_count -= chunk;
    }

    return FAT_OK;
}

static fat_status_t fat_write_fat_bytes(
    fat_fs_t *fs,
    uint32_t fat_byte_offset,
    const uint8_t *src,
    uint32_t byte_count)
{
    fat_status_t status;
    uint64_t fat_total_bytes;

    if (fs == NULL || src == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    fat_total_bytes = (uint64_t)fs->sectors_per_fat * (uint64_t)fs->bytes_per_sector;
    if ((uint64_t)fat_byte_offset + (uint64_t)byte_count > fat_total_bytes) {
        return FAT_ERR_CORRUPTED;
    }

    for (uint32_t fat_index = 0; fat_index < (uint32_t)fs->fat_count; fat_index++) {
        uint32_t offset = fat_byte_offset;
        uint32_t remaining = byte_count;
        const uint8_t *p = src;

        while (remaining > 0U) {
            uint32_t sector_in_fat = offset / (uint32_t)fs->bytes_per_sector;
            uint32_t sector = fs->first_fat_sector + (fat_index * fs->sectors_per_fat) + sector_in_fat;
            uint32_t sector_offset = offset % (uint32_t)fs->bytes_per_sector;
            uint32_t chunk = fat_min_u32(remaining, (uint32_t)fs->bytes_per_sector - sector_offset);

            status = fat_cache_load_sector(fs, sector);
            if (status != FAT_OK) {
                return status;
            }

            fat_mem_copy(fs->sector_cache + sector_offset, p, chunk);
            fs->cache_dirty = 1;

            status = fat_cache_flush_internal(fs);
            if (status != FAT_OK) {
                return status;
            }

            p += chunk;
            offset += chunk;
            remaining -= chunk;
        }
    }

    return FAT_OK;
}

static fat_status_t fat_get_fat_entry(fat_fs_t *fs, uint32_t cluster, uint32_t *value_out)
{
    fat_status_t status;

    if (fs == NULL || value_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fat_cluster_is_data(fs, cluster) && cluster != 0U && cluster != 1U) {
        return FAT_ERR_CORRUPTED;
    }

    if (fs->fat_type == FAT_TYPE_12) {
        uint8_t pair[2];
        uint32_t offset = cluster + (cluster / 2U);
        uint16_t v;

        status = fat_read_fat_bytes(fs, offset, pair, 2U);
        if (status != FAT_OK) {
            return status;
        }

        v = fat_le16(pair);
        if ((cluster & 1U) != 0U) {
            v = (uint16_t)(v >> 4);
        } else {
            v &= 0x0FFFU;
        }
        *value_out = (uint32_t)v;
        return FAT_OK;
    }

    if (fs->fat_type == FAT_TYPE_16) {
        uint8_t data[2];
        uint32_t offset = cluster * 2U;

        status = fat_read_fat_bytes(fs, offset, data, 2U);
        if (status != FAT_OK) {
            return status;
        }

        *value_out = (uint32_t)fat_le16(data);
        return FAT_OK;
    }

    if (fs->fat_type == FAT_TYPE_32) {
        uint8_t data[4];
        uint32_t offset = cluster * 4U;

        status = fat_read_fat_bytes(fs, offset, data, 4U);
        if (status != FAT_OK) {
            return status;
        }

        *value_out = fat_le32(data) & 0x0FFFFFFFU;
        return FAT_OK;
    }

    return FAT_ERR_BAD_FS;
}

static fat_status_t fat_set_fat_entry(fat_fs_t *fs, uint32_t cluster, uint32_t value)
{
    fat_status_t status;

    if (fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    if (!fat_cluster_is_data(fs, cluster) && cluster != 0U && cluster != 1U) {
        return FAT_ERR_CORRUPTED;
    }

    if (fs->fat_type == FAT_TYPE_12) {
        uint8_t pair[2];
        uint32_t offset = cluster + (cluster / 2U);
        uint16_t v;

        value &= 0x0FFFU;

        status = fat_read_fat_bytes(fs, offset, pair, 2U);
        if (status != FAT_OK) {
            return status;
        }

        v = fat_le16(pair);
        if ((cluster & 1U) != 0U) {
            v = (uint16_t)((v & 0x000FU) | ((value & 0x0FFFU) << 4));
        } else {
            v = (uint16_t)((v & 0xF000U) | (value & 0x0FFFU));
        }

        fat_store16(pair, v);
        return fat_write_fat_bytes(fs, offset, pair, 2U);
    }

    if (fs->fat_type == FAT_TYPE_16) {
        uint8_t data[2];
        uint32_t offset = cluster * 2U;

        value &= 0xFFFFU;
        fat_store16(data, (uint16_t)value);
        return fat_write_fat_bytes(fs, offset, data, 2U);
    }

    if (fs->fat_type == FAT_TYPE_32) {
        uint8_t data[4];
        uint32_t offset = cluster * 4U;
        uint32_t current;

        status = fat_read_fat_bytes(fs, offset, data, 4U);
        if (status != FAT_OK) {
            return status;
        }

        current = fat_le32(data);
        current = (current & 0xF0000000U) | (value & 0x0FFFFFFFU);
        fat_store32(data, current);
        return fat_write_fat_bytes(fs, offset, data, 4U);
    }

    return FAT_ERR_BAD_FS;
}

static fat_status_t fat_get_next_cluster(fat_fs_t *fs, uint32_t cluster, uint32_t *next_out)
{
    fat_status_t status;
    uint32_t entry;

    if (fs == NULL || next_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fat_cluster_is_data(fs, cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    status = fat_get_fat_entry(fs, cluster, &entry);
    if (status != FAT_OK) {
        return status;
    }

    if (fat_value_is_eoc(fs, entry)) {
        *next_out = 0U;
        return FAT_OK;
    }

    if (entry == FAT_CLUSTER_FREE || fat_value_is_bad_cluster(fs, entry) || !fat_cluster_is_data(fs, entry)) {
        return FAT_ERR_CORRUPTED;
    }

    *next_out = entry;
    return FAT_OK;
}

static fat_status_t fat_zero_cluster(fat_fs_t *fs, uint32_t cluster)
{
    fat_status_t status;
    uint32_t first_sector;

    if (fs == NULL || !fat_cluster_is_data(fs, cluster)) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    first_sector = fat_cluster_to_sector(fs, cluster);
    for (uint32_t i = 0; i < (uint32_t)fs->sectors_per_cluster; i++) {
        status = fat_cache_load_sector(fs, first_sector + i);
        if (status != FAT_OK) {
            return status;
        }

        fat_mem_set(fs->sector_cache, 0, fs->bytes_per_sector);
        fs->cache_dirty = 1;

        status = fat_cache_flush_internal(fs);
        if (status != FAT_OK) {
            return status;
        }
    }

    return FAT_OK;
}

static fat_status_t fat_allocate_cluster(fat_fs_t *fs, uint32_t *cluster_out)
{
    fat_status_t status;
    uint32_t start;
    uint32_t limit;

    if (fs == NULL || cluster_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    limit = fat_cluster_limit(fs);
    if (limit <= 2U) {
        return FAT_ERR_NO_SPACE;
    }

    start = fs->next_alloc_hint;
    if (start < 2U || start >= limit) {
        start = 2U;
    }

    for (uint32_t pass = 0; pass < 2U; pass++) {
        uint32_t begin = (pass == 0U) ? start : 2U;
        uint32_t end = (pass == 0U) ? limit : start;

        for (uint32_t candidate = begin; candidate < end; candidate++) {
            uint32_t entry = 0;

            status = fat_get_fat_entry(fs, candidate, &entry);
            if (status != FAT_OK) {
                return status;
            }

            if (entry != FAT_CLUSTER_FREE) {
                continue;
            }

            status = fat_set_fat_entry(fs, candidate, fat_eoc_mark_value(fs));
            if (status != FAT_OK) {
                return status;
            }

            status = fat_zero_cluster(fs, candidate);
            if (status != FAT_OK) {
                (void)fat_set_fat_entry(fs, candidate, FAT_CLUSTER_FREE);
                return status;
            }

            fs->next_alloc_hint = candidate + 1U;
            if (fs->next_alloc_hint >= limit) {
                fs->next_alloc_hint = 2U;
            }

            *cluster_out = candidate;
            return FAT_OK;
        }
    }

    return FAT_ERR_NO_SPACE;
}

static fat_status_t fat_free_cluster_chain(fat_fs_t *fs, uint32_t first_cluster)
{
    fat_status_t status;
    uint32_t cluster;

    if (fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (first_cluster == 0U) {
        return FAT_OK;
    }

    if (!fat_cluster_is_data(fs, first_cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    cluster = first_cluster;
    for (uint32_t guard = 0; guard < fs->cluster_count; guard++) {
        uint32_t entry = 0;
        uint32_t next = 0;

        status = fat_get_fat_entry(fs, cluster, &entry);
        if (status != FAT_OK) {
            return status;
        }

        if (!fat_value_is_eoc(fs, entry)) {
            if (entry == FAT_CLUSTER_FREE || fat_value_is_bad_cluster(fs, entry) || !fat_cluster_is_data(fs, entry)) {
                return FAT_ERR_CORRUPTED;
            }
            next = entry;
        }

        status = fat_set_fat_entry(fs, cluster, FAT_CLUSTER_FREE);
        if (status != FAT_OK) {
            return status;
        }

        if (next == 0U) {
            return FAT_OK;
        }

        cluster = next;
    }

    return FAT_ERR_CORRUPTED;
}

static bool fat_directory_next_slot_root(
    const fat_fs_t *fs,
    const fat_dirent_location_t *current,
    fat_dirent_location_t *next_out)
{
    fat_dirent_location_t next;
    uint32_t root_end_sector;

    if (fs == NULL || current == NULL || next_out == NULL) {
        return false;
    }

    root_end_sector = fs->root_dir_sector + fs->root_dir_sector_count;
    if (current->sector < fs->root_dir_sector || current->sector >= root_end_sector) {
        return false;
    }

    next = *current;
    next.offset = (uint16_t)(next.offset + FAT_DIRENT_SIZE);
    if ((uint32_t)next.offset >= (uint32_t)fs->bytes_per_sector) {
        next.offset = 0U;
        next.sector++;
    }

    if (next.sector >= root_end_sector) {
        return false;
    }

    *next_out = next;
    return true;
}

static fat_status_t fat_directory_append_cluster(fat_fs_t *fs, uint32_t last_cluster, uint32_t *new_cluster_out)
{
    fat_status_t status;
    uint32_t new_cluster;

    if (fs == NULL || new_cluster_out == NULL || !fat_cluster_is_data(fs, last_cluster)) {
        return FAT_ERR_INVALID_ARG;
    }

    status = fat_allocate_cluster(fs, &new_cluster);
    if (status != FAT_OK) {
        return status;
    }

    status = fat_set_fat_entry(fs, last_cluster, new_cluster);
    if (status != FAT_OK) {
        (void)fat_set_fat_entry(fs, new_cluster, FAT_CLUSTER_FREE);
        return status;
    }

    *new_cluster_out = new_cluster;
    return FAT_OK;
}

static fat_status_t fat_directory_next_slot_cluster_existing(
    fat_fs_t *fs,
    const fat_dirent_location_t *current,
    uint32_t current_cluster,
    fat_dirent_location_t *next_out,
    uint32_t *next_cluster_out,
    uint8_t *has_next_out)
{
    fat_dirent_location_t next;
    uint32_t first_sector;

    if (fs == NULL || current == NULL || next_out == NULL || next_cluster_out == NULL || has_next_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fat_cluster_is_data(fs, current_cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    first_sector = fat_cluster_to_sector(fs, current_cluster);
    if (current->sector < first_sector || current->sector >= first_sector + (uint32_t)fs->sectors_per_cluster) {
        return FAT_ERR_CORRUPTED;
    }

    next = *current;
    next.offset = (uint16_t)(next.offset + FAT_DIRENT_SIZE);
    if ((uint32_t)next.offset < (uint32_t)fs->bytes_per_sector) {
        *has_next_out = 1U;
        *next_out = next;
        *next_cluster_out = current_cluster;
        return FAT_OK;
    }

    next.offset = 0U;
    next.sector++;
    if (next.sector < first_sector + (uint32_t)fs->sectors_per_cluster) {
        *has_next_out = 1U;
        *next_out = next;
        *next_cluster_out = current_cluster;
        return FAT_OK;
    }

    {
        uint32_t next_cluster = 0;
        fat_status_t status = fat_get_next_cluster(fs, current_cluster, &next_cluster);
        if (status != FAT_OK) {
            return status;
        }

        if (next_cluster == 0U) {
            *has_next_out = 0U;
            return FAT_OK;
        }

        *has_next_out = 1U;
        *next_cluster_out = next_cluster;
        next.sector = fat_cluster_to_sector(fs, next_cluster);
        next.offset = 0U;
        *next_out = next;
        return FAT_OK;
    }
}

static fat_status_t fat_directory_advance_slot_cluster(
    fat_fs_t *fs,
    fat_dirent_location_t *loc,
    uint32_t *cluster_io,
    uint8_t allow_expand)
{
    fat_dirent_location_t next;
    uint32_t next_cluster;
    uint8_t has_next = 0U;
    fat_status_t status;

    if (fs == NULL || loc == NULL || cluster_io == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    status = fat_directory_next_slot_cluster_existing(fs, loc, *cluster_io, &next, &next_cluster, &has_next);
    if (status != FAT_OK) {
        return status;
    }

    if (has_next) {
        *loc = next;
        *cluster_io = next_cluster;
        return FAT_OK;
    }

    if (!allow_expand) {
        return FAT_ERR_NOT_FOUND;
    }

    status = fat_directory_append_cluster(fs, *cluster_io, &next_cluster);
    if (status != FAT_OK) {
        return status;
    }

    *cluster_io = next_cluster;
    loc->sector = fat_cluster_to_sector(fs, next_cluster);
    loc->offset = 0U;
    return FAT_OK;
}

static uint32_t fat_dirent_first_cluster(const uint8_t *entry)
{
    uint32_t high = (uint32_t)fat_le16(entry + 20);
    uint32_t low = (uint32_t)fat_le16(entry + 26);
    return (high << 16) | low;
}

static void fat_dirent_set_first_cluster(uint8_t *entry, uint32_t cluster)
{
    fat_store16(entry + 20, (uint16_t)((cluster >> 16) & 0xFFFFU));
    fat_store16(entry + 26, (uint16_t)(cluster & 0xFFFFU));
}

static uint32_t fat_dirent_file_size(const uint8_t *entry)
{
    return fat_le32(entry + 28);
}

static bool fat_dirent_is_lfn(const uint8_t *entry)
{
    return (entry[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN;
}

static bool fat_dirent_is_end(const uint8_t *entry)
{
    return entry[0] == 0x00U;
}

static bool fat_dirent_is_deleted(const uint8_t *entry)
{
    return entry[0] == 0xE5U;
}

static void fat_get_root_directory(const fat_fs_t *fs, fat_directory_t *dir)
{
    if (fs->fat_type == FAT_TYPE_32) {
        dir->is_root_fixed = 0U;
        dir->start_cluster = fs->root_cluster;
    } else {
        dir->is_root_fixed = 1U;
        dir->start_cluster = 0U;
    }
}

static fat_status_t fat_read_dirent(
    fat_fs_t *fs,
    const fat_dirent_location_t *loc,
    uint8_t *entry_out)
{
    fat_status_t status;

    status = fat_cache_load_sector(fs, loc->sector);
    if (status != FAT_OK) {
        return status;
    }

    if ((uint32_t)loc->offset + FAT_DIRENT_SIZE > (uint32_t)fs->bytes_per_sector) {
        return FAT_ERR_CORRUPTED;
    }

    fat_mem_copy(entry_out, fs->sector_cache + loc->offset, FAT_DIRENT_SIZE);
    return FAT_OK;
}

static fat_status_t fat_write_dirent(
    fat_fs_t *fs,
    const fat_dirent_location_t *loc,
    const uint8_t *entry)
{
    fat_status_t status;

    if (fs == NULL || loc == NULL || entry == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    status = fat_cache_load_sector(fs, loc->sector);
    if (status != FAT_OK) {
        return status;
    }

    if ((uint32_t)loc->offset + FAT_DIRENT_SIZE > (uint32_t)fs->bytes_per_sector) {
        return FAT_ERR_CORRUPTED;
    }

    fat_mem_copy(fs->sector_cache + loc->offset, entry, FAT_DIRENT_SIZE);
    fs->cache_dirty = 1;
    return fat_cache_flush_internal(fs);
}

static fat_status_t fat_directory_search(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    const uint8_t name11[11],
    fat_dir_search_t *result)
{
    fat_status_t status;

    if (fs == NULL || dir == NULL || result == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    fat_mem_set(result, 0, sizeof(*result));

    if (dir->is_root_fixed) {
        uint32_t start_sector = fs->root_dir_sector;
        uint32_t sector_count = fs->root_dir_sector_count;

        for (uint32_t s = 0; s < sector_count; s++) {
            uint32_t sector = start_sector + s;

            status = fat_cache_load_sector(fs, sector);
            if (status != FAT_OK) {
                return status;
            }

            for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                const uint8_t *entry = fs->sector_cache + offset;

                if (fat_dirent_is_end(entry)) {
                    if (!result->free_found) {
                        result->free_found = 1U;
                        result->free_loc.sector = sector;
                        result->free_loc.offset = (uint16_t)offset;
                    }
                    result->reached_end_marker = 1U;
                    return FAT_ERR_NOT_FOUND;
                }

                if (fat_dirent_is_deleted(entry)) {
                    if (!result->free_found) {
                        result->free_found = 1U;
                        result->free_loc.sector = sector;
                        result->free_loc.offset = (uint16_t)offset;
                    }
                    continue;
                }

                if (fat_dirent_is_lfn(entry) || (entry[11] & FAT_ATTR_VOLUME_ID) != 0U) {
                    continue;
                }

                if (name11 != NULL && fat_mem_cmp(entry, name11, 11U) == 0) {
                    result->found = 1U;
                    result->found_loc.sector = sector;
                    result->found_loc.offset = (uint16_t)offset;
                    fat_mem_copy(result->found_entry, entry, FAT_DIRENT_SIZE);
                    return FAT_OK;
                }
            }
        }

        return FAT_ERR_NOT_FOUND;
    }

    if (!fat_cluster_is_data(fs, dir->start_cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    {
        uint32_t cluster = dir->start_cluster;

        for (uint32_t guard = 0; guard < fs->cluster_count; guard++) {
            uint32_t first_sector = fat_cluster_to_sector(fs, cluster);
            result->last_cluster = cluster;

            for (uint32_t s = 0; s < (uint32_t)fs->sectors_per_cluster; s++) {
                uint32_t sector = first_sector + s;

                status = fat_cache_load_sector(fs, sector);
                if (status != FAT_OK) {
                    return status;
                }

                for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                    const uint8_t *entry = fs->sector_cache + offset;

                    if (fat_dirent_is_end(entry)) {
                        if (!result->free_found) {
                            result->free_found = 1U;
                            result->free_loc.sector = sector;
                            result->free_loc.offset = (uint16_t)offset;
                        }
                        result->reached_end_marker = 1U;
                        return FAT_ERR_NOT_FOUND;
                    }

                    if (fat_dirent_is_deleted(entry)) {
                        if (!result->free_found) {
                            result->free_found = 1U;
                            result->free_loc.sector = sector;
                            result->free_loc.offset = (uint16_t)offset;
                        }
                        continue;
                    }

                    if (fat_dirent_is_lfn(entry) || (entry[11] & FAT_ATTR_VOLUME_ID) != 0U) {
                        continue;
                    }

                    if (name11 != NULL && fat_mem_cmp(entry, name11, 11U) == 0) {
                        result->found = 1U;
                        result->found_loc.sector = sector;
                        result->found_loc.offset = (uint16_t)offset;
                        fat_mem_copy(result->found_entry, entry, FAT_DIRENT_SIZE);
                        return FAT_OK;
                    }
                }
            }

            {
                uint32_t next = 0;
                status = fat_get_next_cluster(fs, cluster, &next);
                if (status != FAT_OK) {
                    return status;
                }

                if (next == 0U) {
                    return FAT_ERR_NOT_FOUND;
                }

                cluster = next;
            }
        }
    }

    return FAT_ERR_CORRUPTED;
}

static bool fat_directory_entry_matches_segment(
    const uint8_t *entry,
    const fat_lfn_state_t *lfn_state,
    const char *segment)
{
    uint8_t short_name[11];

    if (entry == NULL || segment == NULL) {
        return false;
    }

    if (fat_make_short_name_83(segment, short_name) && fat_mem_cmp(entry, short_name, 11U) == 0) {
        return true;
    }

    if (lfn_state != NULL) {
        const uint16_t *lfn_name = NULL;
        size_t lfn_len = 0;

        if (fat_lfn_state_get_name(lfn_state, entry, &lfn_name, &lfn_len) &&
            fat_lfn_name_matches_segment(lfn_name, lfn_len, segment)) {
            return true;
        }
    }

    return false;
}

static fat_status_t fat_directory_search_name(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    const char *segment,
    fat_dir_search_t *result)
{
    fat_status_t status;
    fat_lfn_state_t lfn_state;

    if (fs == NULL || dir == NULL || segment == NULL || result == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    fat_mem_set(result, 0, sizeof(*result));
    fat_lfn_state_reset(&lfn_state);

    if (dir->is_root_fixed) {
        uint32_t start_sector = fs->root_dir_sector;
        uint32_t sector_count = fs->root_dir_sector_count;

        for (uint32_t s = 0; s < sector_count; s++) {
            uint32_t sector = start_sector + s;

            status = fat_cache_load_sector(fs, sector);
            if (status != FAT_OK) {
                return status;
            }

            for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                const uint8_t *entry = fs->sector_cache + offset;

                if (fat_dirent_is_end(entry)) {
                    if (!result->free_found) {
                        result->free_found = 1U;
                        result->free_loc.sector = sector;
                        result->free_loc.offset = (uint16_t)offset;
                    }
                    result->reached_end_marker = 1U;
                    return FAT_ERR_NOT_FOUND;
                }

                if (fat_dirent_is_deleted(entry)) {
                    fat_lfn_state_reset(&lfn_state);
                    if (!result->free_found) {
                        result->free_found = 1U;
                        result->free_loc.sector = sector;
                        result->free_loc.offset = (uint16_t)offset;
                    }
                    continue;
                }

                if (fat_dirent_is_lfn(entry)) {
                    fat_lfn_state_consume_entry(&lfn_state, entry);
                    continue;
                }

                if ((entry[11] & FAT_ATTR_VOLUME_ID) != 0U) {
                    fat_lfn_state_reset(&lfn_state);
                    continue;
                }

                if (fat_directory_entry_matches_segment(entry, &lfn_state, segment)) {
                    result->found = 1U;
                    result->found_loc.sector = sector;
                    result->found_loc.offset = (uint16_t)offset;
                    fat_mem_copy(result->found_entry, entry, FAT_DIRENT_SIZE);
                    return FAT_OK;
                }

                fat_lfn_state_reset(&lfn_state);
            }
        }

        return FAT_ERR_NOT_FOUND;
    }

    if (!fat_cluster_is_data(fs, dir->start_cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    {
        uint32_t cluster = dir->start_cluster;

        for (uint32_t guard = 0; guard < fs->cluster_count; guard++) {
            uint32_t first_sector = fat_cluster_to_sector(fs, cluster);
            result->last_cluster = cluster;

            for (uint32_t s = 0; s < (uint32_t)fs->sectors_per_cluster; s++) {
                uint32_t sector = first_sector + s;

                status = fat_cache_load_sector(fs, sector);
                if (status != FAT_OK) {
                    return status;
                }

                for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                    const uint8_t *entry = fs->sector_cache + offset;

                    if (fat_dirent_is_end(entry)) {
                        if (!result->free_found) {
                            result->free_found = 1U;
                            result->free_loc.sector = sector;
                            result->free_loc.offset = (uint16_t)offset;
                        }
                        result->reached_end_marker = 1U;
                        return FAT_ERR_NOT_FOUND;
                    }

                    if (fat_dirent_is_deleted(entry)) {
                        fat_lfn_state_reset(&lfn_state);
                        if (!result->free_found) {
                            result->free_found = 1U;
                            result->free_loc.sector = sector;
                            result->free_loc.offset = (uint16_t)offset;
                        }
                        continue;
                    }

                    if (fat_dirent_is_lfn(entry)) {
                        fat_lfn_state_consume_entry(&lfn_state, entry);
                        continue;
                    }

                    if ((entry[11] & FAT_ATTR_VOLUME_ID) != 0U) {
                        fat_lfn_state_reset(&lfn_state);
                        continue;
                    }

                    if (fat_directory_entry_matches_segment(entry, &lfn_state, segment)) {
                        result->found = 1U;
                        result->found_loc.sector = sector;
                        result->found_loc.offset = (uint16_t)offset;
                        fat_mem_copy(result->found_entry, entry, FAT_DIRENT_SIZE);
                        return FAT_OK;
                    }

                    fat_lfn_state_reset(&lfn_state);
                }
            }

            {
                uint32_t next = 0;
                status = fat_get_next_cluster(fs, cluster, &next);
                if (status != FAT_OK) {
                    return status;
                }

                if (next == 0U) {
                    return FAT_ERR_NOT_FOUND;
                }

                cluster = next;
            }
        }
    }

    return FAT_ERR_CORRUPTED;
}

static fat_status_t fat_directory_short_name_exists(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    const uint8_t short_name[11],
    uint8_t *exists_out)
{
    fat_dir_search_t search;
    fat_status_t status;

    if (fs == NULL || dir == NULL || short_name == NULL || exists_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    status = fat_directory_search(fs, dir, short_name, &search);
    if (status == FAT_OK) {
        *exists_out = 1U;
        return FAT_OK;
    }

    if (status == FAT_ERR_NOT_FOUND) {
        *exists_out = 0U;
        return FAT_OK;
    }

    return status;
}

static uint8_t fat_short_sanitize_char(uint16_t c)
{
    uint8_t ascii;

    if (c < 0x80U) {
        ascii = fat_to_upper((uint8_t)c);
        if (fat_is_valid_short_name_char(ascii)) {
            return ascii;
        }

        if (ascii == (uint8_t)' ' || ascii == (uint8_t)'.') {
            return 0U;
        }
    }

    return (uint8_t)'_';
}

static size_t fat_u32_to_ascii(uint32_t value, char *out, size_t out_capacity)
{
    char rev[10];
    size_t rev_len = 0;
    size_t out_len = 0;

    if (out == NULL || out_capacity == 0U) {
        return 0U;
    }

    if (value == 0U) {
        if (out_capacity < 2U) {
            return 0U;
        }
        out[0] = '0';
        out[1] = '\0';
        return 1U;
    }

    while (value > 0U && rev_len < sizeof(rev)) {
        rev[rev_len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    if (rev_len + 1U > out_capacity) {
        return 0U;
    }

    while (rev_len > 0U) {
        out[out_len++] = rev[--rev_len];
    }
    out[out_len] = '\0';
    return out_len;
}

static fat_status_t fat_generate_unique_short_name(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    const uint16_t *long_name,
    size_t long_len,
    uint8_t short_name_out[11])
{
    uint8_t base_seed[32];
    uint8_t ext_seed[3];
    size_t base_len = 0;
    size_t ext_len = 0;
    size_t dot_pos = (size_t)-1;

    if (fs == NULL || dir == NULL || long_name == NULL || short_name_out == NULL || long_len == 0U) {
        return FAT_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < long_len; i++) {
        if (long_name[i] == (uint16_t)'.') {
            dot_pos = i;
        }
    }

    if (dot_pos == 0U || dot_pos + 1U >= long_len) {
        dot_pos = (size_t)-1;
    }

    {
        size_t base_end = (dot_pos == (size_t)-1) ? long_len : dot_pos;
        for (size_t i = 0; i < base_end && base_len < sizeof(base_seed); i++) {
            uint8_t c = fat_short_sanitize_char(long_name[i]);
            if (c != 0U) {
                base_seed[base_len++] = c;
            }
        }
    }

    if (dot_pos != (size_t)-1) {
        for (size_t i = dot_pos + 1U; i < long_len && ext_len < sizeof(ext_seed); i++) {
            uint8_t c = fat_short_sanitize_char(long_name[i]);
            if (c != 0U) {
                ext_seed[ext_len++] = c;
            }
        }
    }

    if (base_len == 0U) {
        base_seed[0] = (uint8_t)'F';
        base_seed[1] = (uint8_t)'I';
        base_seed[2] = (uint8_t)'L';
        base_seed[3] = (uint8_t)'E';
        base_len = 4U;
    }

    for (uint32_t attempt = 0U; attempt < 1000000U; attempt++) {
        uint8_t exists = 0U;
        fat_status_t status;

        for (size_t i = 0; i < 11U; i++) {
            short_name_out[i] = (uint8_t)' ';
        }

        for (size_t i = 0; i < ext_len; i++) {
            short_name_out[8U + i] = ext_seed[i];
        }

        if (attempt == 0U) {
            size_t copy_len = (base_len < 8U) ? base_len : 8U;
            for (size_t i = 0; i < copy_len; i++) {
                short_name_out[i] = base_seed[i];
            }
        } else {
            char digits[11];
            size_t digits_len = fat_u32_to_ascii(attempt, digits, sizeof(digits));
            size_t tail_len;
            size_t prefix_len;

            if (digits_len == 0U) {
                continue;
            }

            tail_len = 1U + digits_len;
            if (tail_len > 8U) {
                continue;
            }

            prefix_len = 8U - tail_len;
            for (size_t i = 0; i < prefix_len; i++) {
                if (i < base_len) {
                    short_name_out[i] = base_seed[i];
                } else {
                    short_name_out[i] = (uint8_t)'_';
                }
            }

            short_name_out[prefix_len] = (uint8_t)'~';
            for (size_t i = 0; i < digits_len; i++) {
                short_name_out[prefix_len + 1U + i] = (uint8_t)digits[i];
            }
        }

        status = fat_directory_short_name_exists(fs, dir, short_name_out, &exists);
        if (status != FAT_OK) {
            return status;
        }
        if (!exists) {
            return FAT_OK;
        }
    }

    return FAT_ERR_NO_SPACE;
}

static void fat_lfn_entry_set_char(uint8_t *entry, size_t index, uint16_t value)
{
    static const uint8_t kOffsets[FAT_LFN_ENTRY_CHARS] = {
        1U, 3U, 5U, 7U, 9U,
        14U, 16U, 18U, 20U, 22U, 24U,
        28U, 30U
    };

    if (index < FAT_LFN_ENTRY_CHARS) {
        fat_store16(entry + kOffsets[index], value);
    }
}

static void fat_build_lfn_entry(
    uint8_t *entry,
    const uint16_t *name,
    size_t name_len,
    uint8_t ord,
    uint8_t checksum,
    uint8_t is_last)
{
    size_t base_index = (size_t)(ord - 1U) * FAT_LFN_ENTRY_CHARS;

    fat_mem_set(entry, 0, FAT_DIRENT_SIZE);
    entry[0] = (uint8_t)(ord | (is_last ? 0x40U : 0U));
    entry[11] = FAT_ATTR_LFN;
    entry[12] = 0U;
    entry[13] = checksum;
    fat_store16(entry + 26, 0U);

    for (size_t i = 0; i < FAT_LFN_ENTRY_CHARS; i++) {
        size_t idx = base_index + i;
        uint16_t ch;

        if (idx < name_len) {
            ch = name[idx];
        } else if (idx == name_len) {
            ch = 0x0000U;
        } else {
            ch = 0xFFFFU;
        }

        fat_lfn_entry_set_char(entry, i, ch);
    }
}

static fat_status_t fat_directory_allocate_slots(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    uint32_t needed_slots,
    fat_dirent_location_t *slots_out,
    uint8_t *used_end_marker_out)
{
    fat_dirent_location_t run_locs[FAT_LFN_MAX_ENTRIES + 1U];
    uint32_t run_clusters[FAT_LFN_MAX_ENTRIES + 1U];
    uint32_t run_count = 0U;
    uint8_t run_has_end_marker = 0U;

    if (fs == NULL || dir == NULL || slots_out == NULL || used_end_marker_out == NULL ||
        needed_slots == 0U || needed_slots > FAT_LFN_MAX_ENTRIES + 1U) {
        return FAT_ERR_INVALID_ARG;
    }

    if (dir->is_root_fixed) {
        uint32_t root_start = fs->root_dir_sector;
        uint32_t root_end = root_start + fs->root_dir_sector_count;

        for (uint32_t sector = root_start; sector < root_end; sector++) {
            fat_status_t status = fat_cache_load_sector(fs, sector);
            if (status != FAT_OK) {
                return status;
            }

            for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                const uint8_t *entry = fs->sector_cache + offset;
                fat_dirent_location_t loc;
                uint8_t is_free;
                uint8_t is_end;

                loc.sector = sector;
                loc.offset = (uint16_t)offset;
                is_end = fat_dirent_is_end(entry) ? 1U : 0U;
                is_free = (fat_dirent_is_deleted(entry) || is_end) ? 1U : 0U;

                if (!is_free) {
                    run_count = 0U;
                    run_has_end_marker = 0U;
                    continue;
                }

                run_locs[run_count] = loc;
                run_count++;
                if (is_end) {
                    run_has_end_marker = 1U;
                }

                if (run_count == needed_slots) {
                    for (uint32_t i = 0; i < needed_slots; i++) {
                        slots_out[i] = run_locs[i];
                    }
                    *used_end_marker_out = run_has_end_marker;
                    return FAT_OK;
                }

                if (is_end) {
                    fat_dirent_location_t cursor = loc;
                    while (run_count < needed_slots) {
                        if (!fat_directory_next_slot_root(fs, &cursor, &cursor)) {
                            return FAT_ERR_NO_SPACE;
                        }

                        run_locs[run_count] = cursor;
                        run_count++;
                    }

                    for (uint32_t i = 0; i < needed_slots; i++) {
                        slots_out[i] = run_locs[i];
                    }
                    *used_end_marker_out = 1U;
                    return FAT_OK;
                }
            }
        }

        return FAT_ERR_NO_SPACE;
    }

    if (!fat_cluster_is_data(fs, dir->start_cluster)) {
        return FAT_ERR_CORRUPTED;
    }

    {
        uint32_t cluster = dir->start_cluster;

        for (uint32_t guard = 0; guard < fs->cluster_count; guard++) {
            uint32_t first_sector = fat_cluster_to_sector(fs, cluster);

            for (uint32_t s = 0; s < (uint32_t)fs->sectors_per_cluster; s++) {
                uint32_t sector = first_sector + s;
                fat_status_t status = fat_cache_load_sector(fs, sector);
                if (status != FAT_OK) {
                    return status;
                }

                for (uint32_t offset = 0; offset + FAT_DIRENT_SIZE <= (uint32_t)fs->bytes_per_sector; offset += FAT_DIRENT_SIZE) {
                    const uint8_t *entry = fs->sector_cache + offset;
                    fat_dirent_location_t loc;
                    uint8_t is_free;
                    uint8_t is_end;

                    loc.sector = sector;
                    loc.offset = (uint16_t)offset;
                    is_end = fat_dirent_is_end(entry) ? 1U : 0U;
                    is_free = (fat_dirent_is_deleted(entry) || is_end) ? 1U : 0U;

                    if (!is_free) {
                        run_count = 0U;
                        run_has_end_marker = 0U;
                        continue;
                    }

                    run_locs[run_count] = loc;
                    run_clusters[run_count] = cluster;
                    run_count++;
                    if (is_end) {
                        run_has_end_marker = 1U;
                    }

                    if (run_count == needed_slots) {
                        for (uint32_t i = 0; i < needed_slots; i++) {
                            slots_out[i] = run_locs[i];
                        }
                        *used_end_marker_out = run_has_end_marker;
                        return FAT_OK;
                    }

                    if (is_end) {
                        fat_dirent_location_t cursor = loc;
                        uint32_t cursor_cluster = cluster;

                        while (run_count < needed_slots) {
                            status = fat_directory_advance_slot_cluster(fs, &cursor, &cursor_cluster, 1U);
                            if (status != FAT_OK) {
                                return status;
                            }
                            run_locs[run_count] = cursor;
                            run_clusters[run_count] = cursor_cluster;
                            run_count++;
                        }

                        for (uint32_t i = 0; i < needed_slots; i++) {
                            slots_out[i] = run_locs[i];
                        }
                        *used_end_marker_out = 1U;
                        return FAT_OK;
                    }
                }
            }

            {
                uint32_t next = 0U;
                fat_status_t status = fat_get_next_cluster(fs, cluster, &next);
                if (status != FAT_OK) {
                    return status;
                }

                if (next == 0U) {
                    fat_dirent_location_t cursor;
                    uint32_t cursor_cluster;

                    if (run_count == 0U) {
                        status = fat_directory_append_cluster(fs, cluster, &next);
                        if (status != FAT_OK) {
                            return status;
                        }

                        run_locs[0].sector = fat_cluster_to_sector(fs, next);
                        run_locs[0].offset = 0U;
                        run_clusters[0] = next;
                        run_count = 1U;
                        run_has_end_marker = 1U;

                        if (run_count == needed_slots) {
                            slots_out[0] = run_locs[0];
                            *used_end_marker_out = 1U;
                            return FAT_OK;
                        }
                    }

                    cursor = run_locs[run_count - 1U];
                    cursor_cluster = run_clusters[run_count - 1U];
                    while (run_count < needed_slots) {
                        status = fat_directory_advance_slot_cluster(fs, &cursor, &cursor_cluster, 1U);
                        if (status != FAT_OK) {
                            return status;
                        }

                        run_locs[run_count] = cursor;
                        run_clusters[run_count] = cursor_cluster;
                        run_count++;
                    }

                    for (uint32_t i = 0; i < needed_slots; i++) {
                        slots_out[i] = run_locs[i];
                    }
                    *used_end_marker_out = 1U;
                    return FAT_OK;
                }

                cluster = next;
            }
        }
    }

    return FAT_ERR_CORRUPTED;
}

static fat_status_t fat_directory_set_end_marker_after(
    fat_fs_t *fs,
    const fat_directory_t *dir,
    const fat_dirent_location_t *last_slot)
{
    fat_dirent_location_t next_loc;
    uint8_t has_next = 0U;

    if (fs == NULL || dir == NULL || last_slot == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (dir->is_root_fixed) {
        if (!fat_directory_next_slot_root(fs, last_slot, &next_loc)) {
            return FAT_OK;
        }
        has_next = 1U;
    } else {
        uint32_t cluster;
        uint32_t next_cluster;
        fat_status_t status;

        if (!fat_sector_to_cluster(fs, last_slot->sector, &cluster)) {
            return FAT_ERR_CORRUPTED;
        }

        status = fat_directory_next_slot_cluster_existing(
            fs, last_slot, cluster, &next_loc, &next_cluster, &has_next);
        if (status != FAT_OK) {
            return status;
        }
    }

    if (has_next) {
        uint8_t end_entry[FAT_DIRENT_SIZE];
        fat_mem_set(end_entry, 0, sizeof(end_entry));
        return fat_write_dirent(fs, &next_loc, end_entry);
    }

    return FAT_OK;
}

static fat_status_t fat_create_file_entry(
    fat_fs_t *fs,
    const fat_directory_t *parent,
    const char *segment,
    fat_dirent_location_t *short_loc_out,
    uint8_t *short_entry_out)
{
    fat_status_t status;
    uint16_t long_name[FAT_LFN_MAX_CHARS];
    size_t long_len = 0U;
    uint8_t short_name[11];
    uint8_t has_plain_short = 0U;
    uint8_t need_lfn = 0U;
    uint8_t used_end_marker = 0U;
    uint8_t short_exists = 0U;
    uint32_t lfn_count;
    uint32_t needed_slots;
    fat_dirent_location_t slots[FAT_LFN_MAX_ENTRIES + 1U];
    uint8_t entry[FAT_DIRENT_SIZE];

    if (fs == NULL || parent == NULL || segment == NULL || short_loc_out == NULL || short_entry_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    status = fat_segment_to_utf16_name(segment, long_name, &long_len);
    if (status != FAT_OK) {
        return status;
    }

    if (fat_make_short_name_83(segment, short_name) && !fat_segment_has_lowercase_ascii(segment)) {
        has_plain_short = 1U;
    }

    need_lfn = has_plain_short ? 0U : 1U;

    if (has_plain_short) {
        status = fat_directory_short_name_exists(fs, parent, short_name, &short_exists);
        if (status != FAT_OK) {
            return status;
        }
        if (short_exists) {
            return FAT_ERR_ALREADY_EXISTS;
        }
    } else {
        status = fat_generate_unique_short_name(fs, parent, long_name, long_len, short_name);
        if (status != FAT_OK) {
            return status;
        }
    }

    lfn_count = need_lfn ? (uint32_t)((long_len + FAT_LFN_ENTRY_CHARS - 1U) / FAT_LFN_ENTRY_CHARS) : 0U;
    needed_slots = lfn_count + 1U;

    status = fat_directory_allocate_slots(fs, parent, needed_slots, slots, &used_end_marker);
    if (status != FAT_OK) {
        return status;
    }

    if (need_lfn) {
        uint8_t checksum = fat_short_name_checksum(short_name);

        for (uint32_t i = 0; i < lfn_count; i++) {
            uint8_t ord = (uint8_t)(lfn_count - i);
            uint8_t is_last = (ord == lfn_count) ? 1U : 0U;

            fat_build_lfn_entry(entry, long_name, long_len, ord, checksum, is_last);
            status = fat_write_dirent(fs, &slots[i], entry);
            if (status != FAT_OK) {
                return status;
            }
        }
    }

    fat_mem_set(entry, 0, sizeof(entry));
    fat_mem_copy(entry, short_name, 11U);
    entry[11] = FAT_ATTR_ARCHIVE;
    fat_dirent_set_first_cluster(entry, 0U);
    fat_store32(entry + 28, 0U);

    status = fat_write_dirent(fs, &slots[needed_slots - 1U], entry);
    if (status != FAT_OK) {
        return status;
    }

    if (used_end_marker) {
        status = fat_directory_set_end_marker_after(fs, parent, &slots[needed_slots - 1U]);
        if (status != FAT_OK) {
            return status;
        }
    }

    *short_loc_out = slots[needed_slots - 1U];
    fat_mem_copy(short_entry_out, entry, FAT_DIRENT_SIZE);
    return FAT_OK;
}

static fat_status_t fat_resolve_parent_directory(
    fat_fs_t *fs,
    const char *path,
    fat_directory_t *parent_out,
    char *final_segment_out,
    size_t final_segment_capacity)
{
    fat_status_t status;
    fat_directory_t current;
    size_t cursor = 0;
    char segment[FAT_SEGMENT_MAX_UTF8_BYTES];
    uint8_t is_last = 0;

    if (fs == NULL || path == NULL || parent_out == NULL || final_segment_out == NULL || final_segment_capacity == 0U) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fat_path_is_root_only(path)) {
        return FAT_ERR_IS_DIR;
    }

    status = fat_path_next_segment(path, &cursor, segment, sizeof(segment), &is_last);
    if (status != FAT_OK) {
        return (status == FAT_ERR_NOT_FOUND) ? FAT_ERR_NOT_FOUND : status;
    }

    fat_get_root_directory(fs, &current);

    for (;;) {
        if (is_last) {
            *parent_out = current;
            if (fat_str_len(segment) + 1U > final_segment_capacity) {
                return FAT_ERR_NOT_SUPPORTED;
            }
            fat_mem_copy(final_segment_out, segment, fat_str_len(segment) + 1U);
            return FAT_OK;
        }

        {
            fat_dir_search_t search;
            status = fat_directory_search_name(fs, &current, segment, &search);
            if (status != FAT_OK) {
                return status;
            }

            if ((search.found_entry[11] & FAT_ATTR_DIRECTORY) == 0U) {
                return FAT_ERR_NOT_DIR;
            }

            current.is_root_fixed = 0U;
            current.start_cluster = fat_dirent_first_cluster(search.found_entry);
            if (!fat_cluster_is_data(fs, current.start_cluster)) {
                return FAT_ERR_CORRUPTED;
            }
        }

        status = fat_path_next_segment(path, &cursor, segment, sizeof(segment), &is_last);
        if (status != FAT_OK) {
            return status;
        }
    }
}

static fat_status_t fat_find_entry_by_path(
    fat_fs_t *fs,
    const char *path,
    fat_directory_t *parent_out,
    fat_dirent_location_t *loc_out,
    uint8_t *entry_out)
{
    fat_status_t status;
    fat_directory_t parent;
    char final_segment[FAT_SEGMENT_MAX_UTF8_BYTES];
    fat_dir_search_t search;

    if (fs == NULL || path == NULL || parent_out == NULL || loc_out == NULL || entry_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fat_path_has_trailing_slash(path)) {
        return FAT_ERR_IS_DIR;
    }

    status = fat_resolve_parent_directory(fs, path, &parent, final_segment, sizeof(final_segment));
    if (status != FAT_OK) {
        return status;
    }

    status = fat_directory_search_name(fs, &parent, final_segment, &search);
    if (status != FAT_OK) {
        return status;
    }

    *parent_out = parent;
    *loc_out = search.found_loc;
    fat_mem_copy(entry_out, search.found_entry, FAT_DIRENT_SIZE);
    return FAT_OK;
}

static fat_status_t fat_file_commit_metadata(fat_file_t *file)
{
    fat_status_t status;
    uint8_t entry[FAT_DIRENT_SIZE];
    fat_dirent_location_t loc;

    if (file == NULL || !file->open || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!file->metadata_dirty) {
        return FAT_OK;
    }

    if (file->size > 0xFFFFFFFFULL) {
        return FAT_ERR_NOT_SUPPORTED;
    }

    loc.sector = file->dir_entry_sector;
    loc.offset = file->dir_entry_offset;

    status = fat_read_dirent(file->fs, &loc, entry);
    if (status != FAT_OK) {
        return status;
    }

    if (fat_dirent_is_deleted(entry) || fat_dirent_is_end(entry)) {
        return FAT_ERR_CORRUPTED;
    }

    fat_dirent_set_first_cluster(entry, file->first_cluster);
    fat_store32(entry + 28, (uint32_t)file->size);
    entry[11] |= FAT_ATTR_ARCHIVE;

    status = fat_write_dirent(file->fs, &loc, entry);
    if (status != FAT_OK) {
        return status;
    }

    file->metadata_dirty = 0U;
    return FAT_OK;
}

static fat_status_t fat_file_locate_cluster(
    fat_file_t *file,
    uint64_t position,
    uint8_t allocate,
    uint32_t *cluster_out)
{
    fat_status_t status;
    fat_fs_t *fs;
    uint64_t target_index64;
    uint32_t target_index;
    uint32_t cluster;
    uint32_t index;

    if (file == NULL || cluster_out == NULL || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    fs = file->fs;
    target_index64 = position / (uint64_t)fat_cluster_size_bytes(fs);
    if (target_index64 > 0xFFFFFFFFULL) {
        return FAT_ERR_NOT_SUPPORTED;
    }
    target_index = (uint32_t)target_index64;

    if (target_index >= fs->cluster_count) {
        return allocate ? FAT_ERR_NO_SPACE : FAT_ERR_CORRUPTED;
    }

    if (file->first_cluster == 0U) {
        if (!allocate) {
            return FAT_ERR_NOT_FOUND;
        }

        status = fat_allocate_cluster(fs, &cluster);
        if (status != FAT_OK) {
            return status;
        }

        file->first_cluster = cluster;
        file->current_cluster = cluster;
        file->current_cluster_index = 0U;
        file->metadata_dirty = 1U;
    }

    if (file->current_cluster != 0U && file->current_cluster_index <= target_index) {
        cluster = file->current_cluster;
        index = file->current_cluster_index;
    } else {
        cluster = file->first_cluster;
        index = 0U;
    }

    while (index < target_index) {
        uint32_t next = 0;

        status = fat_get_next_cluster(fs, cluster, &next);
        if (status != FAT_OK) {
            return status;
        }

        if (next == 0U) {
            if (!allocate) {
                return FAT_ERR_CORRUPTED;
            }

            status = fat_allocate_cluster(fs, &next);
            if (status != FAT_OK) {
                return status;
            }

            status = fat_set_fat_entry(fs, cluster, next);
            if (status != FAT_OK) {
                (void)fat_set_fat_entry(fs, next, FAT_CLUSTER_FREE);
                return status;
            }
        }

        cluster = next;
        index++;
    }

    file->current_cluster = cluster;
    file->current_cluster_index = target_index;
    *cluster_out = cluster;
    return FAT_OK;
}

static fat_status_t fat_file_write_internal(
    fat_file_t *file,
    const uint8_t *data,
    size_t size,
    size_t *written_out)
{
    fat_status_t status;
    fat_fs_t *fs;
    size_t written = 0;

    if (file == NULL || file->fs == NULL || written_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    fs = file->fs;

    while (written < size) {
        uint32_t cluster;
        uint32_t cluster_size = fat_cluster_size_bytes(fs);
        uint32_t cluster_offset = (uint32_t)(file->position % (uint64_t)cluster_size);
        uint32_t sector_in_cluster = cluster_offset / (uint32_t)fs->bytes_per_sector;
        uint32_t offset_in_sector = cluster_offset % (uint32_t)fs->bytes_per_sector;
        uint32_t data_sector;
        size_t chunk;

        status = fat_file_locate_cluster(file, file->position, 1U, &cluster);
        if (status != FAT_OK) {
            *written_out = written;
            return status;
        }

        data_sector = fat_cluster_to_sector(fs, cluster) + sector_in_cluster;
        status = fat_cache_load_sector(fs, data_sector);
        if (status != FAT_OK) {
            *written_out = written;
            return status;
        }

        chunk = fat_min_size(size - written, (size_t)fs->bytes_per_sector - (size_t)offset_in_sector);
        if (data != NULL) {
            fat_mem_copy(fs->sector_cache + offset_in_sector, data + written, chunk);
        } else {
            fat_mem_set(fs->sector_cache + offset_in_sector, 0U, chunk);
        }

        fs->cache_dirty = 1U;
        file->position += (uint64_t)chunk;
        written += chunk;
    }

    if (file->position > file->size) {
        file->size = file->position;
        file->metadata_dirty = 1U;
    }

    file->attr |= FAT_ATTR_ARCHIVE;
    *written_out = written;
    return FAT_OK;
}

static fat_status_t fat_file_fill_gap_with_zeros(fat_file_t *file, uint64_t gap_bytes)
{
    fat_status_t status;
    uint8_t zero_chunk[128];

    fat_mem_set(zero_chunk, 0, sizeof(zero_chunk));

    while (gap_bytes > 0U) {
        size_t chunk = (gap_bytes > (uint64_t)sizeof(zero_chunk)) ? sizeof(zero_chunk) : (size_t)gap_bytes;
        size_t written = 0;

        status = fat_file_write_internal(file, NULL, chunk, &written);
        if (status != FAT_OK) {
            return status;
        }

        if (written != chunk) {
            return FAT_ERR_IO;
        }

        gap_bytes -= (uint64_t)chunk;
    }

    return FAT_OK;
}

fat_status_t fat_mount(fat_fs_t *fs, const fat_mount_config_t *config)
{
    fat_status_t status;
    uint8_t boot_sector[FAT_MAX_SECTOR_SIZE];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_dir_sector_count;
    uint32_t first_data_sector;
    uint32_t data_sectors;
    uint32_t cluster_count;

    if (fs == NULL || config == NULL || config->device.read_blocks == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (fs->mounted) {
        status = fat_unmount(fs);
        if (status != FAT_OK) {
            return status;
        }
    }

    fat_mem_set(fs, 0, sizeof(*fs));
    fs->device = config->device;
    fs->partition_lba = config->partition_lba;

    if (!fs->device.read_blocks(fs->device.user, fs->partition_lba, 1U, boot_sector)) {
        return FAT_ERR_IO;
    }

    if (boot_sector[510] != 0x55U || boot_sector[511] != 0xAAU) {
        return FAT_ERR_BAD_FS;
    }

    bytes_per_sector = fat_le16(boot_sector + 11);
    sectors_per_cluster = boot_sector[13];
    reserved_sectors = fat_le16(boot_sector + 14);
    fat_count = boot_sector[16];
    root_entry_count = fat_le16(boot_sector + 17);

    total_sectors = (uint32_t)fat_le16(boot_sector + 19);
    if (total_sectors == 0U) {
        total_sectors = fat_le32(boot_sector + 32);
    }

    sectors_per_fat = (uint32_t)fat_le16(boot_sector + 22);
    if (sectors_per_fat == 0U) {
        sectors_per_fat = fat_le32(boot_sector + 36);
    }

    if (bytes_per_sector < FAT_MIN_SECTOR_SIZE || bytes_per_sector > FAT_MAX_SECTOR_SIZE ||
        !fat_is_power_of_two_u32((uint32_t)bytes_per_sector)) {
        return FAT_ERR_BAD_FS;
    }

    if (sectors_per_cluster == 0U || !fat_is_power_of_two_u32((uint32_t)sectors_per_cluster)) {
        return FAT_ERR_BAD_FS;
    }

    if (reserved_sectors == 0U || fat_count == 0U || total_sectors == 0U || sectors_per_fat == 0U) {
        return FAT_ERR_BAD_FS;
    }

    root_dir_sector_count = ((uint32_t)root_entry_count * FAT_DIRENT_SIZE + ((uint32_t)bytes_per_sector - 1U)) /
                            (uint32_t)bytes_per_sector;

    fs->bytes_per_sector = bytes_per_sector;
    fs->sectors_per_cluster = sectors_per_cluster;
    fs->reserved_sectors = reserved_sectors;
    fs->fat_count = fat_count;
    fs->sectors_per_fat = sectors_per_fat;
    fs->root_entry_count = root_entry_count;
    fs->total_sectors = total_sectors;

    fs->first_fat_sector = (uint32_t)reserved_sectors;
    fs->root_dir_sector = fs->first_fat_sector + ((uint32_t)fat_count * sectors_per_fat);
    fs->root_dir_sector_count = root_dir_sector_count;

    first_data_sector = fs->root_dir_sector + root_dir_sector_count;
    if (first_data_sector >= total_sectors) {
        return FAT_ERR_BAD_FS;
    }

    fs->first_data_sector = first_data_sector;

    data_sectors = total_sectors - first_data_sector;
    cluster_count = data_sectors / (uint32_t)sectors_per_cluster;
    if (cluster_count == 0U) {
        return FAT_ERR_BAD_FS;
    }

    fs->cluster_count = cluster_count;

    if (cluster_count < 4085U) {
        fs->fat_type = FAT_TYPE_12;
    } else if (cluster_count < 65525U) {
        fs->fat_type = FAT_TYPE_16;
    } else {
        fs->fat_type = FAT_TYPE_32;
    }

    if (fs->fat_type == FAT_TYPE_32) {
        fs->root_cluster = fat_le32(boot_sector + 44) & 0x0FFFFFFFU;
        if (!fat_cluster_is_data(fs, fs->root_cluster)) {
            return FAT_ERR_BAD_FS;
        }

        fs->root_dir_sector = 0U;
        fs->root_dir_sector_count = 0U;
    } else {
        if (root_entry_count == 0U) {
            return FAT_ERR_BAD_FS;
        }
        fs->root_cluster = 0U;
    }

    fs->sector_cache = (uint8_t *)malloc(bytes_per_sector);
    if (fs->sector_cache == NULL) {
        return FAT_ERR_NO_SPACE;
    }

    fs->cache_valid = 0U;
    fs->cache_dirty = 0U;
    fs->cache_sector = 0U;

    fs->read_only = (config->device.write_blocks == NULL) ? 1U : 0U;
    fs->next_alloc_hint = 2U;
    fs->mounted = 1U;
    return FAT_OK;
}

fat_status_t fat_unmount(fat_fs_t *fs)
{
    fat_status_t status = FAT_OK;

    if (fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->mounted) {
        fat_mem_set(fs, 0, sizeof(*fs));
        return FAT_OK;
    }

    status = fat_cache_flush_internal(fs);

    if (fs->sector_cache != NULL) {
        free(fs->sector_cache);
    }

    fat_mem_set(fs, 0, sizeof(*fs));
    return status;
}

fat_status_t fat_sync(fat_fs_t *fs)
{
    if (fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->mounted) {
        return FAT_ERR_NOT_MOUNTED;
    }

    return fat_cache_flush_internal(fs);
}

fat_status_t fat_open(fat_fs_t *fs, fat_file_t *file, const char *path, uint32_t flags)
{
    fat_status_t status;
    fat_directory_t parent;
    char final_segment[FAT_SEGMENT_MAX_UTF8_BYTES];
    fat_dir_search_t search;
    uint8_t write_requested;

    if (fs == NULL || file == NULL || path == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->mounted) {
        return FAT_ERR_NOT_MOUNTED;
    }

    if ((flags & (FAT_O_READ | FAT_O_WRITE)) == 0U) {
        return FAT_ERR_INVALID_ARG;
    }

    write_requested = (flags & FAT_O_WRITE) ? 1U : 0U;
    if ((flags & (FAT_O_CREATE | FAT_O_TRUNC | FAT_O_APPEND)) != 0U && !write_requested) {
        return FAT_ERR_INVALID_ARG;
    }

    if (write_requested && fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    if (fat_path_has_trailing_slash(path)) {
        return FAT_ERR_IS_DIR;
    }

    status = fat_resolve_parent_directory(fs, path, &parent, final_segment, sizeof(final_segment));
    if (status != FAT_OK) {
        return status;
    }

    status = fat_directory_search_name(fs, &parent, final_segment, &search);
    if (status == FAT_OK) {
        uint8_t *entry = search.found_entry;
        uint8_t attr = entry[11];
        uint32_t first_cluster = fat_dirent_first_cluster(entry);
        uint32_t file_size = fat_dirent_file_size(entry);

        if ((attr & FAT_ATTR_DIRECTORY) != 0U) {
            return FAT_ERR_IS_DIR;
        }

        if ((file_size > 0U && first_cluster == 0U) ||
            (first_cluster != 0U && !fat_cluster_is_data(fs, first_cluster))) {
            return FAT_ERR_CORRUPTED;
        }

        if ((attr & FAT_ATTR_READ_ONLY) != 0U && write_requested) {
            return FAT_ERR_ACCESS;
        }

        if ((flags & FAT_O_TRUNC) != 0U) {
            status = fat_free_cluster_chain(fs, first_cluster);
            if (status != FAT_OK) {
                return status;
            }

            fat_dirent_set_first_cluster(entry, 0U);
            fat_store32(entry + 28, 0U);
            entry[11] |= FAT_ATTR_ARCHIVE;

            status = fat_write_dirent(fs, &search.found_loc, entry);
            if (status != FAT_OK) {
                return status;
            }

            first_cluster = 0U;
            file_size = 0U;
        }

        fat_reset_file(file);
        file->fs = fs;
        file->first_cluster = first_cluster;
        file->current_cluster = first_cluster;
        file->current_cluster_index = 0U;
        file->position = ((flags & FAT_O_APPEND) != 0U) ? (uint64_t)file_size : 0U;
        file->size = (uint64_t)file_size;
        file->dir_entry_sector = search.found_loc.sector;
        file->dir_entry_offset = search.found_loc.offset;
        file->mode_flags = (uint8_t)(flags & 0xFFU);
        file->attr = attr;
        file->open = 1U;
        file->metadata_dirty = 0U;
        return FAT_OK;
    }

    if (status != FAT_ERR_NOT_FOUND) {
        return status;
    }

    if ((flags & FAT_O_CREATE) == 0U) {
        return FAT_ERR_NOT_FOUND;
    }

    {
        fat_dirent_location_t new_loc;
        uint8_t new_entry[FAT_DIRENT_SIZE];

        status = fat_create_file_entry(fs, &parent, final_segment, &new_loc, new_entry);
        if (status != FAT_OK) {
            return status;
        }

        fat_reset_file(file);
        file->fs = fs;
        file->first_cluster = 0U;
        file->current_cluster = 0U;
        file->current_cluster_index = 0U;
        file->position = 0U;
        file->size = 0U;
        file->dir_entry_sector = new_loc.sector;
        file->dir_entry_offset = new_loc.offset;
        file->mode_flags = (uint8_t)(flags & 0xFFU);
        file->attr = new_entry[11];
        file->open = 1U;
        file->metadata_dirty = 0U;

        if ((flags & FAT_O_APPEND) != 0U) {
            file->position = file->size;
        }

        return FAT_OK;
    }
}

fat_status_t fat_read(fat_file_t *file, void *buffer, size_t bytes_to_read, size_t *bytes_read)
{
    fat_status_t status;
    fat_fs_t *fs;
    uint8_t *out;
    size_t done = 0;

    if (bytes_read != NULL) {
        *bytes_read = 0;
    }

    if (file == NULL || buffer == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!file->open || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if ((file->mode_flags & FAT_O_READ) == 0U) {
        return FAT_ERR_ACCESS;
    }

    if (bytes_to_read == 0U) {
        return FAT_OK;
    }

    if (file->position >= file->size) {
        return FAT_OK;
    }

    fs = file->fs;
    out = (uint8_t *)buffer;

    {
        uint64_t remain64 = file->size - file->position;
        if (remain64 < (uint64_t)bytes_to_read) {
            bytes_to_read = (size_t)remain64;
        }
    }

    while (done < bytes_to_read) {
        uint32_t cluster;
        uint32_t cluster_size = fat_cluster_size_bytes(fs);
        uint32_t cluster_offset = (uint32_t)(file->position % (uint64_t)cluster_size);
        uint32_t sector_in_cluster = cluster_offset / (uint32_t)fs->bytes_per_sector;
        uint32_t offset_in_sector = cluster_offset % (uint32_t)fs->bytes_per_sector;
        uint32_t data_sector;
        size_t chunk;

        status = fat_file_locate_cluster(file, file->position, 0U, &cluster);
        if (status != FAT_OK) {
            if (bytes_read != NULL) {
                *bytes_read = done;
            }
            return status;
        }

        data_sector = fat_cluster_to_sector(fs, cluster) + sector_in_cluster;
        status = fat_cache_load_sector(fs, data_sector);
        if (status != FAT_OK) {
            if (bytes_read != NULL) {
                *bytes_read = done;
            }
            return status;
        }

        chunk = fat_min_size(bytes_to_read - done, (size_t)fs->bytes_per_sector - (size_t)offset_in_sector);
        fat_mem_copy(out + done, fs->sector_cache + offset_in_sector, chunk);

        file->position += (uint64_t)chunk;
        done += chunk;
    }

    if (bytes_read != NULL) {
        *bytes_read = done;
    }

    return FAT_OK;
}

fat_status_t fat_write(fat_file_t *file, const void *buffer, size_t bytes_to_write, size_t *bytes_written)
{
    fat_status_t status;
    const uint8_t *data;
    size_t done = 0;

    if (bytes_written != NULL) {
        *bytes_written = 0;
    }

    if (file == NULL || (buffer == NULL && bytes_to_write != 0U)) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!file->open || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if ((file->mode_flags & FAT_O_WRITE) == 0U) {
        return FAT_ERR_ACCESS;
    }

    if (file->fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    if (bytes_to_write == 0U) {
        return FAT_OK;
    }

    if (file->position > 0xFFFFFFFFULL) {
        return FAT_ERR_NOT_SUPPORTED;
    }

    if ((uint64_t)bytes_to_write > (0xFFFFFFFFULL - file->position)) {
        return FAT_ERR_NOT_SUPPORTED;
    }

    data = (const uint8_t *)buffer;

    if (file->position > file->size) {
        uint64_t target_pos = file->position;
        uint64_t gap;

        file->position = file->size;
        gap = target_pos - file->size;

        status = fat_file_fill_gap_with_zeros(file, gap);
        if (status != FAT_OK) {
            return status;
        }
    }

    status = fat_file_write_internal(file, data, bytes_to_write, &done);
    if (status != FAT_OK) {
        if (bytes_written != NULL) {
            *bytes_written = done;
        }
        return status;
    }

    status = fat_cache_flush_internal(file->fs);
    if (status != FAT_OK) {
        if (bytes_written != NULL) {
            *bytes_written = done;
        }
        return status;
    }

    status = fat_file_commit_metadata(file);
    if (status != FAT_OK) {
        if (bytes_written != NULL) {
            *bytes_written = done;
        }
        return status;
    }

    if (bytes_written != NULL) {
        *bytes_written = done;
    }

    return FAT_OK;
}

fat_status_t fat_seek(fat_file_t *file, int64_t offset, fat_seek_origin_t origin, uint64_t *new_position)
{
    uint64_t base;
    uint64_t target;

    if (file == NULL || !file->open || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (origin == FAT_SEEK_SET) {
        base = 0U;
    } else if (origin == FAT_SEEK_CUR) {
        base = file->position;
    } else if (origin == FAT_SEEK_END) {
        base = file->size;
    } else {
        return FAT_ERR_INVALID_ARG;
    }

    if (offset < 0) {
        uint64_t abs = (uint64_t)(-offset);
        if (abs > base) {
            return FAT_ERR_INVALID_ARG;
        }
        target = base - abs;
    } else {
        uint64_t add = (uint64_t)offset;
        if (base > UINT64_MAX - add) {
            return FAT_ERR_INVALID_ARG;
        }
        target = base + add;
    }

    file->position = target;
    file->current_cluster = file->first_cluster;
    file->current_cluster_index = 0U;

    if (new_position != NULL) {
        *new_position = target;
    }

    return FAT_OK;
}

fat_status_t fat_flush(fat_file_t *file)
{
    fat_status_t status;

    if (file == NULL || !file->open || file->fs == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    status = fat_cache_flush_internal(file->fs);
    if (status != FAT_OK) {
        return status;
    }

    status = fat_file_commit_metadata(file);
    if (status != FAT_OK) {
        return status;
    }

    return fat_cache_flush_internal(file->fs);
}

fat_status_t fat_close(fat_file_t *file)
{
    fat_status_t status;

    if (file == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!file->open) {
        fat_reset_file(file);
        return FAT_OK;
    }

    status = fat_flush(file);
    if (status != FAT_OK) {
        return status;
    }

    fat_reset_file(file);
    return FAT_OK;
}

fat_status_t fat_remove(fat_fs_t *fs, const char *path)
{
    fat_status_t status;
    fat_directory_t parent;
    fat_dirent_location_t loc;
    uint8_t entry[FAT_DIRENT_SIZE];
    uint32_t first_cluster;

    if (fs == NULL || path == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->mounted) {
        return FAT_ERR_NOT_MOUNTED;
    }

    if (fs->read_only) {
        return FAT_ERR_ACCESS;
    }

    if (fat_path_has_trailing_slash(path)) {
        return FAT_ERR_IS_DIR;
    }

    status = fat_find_entry_by_path(fs, path, &parent, &loc, entry);
    if (status != FAT_OK) {
        return status;
    }

    if ((entry[11] & FAT_ATTR_DIRECTORY) != 0U) {
        return FAT_ERR_IS_DIR;
    }

    if ((entry[11] & FAT_ATTR_READ_ONLY) != 0U) {
        return FAT_ERR_ACCESS;
    }

    first_cluster = fat_dirent_first_cluster(entry);
    if (first_cluster != 0U) {
        status = fat_free_cluster_chain(fs, first_cluster);
        if (status != FAT_OK) {
            return status;
        }
    }

    entry[0] = 0xE5U;
    status = fat_write_dirent(fs, &loc, entry);
    if (status != FAT_OK) {
        return status;
    }

    return fat_cache_flush_internal(fs);
}

fat_status_t fat_stat(fat_fs_t *fs, const char *path, fat_file_info_t *info_out)
{
    fat_status_t status;
    fat_directory_t parent;
    fat_dirent_location_t loc;
    uint8_t entry[FAT_DIRENT_SIZE];

    if (fs == NULL || path == NULL || info_out == NULL) {
        return FAT_ERR_INVALID_ARG;
    }

    if (!fs->mounted) {
        return FAT_ERR_NOT_MOUNTED;
    }

    if (fat_path_is_root_only(path)) {
        info_out->size = 0U;
        info_out->is_directory = 1U;
        info_out->read_only = fs->read_only;
        info_out->archive = 0U;
        return FAT_OK;
    }

    if (fat_path_has_trailing_slash(path)) {
        return FAT_ERR_IS_DIR;
    }

    status = fat_find_entry_by_path(fs, path, &parent, &loc, entry);
    if (status != FAT_OK) {
        return status;
    }

    info_out->size = (uint64_t)fat_dirent_file_size(entry);
    info_out->is_directory = ((entry[11] & FAT_ATTR_DIRECTORY) != 0U) ? 1U : 0U;
    info_out->read_only = ((entry[11] & FAT_ATTR_READ_ONLY) != 0U) ? 1U : 0U;
    info_out->archive = ((entry[11] & FAT_ATTR_ARCHIVE) != 0U) ? 1U : 0U;

    return FAT_OK;
}
