#ifndef FAT_H
#define FAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Block device callbacks.
 * - Return non-zero on success, 0 on failure.
 * - LBA is addressed in FAT logical sectors for the mounted volume.
 *
 * Notes:
 * - This implementation supports FAT12/FAT16/FAT32.
 * - Path parser supports short 8.3 and VFAT LFN.
 * - LFN input path is UTF-8 (1/2/3-byte sequences, BMP range).
 */
typedef int (*fat_read_blocks_fn)(void *user, uint64_t lba, uint32_t block_count, void *buffer);
typedef int (*fat_write_blocks_fn)(void *user, uint64_t lba, uint32_t block_count, const void *buffer);

typedef struct {
    void *user;
    fat_read_blocks_fn read_blocks;
    fat_write_blocks_fn write_blocks; /* NULL means read-only mount */
} fat_block_device_t;

typedef enum {
    FAT_OK = 0,
    FAT_ERR_INVALID_ARG,
    FAT_ERR_NOT_MOUNTED,
    FAT_ERR_IO,
    FAT_ERR_BAD_FS,
    FAT_ERR_NO_SPACE,
    FAT_ERR_NOT_FOUND,
    FAT_ERR_ALREADY_EXISTS,
    FAT_ERR_ACCESS,
    FAT_ERR_IS_DIR,
    FAT_ERR_NOT_DIR,
    FAT_ERR_NOT_SUPPORTED,
    FAT_ERR_CORRUPTED
} fat_status_t;

typedef enum {
    FAT_SEEK_SET = 0,
    FAT_SEEK_CUR = 1,
    FAT_SEEK_END = 2
} fat_seek_origin_t;

#define FAT_O_READ    (1U << 0)
#define FAT_O_WRITE   (1U << 1)
#define FAT_O_CREATE  (1U << 2)
#define FAT_O_TRUNC   (1U << 3)
#define FAT_O_APPEND  (1U << 4)

typedef struct {
    fat_block_device_t device;
    uint64_t partition_lba;
} fat_mount_config_t;

typedef struct {
    uint64_t size;
    uint8_t is_directory;
    uint8_t read_only;
    uint8_t archive;
} fat_file_info_t;

typedef struct fat_fs {
    fat_block_device_t device;
    uint64_t partition_lba;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_entry_count;
    uint32_t total_sectors;

    uint32_t first_fat_sector;
    uint32_t first_data_sector;
    uint32_t root_dir_sector;
    uint32_t root_dir_sector_count;
    uint32_t root_cluster;
    uint32_t cluster_count;

    uint32_t next_alloc_hint;

    uint8_t fat_type;
    uint8_t mounted;
    uint8_t read_only;

    uint8_t *sector_cache;
    uint32_t cache_sector;
    uint8_t cache_valid;
    uint8_t cache_dirty;
} fat_fs_t;

typedef struct fat_file {
    fat_fs_t *fs;

    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t current_cluster_index;

    uint64_t position;
    uint64_t size;

    uint32_t dir_entry_sector;
    uint16_t dir_entry_offset;

    uint8_t mode_flags;
    uint8_t attr;
    uint8_t open;
    uint8_t metadata_dirty;
} fat_file_t;

fat_status_t fat_mount(fat_fs_t *fs, const fat_mount_config_t *config);
fat_status_t fat_unmount(fat_fs_t *fs);
fat_status_t fat_sync(fat_fs_t *fs);

fat_status_t fat_open(fat_fs_t *fs, fat_file_t *file, const char *path, uint32_t flags);
fat_status_t fat_read(fat_file_t *file, void *buffer, size_t bytes_to_read, size_t *bytes_read);
fat_status_t fat_write(fat_file_t *file, const void *buffer, size_t bytes_to_write, size_t *bytes_written);
fat_status_t fat_seek(fat_file_t *file, int64_t offset, fat_seek_origin_t origin, uint64_t *new_position);
fat_status_t fat_flush(fat_file_t *file);
fat_status_t fat_close(fat_file_t *file);

fat_status_t fat_remove(fat_fs_t *fs, const char *path);
fat_status_t fat_stat(fat_fs_t *fs, const char *path, fat_file_info_t *info_out);

#ifdef __cplusplus
}
#endif

#endif
