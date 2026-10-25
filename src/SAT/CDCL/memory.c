#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define INVALID SIZE_MAX

typedef struct {
    size_t handle_id;
    size_t size;
    size_t capacity;
    bool is_deleted;
} RecordHeader;

typedef struct {
    size_t *items;
    size_t length;
    size_t capacity;
} IntegerList;

typedef struct {
    void **buffer;
    size_t total_capacity;
    size_t next_free_offset;
    size_t *handle_to_offset;
    IntegerList free_handles;
    size_t next_unused_handle;
    size_t total_wasted_slots;
    double compaction_threshold_ratio;
} MemoryManager;

static void *encode_integer(size_t value)
{
    return (void *)(uintptr_t)value;
}

static size_t decode_integer(void *value)
{
    return (size_t)(uintptr_t)value;
}

static bool ensure_free_handle_capacity(IntegerList *list)
{
    if (list->length < list->capacity)
        return true;

    size_t new_capacity =
        list->capacity == 0 ? 8 : list->capacity * 2;

    if (new_capacity < list->capacity)
        return false;

    size_t *new_items =
        realloc(list->items, new_capacity * sizeof(size_t));

    if (new_items == NULL)
        return false;

    list->items = new_items;
    list->capacity = new_capacity;

    return true;
}

static bool push_free_handle(IntegerList *list, size_t handle)
{
    if (!ensure_free_handle_capacity(list))
        return false;

    list->items[list->length++] = handle;
    return true;
}

static bool pop_free_handle(IntegerList *list, size_t *handle)
{
    if (list->length == 0)
        return false;

    *handle = list->items[--list->length];
    return true;
}


static bool expand_handle_table(MemoryManager *manager,
                                size_t new_capacity)
{
    size_t old_capacity = manager->total_capacity;

    size_t *new_table =
        realloc(manager->handle_to_offset,
                new_capacity * sizeof(size_t));

    if (new_capacity > 0 && new_table == NULL)
        return false;

    manager->handle_to_offset = new_table;

    for (size_t i = old_capacity; i < new_capacity; i++)
        manager->handle_to_offset[i] = INVALID;

    return true;
}





MemoryManager *initialize_memory_manager(size_t initial_buffer_size)
{
    MemoryManager *manager = malloc(sizeof(MemoryManager));

    if (manager == NULL)
        return NULL;

    manager->total_capacity = initial_buffer_size;
    manager->next_free_offset = 0;

    manager->buffer =
        calloc(initial_buffer_size, sizeof(void *));

    manager->handle_to_offset =
        malloc(initial_buffer_size * sizeof(size_t));

    manager->free_handles.items = NULL;
    manager->free_handles.length = 0;
    manager->free_handles.capacity = 0;

    manager->next_unused_handle = 0;

    manager->total_wasted_slots = 0;
    manager->compaction_threshold_ratio = 0.30;

    if (initial_buffer_size > 0 &&
        (manager->buffer == NULL ||
         manager->handle_to_offset == NULL)) {
        free(manager->buffer);
        free(manager->handle_to_offset);
        free(manager);
        return NULL;
    }

    for (size_t i = 0; i < initial_buffer_size; i++)
        manager->handle_to_offset[i] = INVALID;

    return manager;
}

bool expand_buffer(MemoryManager *manager, size_t extra_needed)
{
    if (extra_needed > SIZE_MAX - manager->next_free_offset)
        return false;

    size_t required_capacity =
        manager->next_free_offset + extra_needed;

    size_t doubled_capacity =
        manager->total_capacity <= SIZE_MAX / 2
            ? manager->total_capacity * 2
            : SIZE_MAX;

    size_t new_capacity =
        doubled_capacity > required_capacity
            ? doubled_capacity
            : required_capacity;

    if (new_capacity < required_capacity)
        return false;

    void **new_buffer =
        realloc(manager->buffer, new_capacity * sizeof(void *));

    if (new_capacity > 0 && new_buffer == NULL)
        return false;

    for (size_t i = manager->total_capacity;
         i < new_capacity;
         i++) {
        new_buffer[i] = NULL;
    }

    manager->buffer = new_buffer;
    if (!expand_handle_table(manager, new_capacity))
        return false;

    manager->total_capacity = new_capacity;

    return true;
}




