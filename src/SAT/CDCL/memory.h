#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define INVALID_OFFSET        ((size_t)-1)
#define INITIAL_GENERATION    ((uint32_t)1)
#define MAX_GENERATION        UINT32_MAX
#define DEFAULT_COMPACTION_THRESHOLD 0.30

typedef enum Status {
    STATUS_SUCCESS              =  0,
    STATUS_FAILURE              = -1,
    STATUS_INVALID              = -2,
    STATUS_INTEGRITY_FAILURE    = -3,
    STATUS_NO_REUSABLE_HANDLE   = -4
} Status;

typedef struct RecordHeader {
    size_t   handle_index;
    uint32_t generation;
    size_t   data_size;
    size_t   data_capacity;
    bool     is_deleted;
} RecordHeader;

typedef union Slot {
    RecordHeader record_header;
    void*        data_pointer;
} Slot;

typedef struct HandleEntry {
    size_t   buffer_offset;
    uint32_t generation;
    bool     is_active;
} HandleEntry;

typedef struct IntegerStack {
    size_t* items;
    size_t  length;
    size_t  capacity;
} IntegerStack;

typedef struct RecordHandle {
    size_t   index;
    uint32_t generation;
} RecordHandle;

typedef struct MemoryManager {
    Slot*        buffer;
    size_t       buffer_capacity;
    size_t       used_slots;

    HandleEntry* handle_table;
    size_t       handle_table_capacity;
    size_t       next_unused_handle;

    IntegerStack free_handles;

    size_t       wasted_slots;
    double       compaction_threshold;
} MemoryManager;

Status initialize_memory_manager(size_t initial_slots, MemoryManager** out_manager);
Status ensure_buffer_capacity(MemoryManager* manager, size_t required_slots);
Status ensure_handle_table_capacity(MemoryManager* manager, size_t required_entries);
Status ensure_free_handle_capacity(MemoryManager* manager);
Status push_free_handle(MemoryManager* manager, size_t index);
Status pop_free_handle(MemoryManager* manager, size_t* out_index);
Status validate_handle(const MemoryManager* manager, RecordHandle handle, size_t* out_offset);
Status allocate_record(MemoryManager* manager, void* const* data_items, size_t data_length, RecordHandle* out_handle);
Status read_record(const MemoryManager* manager, RecordHandle handle, void*** out_data, size_t* out_size);
Status update_record(MemoryManager* manager, RecordHandle handle, void* const* new_items, size_t new_size);
Status delete_record(MemoryManager* manager, RecordHandle handle);
bool   compaction_needed(const MemoryManager* manager);
Status compact_and_remove_deleted(MemoryManager* manager);
void   destroy_memory_manager(MemoryManager* manager);

#endif // MEMORY_H
