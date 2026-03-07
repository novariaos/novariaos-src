#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ISO9660 Primary Volume Descriptor
typedef struct {
    uint8_t type;                    // Volume descriptor type (1 for primary)
    char identifier[5];              // "CD001"
    uint8_t version;                 // Volume descriptor version (1)
    uint8_t unused1;
    char system_id[32];              // System identifier
    char volume_id[32];              // Volume identifier
    uint8_t unused2[8];
    uint32_t volume_space_size_le;   // Volume space size (little-endian)
    uint32_t volume_space_size_be;   // Volume space size (big-endian)
    uint8_t unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_seq_number_le;
    uint16_t volume_seq_number_be;
    uint16_t logical_block_size_le;  // Logical block size
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t type_l_path_table;
    uint32_t opt_type_l_path_table;
    uint32_t type_m_path_table;
    uint32_t opt_type_m_path_table;
    uint8_t root_directory_entry[34]; // Root directory entry
    char volume_set_id[128];
    char publisher_id[128];
    char preparer_id[128];
    char application_id[128];
    char copyright_file_id[37];
    char abstract_file_id[37];
    char bibliographic_file_id[37];
    char creation_date[17];
    char modification_date[17];
    char expiration_date[17];
    char effective_date[17];
    uint8_t file_structure_version;
    uint8_t unused4;
    uint8_t application_data[512];
    uint8_t reserved[653];
} __attribute__((packed)) iso9660_pvd_t;

// ISO9660 Directory Entry
typedef struct {
    uint8_t length;                  // Length of directory record
    uint8_t ext_attr_length;         // Extended attribute record length
    uint32_t extent_le;              // Location of extent (LBA) little-endian
    uint32_t extent_be;              // Location of extent (LBA) big-endian
    uint32_t size_le;                // Data length little-endian
    uint32_t size_be;                // Data length big-endian
    uint8_t date[7];                 // Recording date and time
    uint8_t flags;                   // File flags
    uint8_t file_unit_size;
    uint8_t interleave;
    uint16_t volume_sequence_le;
    uint16_t volume_sequence_be;
    uint8_t name_len;                // Length of file identifier
    // Followed by: file identifier, padding, system use
} __attribute__((packed)) iso9660_dir_entry_t;

// File flags
#define ISO_FLAG_HIDDEN     0x01
#define ISO_FLAG_DIRECTORY  0x02
#define ISO_FLAG_ASSOCIATED 0x04
#define ISO_FLAG_RECORD     0x08
#define ISO_FLAG_PROTECTION 0x10
#define ISO_FLAG_MULTIEXTENT 0x80

// Initialize ISO9660 filesystem (reads from the boot ISO)
void iso9660_init(void* iso_start, size_t iso_size);

// Find a file in the ISO filesystem
// Returns pointer to file data and sets size, or NULL if not found
const void* iso9660_find_file(const char* path, size_t* size);

// List files in a directory
void iso9660_list_dir(const char* path);

// Check if ISO9660 is initialized
bool iso9660_is_initialized(void);

// Mount ISO contents to VFS at specified path
void iso9660_mount_to_vfs(const char* mount_point, const char* iso_path);

#endif // ISO9660_H