size_t allocate_record(MemoryManager *manager,
                       void **data_items,
                       size_t data_length)
{
    const size_t header_size = 4;

    if (data_length > SIZE_MAX - header_size)
        return INVALID;

    size_t required_space = header_size + data_length;

    if (required_space > SIZE_MAX - manager->next_free_offset)
        return INVALID;

    if (manager->next_free_offset + required_space >
        manager->total_capacity) {
        if (!expand_buffer(manager, required_space))
            return INVALID;
    }

    size_t assigned_handle;

    if (!pop_free_handle(&manager->free_handles,
                         &assigned_handle)) {
        if (manager->next_unused_handle >=
            manager->total_capacity) {
            size_t old_capacity = manager->total_capacity;

            if (old_capacity == SIZE_MAX)
                return INVALID;

            size_t required_capacity =
                old_capacity == 0 ? 1 : old_capacity * 2;

            if (required_capacity < old_capacity)
                return INVALID;

            if (!expand_buffer(manager, 0))
                return INVALID;


            if (manager->next_unused_handle >=
                manager->total_capacity) {
                size_t extra =
                    manager->next_unused_handle + 1 -
                    manager->next_free_offset;

                if (!expand_buffer(manager, extra))
                    return INVALID;
            }
        }

        assigned_handle = manager->next_unused_handle++;
    }

    size_t record_offset = manager->next_free_offset;

    manager->buffer[record_offset + 0] =
        encode_integer(assigned_handle);

    manager->buffer[record_offset + 1] =
        encode_integer(data_length);

    manager->buffer[record_offset + 2] =
        encode_integer(data_length);

    manager->buffer[record_offset + 3] =
        encode_integer(false);

    for (size_t i = 0; i < data_length; i++) {
        manager->buffer[record_offset + header_size + i] =
            data_items[i];
    }

    manager->handle_to_offset[assigned_handle] = record_offset;

    manager->next_free_offset += required_space;

    return assigned_handle;
}

void **read_record(MemoryManager *manager,
                   size_t handle_id,
                   size_t *out_size)
{
    if (out_size == NULL)
        return NULL;

    *out_size = 0;

    if (handle_id >= manager->next_unused_handle ||
        handle_id >= manager->total_capacity) {
        fprintf(stderr,
                "Error: Attempted to read non-existent record\n");
        return NULL;
    }

    size_t offset = manager->handle_to_offset[handle_id];

    if (offset == INVALID ||
        offset + 3 >= manager->next_free_offset ||
        manager->buffer[offset + 3] != encode_integer(false)) {
        fprintf(stderr,
                "Error: Attempted to read non-existent or deleted record\n");
        return NULL;
    }

    size_t size = decode_integer(manager->buffer[offset + 1]);
    const size_t header_size = 4;

    void **result = NULL;

    if (size > 0) {
        result = malloc(size * sizeof(void *));

        if (result == NULL)
            return NULL;

        for (size_t i = 0; i < size; i++) {
            result[i] =
                manager->buffer[offset + header_size + i];
        }
    }

    *out_size = size;

    return result;
}

bool check_compaction_needed(MemoryManager *manager)
{
    if (manager->total_capacity == 0)
        return false;

    double wasted_ratio =
        (double)manager->total_wasted_slots /
        (double)manager->total_capacity;

    return wasted_ratio >= manager->compaction_threshold_ratio;
}



void compact_and_remove_deleted(MemoryManager *manager);


