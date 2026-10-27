#include "memory.h"


static inline Status checked_add(size_t a, size_t b, size_t* out) {
    if (b > SIZE_MAX - a) {
        return STATUS_FAILURE;
    }
    if (out) *out = a + b;
    return STATUS_SUCCESS;
}

static inline Status checked_multiply(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return STATUS_FAILURE;
    }
    if (out) *out = a * b;
    return STATUS_SUCCESS;
}

static inline Status choose_growth_capacity(size_t current_capacity, size_t required_capacity, size_t* out) {
    if (required_capacity <= current_capacity) {
        if (out) *out = current_capacity;
        return STATUS_SUCCESS;
    }

    size_t candidate;
    if (current_capacity == 0) {
        candidate = 8;
    } else if (current_capacity > SIZE_MAX / 2) {
        candidate = SIZE_MAX;
    } else {
        candidate = current_capacity * 2;
    }

    size_t new_capacity = (candidate > required_capacity) ? candidate : required_capacity;
    if (new_capacity < required_capacity) {
        return STATUS_FAILURE;
    }

    if (out) *out = new_capacity;
    return STATUS_SUCCESS;
}


Status initialize_memory_manager(size_t initial_slots, MemoryManager** out_manager) {
    if (!out_manager) return STATUS_FAILURE;

    MemoryManager* manager = (MemoryManager*)calloc(1, sizeof(MemoryManager));
    if (!manager) return STATUS_FAILURE;

    size_t buffer_bytes, table_bytes;
    if (checked_multiply(initial_slots, sizeof(Slot), &buffer_bytes) != STATUS_SUCCESS ||
        checked_multiply(initial_slots, sizeof(HandleEntry), &table_bytes) != STATUS_SUCCESS) {
        free(manager);
        return STATUS_FAILURE;
    }

    if (initial_slots > 0) {
        manager->buffer = (Slot*)malloc(buffer_bytes);
        manager->handle_table = (HandleEntry*)malloc(table_bytes);

        if (!manager->buffer || !manager->handle_table) {
            free(manager->buffer);
            free(manager->handle_table);
            free(manager);
            return STATUS_FAILURE;
        }

        memset(manager->buffer, 0, buffer_bytes);
        for (size_t i = 0; i < initial_slots; ++i) {
            manager->handle_table[i].buffer_offset = INVALID_OFFSET;
            manager->handle_table[i].generation = INITIAL_GENERATION;
            manager->handle_table[i].is_active = false;
        }
    } else {
        manager->buffer = NULL;
        manager->handle_table = NULL;
    }

    manager->buffer_capacity = initial_slots;
    manager->used_slots = 0;
    manager->handle_table_capacity = initial_slots;
    manager->next_unused_handle = 0;
    manager->free_handles.items = NULL;
    manager->free_handles.length = 0;
    manager->free_handles.capacity = 0;
    manager->wasted_slots = 0;
    manager->compaction_threshold = DEFAULT_COMPACTION_THRESHOLD;

    *out_manager = manager;
    return STATUS_SUCCESS;
}