bool update_record(MemoryManager *manager,
                   size_t handle_id,
                   void **new_data_items,
                   size_t new_size)
{
    if (handle_id >= manager->next_unused_handle ||
        handle_id >= manager->total_capacity) {
        fprintf(stderr,
                "Error: Attempted to update non-existent record\n");
        return false;
    }

    size_t offset = manager->handle_to_offset[handle_id];

    if (offset == INVALID ||
        offset + 3 >= manager->next_free_offset ||
        manager->buffer[offset + 3] != encode_integer(false)) {
        fprintf(stderr,
                "Error: Attempted to update non-existent or deleted record\n");
        return false;
    }

    size_t current_capacity =
        decode_integer(manager->buffer[offset + 2]);

    const size_t header_size = 4;

    if (new_size <= current_capacity) {
        manager->buffer[offset + 1] = encode_integer(new_size);

        for (size_t i = 0; i < new_size; i++) {
            manager->buffer[offset + header_size + i] =
                new_data_items[i];
        }

        return true;
    }


    manager->buffer[offset + 3] = encode_integer(true);

    manager->total_wasted_slots +=
        header_size + current_capacity;

    if (new_size > SIZE_MAX / 2)
        return false;

    size_t allocated_capacity = new_size * 2;

    if (allocated_capacity > SIZE_MAX - header_size)
        return false;

    size_t required_space = header_size + allocated_capacity;

    if (required_space > SIZE_MAX - manager->next_free_offset)
        return false;

    if (manager->next_free_offset + required_space >
        manager->total_capacity) {
        if (!expand_buffer(manager, required_space))
            return false;
    }

    size_t new_offset = manager->next_free_offset;

    manager->buffer[new_offset + 0] = encode_integer(handle_id);
    manager->buffer[new_offset + 1] = encode_integer(new_size);
    manager->buffer[new_offset + 2] =
        encode_integer(allocated_capacity);
    manager->buffer[new_offset + 3] = encode_integer(false);

    for (size_t i = 0; i < new_size; i++) {
        manager->buffer[new_offset + header_size + i] =
            new_data_items[i];
    }

    manager->handle_to_offset[handle_id] = new_offset;

    manager->next_free_offset += required_space;
    if (check_compaction_needed(manager))
        compact_and_remove_deleted(manager);

    return true;
}

bool delete_record(MemoryManager *manager, size_t handle_id)
{
    if (handle_id >= manager->next_unused_handle ||
        handle_id >= manager->total_capacity) {
        return false;
    }

    size_t offset = manager->handle_to_offset[handle_id];

    if (offset == INVALID ||
        offset + 3 >= manager->next_free_offset ||
        manager->buffer[offset + 3] != encode_integer(false)) {
        return false;
    }

    size_t capacity =
        decode_integer(manager->buffer[offset + 2]);

    const size_t header_size = 4;

    manager->buffer[offset + 3] = encode_integer(true);

    manager->total_wasted_slots += header_size + capacity;

    manager->handle_to_offset[handle_id] = INVALID;

    if (!push_free_handle(&manager->free_handles, handle_id)) {
        return false;
    }

    if (check_compaction_needed(manager))
        compact_and_remove_deleted(manager);

    return true;
}


void compact_and_remove_deleted(MemoryManager *manager)
{
    size_t read_cursor = 0;
    size_t write_cursor = 0;

    const size_t header_size = 4;

    while (read_cursor < manager->next_free_offset) {
        size_t record_handle =
            decode_integer(manager->buffer[read_cursor + 0]);

        size_t record_size =
            decode_integer(manager->buffer[read_cursor + 1]);

        size_t record_capacity =
            decode_integer(manager->buffer[read_cursor + 2]);

        bool is_deleted =
            decode_integer(manager->buffer[read_cursor + 3]) != 0;

        size_t total_record_slots =
            header_size + record_capacity;

        if (total_record_slots == 0 ||
            total_record_slots >
                manager->next_free_offset - read_cursor) {
            break;
        }

        if (is_deleted) {
            read_cursor += total_record_slots;
        } else {
            if (write_cursor != read_cursor) {
                memmove(&manager->buffer[write_cursor],
                        &manager->buffer[read_cursor],
                        total_record_slots * sizeof(void *));
                if (record_handle < manager->total_capacity) {
                    manager->handle_to_offset[record_handle] =
                        write_cursor;
                }
            }

            write_cursor += total_record_slots;
            read_cursor += total_record_slots;
        }
    }
    manager->next_free_offset = write_cursor;
    manager->total_wasted_slots = 0;
}


void destroy_memory_manager(MemoryManager *manager)
{
    if (manager == NULL)
        return;

    /*
     * Payload objects are not freed because the buffer stores
     * generic pointers and does not own the pointed-to objects.
     */
    free(manager->buffer);
    free(manager->handle_to_offset);
    free(manager->free_handles.items);
    free(manager);
}