Status ensure_buffer_capacity(MemoryManager* manager, size_t required_slots) {
    if (!manager) return STATUS_FAILURE;
    if (required_slots <= manager->buffer_capacity) {
        return STATUS_SUCCESS;
    }

    size_t new_capacity;
    if (choose_growth_capacity(manager->buffer_capacity, required_slots, &new_capacity) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t alloc_bytes;
    if (checked_multiply(new_capacity, sizeof(Slot), &alloc_bytes) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    Slot* new_buffer = (Slot*)malloc(alloc_bytes);
    if (!new_buffer) {
        return STATUS_FAILURE;
    }

    if (manager->buffer_capacity > 0 && manager->buffer != NULL) {
        memcpy(new_buffer, manager->buffer, manager->buffer_capacity * sizeof(Slot));
    }
    memset(new_buffer + manager->buffer_capacity, 0, (new_capacity - manager->buffer_capacity) * sizeof(Slot));

    Slot* old_buffer = manager->buffer;
    manager->buffer = new_buffer;
    manager->buffer_capacity = new_capacity;
    free(old_buffer);

    return STATUS_SUCCESS;
}

Status ensure_handle_table_capacity(MemoryManager* manager, size_t required_entries) {
    if (!manager) return STATUS_FAILURE;
    if (required_entries <= manager->handle_table_capacity) {
        return STATUS_SUCCESS;
    }

    size_t new_capacity;
    if (choose_growth_capacity(manager->handle_table_capacity, required_entries, &new_capacity) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t alloc_bytes;
    if (checked_multiply(new_capacity, sizeof(HandleEntry), &alloc_bytes) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    HandleEntry* new_table = (HandleEntry*)malloc(alloc_bytes);
    if (!new_table) {
        return STATUS_FAILURE;
    }

    if (manager->handle_table_capacity > 0 && manager->handle_table != NULL) {
        memcpy(new_table, manager->handle_table, manager->handle_table_capacity * sizeof(HandleEntry));
    }

    for (size_t i = manager->handle_table_capacity; i < new_capacity; ++i) {
        new_table[i].buffer_offset = INVALID_OFFSET;
        new_table[i].generation = INITIAL_GENERATION;
        new_table[i].is_active = false;
    }

    HandleEntry* old_table = manager->handle_table;
    manager->handle_table = new_table;
    manager->handle_table_capacity = new_capacity;
    free(old_table);

    return STATUS_SUCCESS;
}


Status ensure_free_handle_capacity(MemoryManager* manager) {
    if (!manager) return STATUS_FAILURE;
    IntegerStack* stack = &manager->free_handles;

    if (stack->length < stack->capacity) {
        return STATUS_SUCCESS;
    }

    size_t new_capacity;
    if (stack->capacity == 0) {
        new_capacity = 8;
    } else {
        if (stack->capacity > SIZE_MAX / 2) {
            return STATUS_FAILURE;
        }
        new_capacity = stack->capacity * 2;
    }

    size_t alloc_bytes;
    if (checked_multiply(new_capacity, sizeof(size_t), &alloc_bytes) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t* new_items = (size_t*)malloc(alloc_bytes);
    if (!new_items) {
        return STATUS_FAILURE;
    }

    if (stack->length > 0 && stack->items != NULL) {
        memcpy(new_items, stack->items, stack->length * sizeof(size_t));
    }

    free(stack->items);
    stack->items = new_items;
    stack->capacity = new_capacity;

    return STATUS_SUCCESS;
}

Status push_free_handle(MemoryManager* manager, size_t index) {
    if (!manager) return STATUS_FAILURE;
    if (ensure_free_handle_capacity(manager) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    manager->free_handles.items[manager->free_handles.length] = index;
    manager->free_handles.length++;
    return STATUS_SUCCESS;
}

Status pop_free_handle(MemoryManager* manager, size_t* out_index) {
    if (!manager || manager->free_handles.length == 0) {
        return STATUS_NO_REUSABLE_HANDLE;
    }

    manager->free_handles.length--;
    if (out_index) {
        *out_index = manager->free_handles.items[manager->free_handles.length];
    }
    return STATUS_SUCCESS;
}


Status validate_handle(const MemoryManager* manager, RecordHandle handle, size_t* out_offset) {
    if (!manager) return STATUS_INVALID;
    if (handle.index >= manager->handle_table_capacity) return STATUS_INVALID;

    HandleEntry entry = manager->handle_table[handle.index];
    if (!entry.is_active) return STATUS_INVALID;
    if (entry.generation != handle.generation) return STATUS_INVALID;
    if (entry.buffer_offset == INVALID_OFFSET) return STATUS_INVALID;

    size_t offset = entry.buffer_offset;
    if (offset >= manager->used_slots) return STATUS_INVALID;
    size_t remaining_slots = manager->used_slots - offset;
    if (remaining_slots < 1) return STATUS_INVALID;

    RecordHeader header = manager->buffer[offset].record_header;
    if (header.is_deleted) return STATUS_INVALID;
    if (header.handle_index != handle.index) return STATUS_INVALID;
    if (header.generation != handle.generation) return STATUS_INVALID;
    if (header.data_size > header.data_capacity) return STATUS_INVALID;

    if (header.data_capacity > remaining_slots - 1) return STATUS_INVALID;

    if (out_offset) *out_offset = offset;
    return STATUS_SUCCESS;
}


Status allocate_record(MemoryManager* manager, void* const* data_items, size_t data_length, RecordHandle* out_handle) {
    if (!manager) return STATUS_FAILURE;
    if (data_length > 0 && data_items == NULL) return STATUS_FAILURE;

    size_t record_capacity = data_length;
    size_t required_slots;
    if (checked_add(1, record_capacity, &required_slots) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t required_end;
    if (checked_add(manager->used_slots, required_slots, &required_end) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    if (ensure_buffer_capacity(manager, required_end) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t handle_index;
    bool reusing_handle;
    uint32_t generation;

    if (manager->free_handles.length > 0) {
        handle_index = manager->free_handles.items[manager->free_handles.length - 1];
        reusing_handle = true;
        generation = manager->handle_table[handle_index].generation;
    } else {
        handle_index = manager->next_unused_handle;
        size_t needed_entries;
        if (checked_add(handle_index, 1, &needed_entries) != STATUS_SUCCESS) {
            return STATUS_FAILURE;
        }
        if (ensure_handle_table_capacity(manager, needed_entries) != STATUS_SUCCESS) {
            return STATUS_FAILURE;
        }
        reusing_handle = false;
        generation = INITIAL_GENERATION;
    }

    size_t offset = manager->used_slots;

    manager->buffer[offset].record_header.handle_index = handle_index;
    manager->buffer[offset].record_header.generation = generation;
    manager->buffer[offset].record_header.data_size = data_length;
    manager->buffer[offset].record_header.data_capacity = record_capacity;
    manager->buffer[offset].record_header.is_deleted = false;

    for (size_t i = 0; i < data_length; ++i) {
        manager->buffer[offset + 1 + i].data_pointer = data_items[i];
    }

    HandleEntry* entry = &manager->handle_table[handle_index];
    entry->buffer_offset = offset;
    entry->generation = generation;
    entry->is_active = true;

    if (reusing_handle) {
        manager->free_handles.length--; /* Discard top item from free stack */
    } else {
        manager->next_unused_handle++;
    }

    manager->used_slots = required_end;

    if (out_handle) {
        out_handle->index = handle_index;
        out_handle->generation = generation;
    }

    return STATUS_SUCCESS;
}


Status read_record(const MemoryManager* manager, RecordHandle handle, void*** out_data, size_t* out_size) {
    if (!out_data || !out_size) return STATUS_FAILURE;

    size_t offset;
    if (validate_handle(manager, handle, &offset) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    RecordHeader header = manager->buffer[offset].record_header;
    size_t size = header.data_size;

    if (size == 0) {
        *out_data = NULL;
        *out_size = 0;
        return STATUS_SUCCESS;
    }

    size_t alloc_bytes;
    if (checked_multiply(size, sizeof(void*), &alloc_bytes) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    void** output = (void**)malloc(alloc_bytes);
    if (!output) {
        return STATUS_FAILURE;
    }

    for (size_t i = 0; i < size; ++i) {
        output[i] = manager->buffer[offset + 1 + i].data_pointer;
    }

    *out_data = output;
    *out_size = size;
    return STATUS_SUCCESS;
}


Status update_record(MemoryManager* manager, RecordHandle handle, void* const* new_items, size_t new_size) {
    size_t old_offset;
    if (validate_handle(manager, handle, &old_offset) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    if (new_size > 0 && new_items == NULL) {
        return STATUS_FAILURE;
    }

    RecordHeader old_header = manager->buffer[old_offset].record_header;

    if (new_size <= old_header.data_capacity) {
        /* In-place update: no allocation required */
        for (size_t i = 0; i < new_size; ++i) {
            manager->buffer[old_offset + 1 + i].data_pointer = new_items[i];
        }
        manager->buffer[old_offset].record_header.data_size = new_size;
        return STATUS_SUCCESS;
    }

    /* Out-of-place replacement: double new size for growth headroom */
    size_t new_capacity;
    if (checked_multiply(new_size, 2, &new_capacity) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t replacement_slots;
    if (checked_add(1, new_capacity, &replacement_slots) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t new_end;
    if (checked_add(manager->used_slots, replacement_slots, &new_end) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    if (ensure_buffer_capacity(manager, new_end) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    size_t new_offset = manager->used_slots;
    size_t old_slots = 1 + old_header.data_capacity;

    /* Populate replacement record with same handle and generation */
    manager->buffer[new_offset].record_header.handle_index = handle.index;
    manager->buffer[new_offset].record_header.generation = handle.generation;
    manager->buffer[new_offset].record_header.data_size = new_size;
    manager->buffer[new_offset].record_header.data_capacity = new_capacity;
    manager->buffer[new_offset].record_header.is_deleted = false;

    for (size_t i = 0; i < new_size; ++i) {
        manager->buffer[new_offset + 1 + i].data_pointer = new_items[i];
    }

    /* Commit replacement */
    manager->buffer[old_offset].record_header.is_deleted = true;
    manager->wasted_slots += old_slots;

    manager->handle_table[handle.index].buffer_offset = new_offset;
    manager->used_slots = new_end;

    if (compaction_needed(manager)) {
        compact_and_remove_deleted(manager);
    }

    return STATUS_SUCCESS;
}


Status delete_record(MemoryManager* manager, RecordHandle handle) {
    size_t offset;
    if (validate_handle(manager, handle, &offset) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    /* Reserve free-stack capacity before altering state */
    if (ensure_free_handle_capacity(manager) != STATUS_SUCCESS) {
        return STATUS_FAILURE;
    }

    RecordHeader header = manager->buffer[offset].record_header;
    size_t old_slots = 1 + header.data_capacity;

    bool retire_index;
    uint32_t next_generation = 0;
    if (header.generation == MAX_GENERATION) {
        retire_index = true;
    } else {
        next_generation = header.generation + 1;
        retire_index = false;
    }

    /* Commit deletion */
    manager->buffer[offset].record_header.is_deleted = true;

    HandleEntry* entry = &manager->handle_table[handle.index];
    entry->is_active = false;
    entry->buffer_offset = INVALID_OFFSET;

    if (retire_index) {
        /* Permanently retired to avoid wrap-around revival */
    } else {
        entry->generation = next_generation;
        /* Guaranteed to fit without reallocation */
        manager->free_handles.items[manager->free_handles.length++] = handle.index;
    }

    manager->wasted_slots += old_slots;

    if (compaction_needed(manager)) {
        compact_and_remove_deleted(manager);
    }

    return STATUS_SUCCESS;
}


bool compaction_needed(const MemoryManager* manager) {
    if (!manager || manager->buffer_capacity == 0) {
        return false;
    }
    double wasted_ratio = (double)manager->wasted_slots / (double)manager->buffer_capacity;
    return wasted_ratio >= manager->compaction_threshold;
}

Status compact_and_remove_deleted(MemoryManager* manager) {
    if (!manager) return STATUS_FAILURE;

    size_t cursor = 0;
    while (cursor < manager->used_slots) {
        size_t remaining = manager->used_slots - cursor;
        if (remaining < 1) {
            return STATUS_INTEGRITY_FAILURE;
        }

        RecordHeader header = manager->buffer[cursor].record_header;
        if (header.data_size > header.data_capacity) {
            return STATUS_INTEGRITY_FAILURE;
        }

        if (header.data_capacity > remaining - 1) {
            return STATUS_INTEGRITY_FAILURE;
        }

        size_t record_slots = 1 + header.data_capacity;
        cursor += record_slots;
    }

    if (cursor != manager->used_slots) {
        return STATUS_INTEGRITY_FAILURE;
    }

    size_t read_cursor = 0;
    size_t write_cursor = 0;

    while (read_cursor < manager->used_slots) {
        RecordHeader header = manager->buffer[read_cursor].record_header;
        size_t record_slots = 1 + header.data_capacity;

        if (header.is_deleted) {
            read_cursor += record_slots;
            continue;
        }

        if (write_cursor != read_cursor) {
            memmove(&manager->buffer[write_cursor],
                    &manager->buffer[read_cursor],
                    record_slots * sizeof(Slot));
        }

        manager->handle_table[header.handle_index].buffer_offset = write_cursor;

        write_cursor += record_slots;
        read_cursor += record_slots;
    }

    manager->used_slots = write_cursor;
    manager->wasted_slots = 0;

    return STATUS_SUCCESS;
}

void destroy_memory_manager(MemoryManager* manager) {
    if (!manager) return;

    free(manager->buffer);
    free(manager->handle_table);
    free(manager->free_handles.items);
    free(manager);
}
