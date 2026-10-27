//This would be the execution of the VSIDS system for the CDCL.
#include "VSIDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static inline double slot_to_double(void* ptr) {
    double val;
    memcpy(&val, &ptr, sizeof(double));
    return val;
}

static inline void* double_to_slot(double val) {
    void* ptr;
    memcpy(&ptr, &val, sizeof(double));
    return ptr;
}

static inline Slot* vsids_get_slots(const VSIDS* vsids) {
    if (!vsids || !vsids->manager) {
        return NULL;
    }
    size_t offset;
    if (validate_handle(vsids->manager, vsids->activity_handle, &offset) != STATUS_SUCCESS) {
        return NULL;
    }
    return &vsids->manager->buffer[offset + 1];
}

static inline void vsids_set_activity(VSIDS* vsids, size_t variable, double value) {
    Slot* slots = vsids_get_slots(vsids);
    if (!slots) return;
    slots[variable].data_pointer = double_to_slot(value);
}

bool vsids_initialize_with_manager(VSIDS* vsids, MemoryManager* manager, size_t variable_count, double decay_factor) {
    if (!vsids || variable_count < 1 || decay_factor <= 0.0 || decay_factor >= 1.0) {
        return false;
    }

    bool owns_manager = false;
    if (!manager) {
        if (initialize_memory_manager(variable_count + 16, &manager) != STATUS_SUCCESS) {
            return false;
        }
        owns_manager = true;
    }

    size_t record_slots = variable_count + 1;
    void** initial_items = (void**)calloc(record_slots, sizeof(void*));
    if (!initial_items) {
        if (owns_manager) {
            destroy_memory_manager(manager);
        }
        return false;
    }

    void* zero_slot = double_to_slot(0.0);
    for (size_t i = 0; i < record_slots; ++i) {
        initial_items[i] = zero_slot;
    }

    RecordHandle handle;
    Status status = allocate_record(manager, initial_items, record_slots, &handle);
    free(initial_items);

    if (status != STATUS_SUCCESS) {
        if (owns_manager) {
            destroy_memory_manager(manager);
        }
        return false;
    }

    vsids->manager = manager;
    vsids->activity_handle = handle;
    vsids->variable_count = variable_count;
    vsids->decay_factor = decay_factor;
    vsids->increment = INITIAL_INCREMENT;
    vsids->owns_manager = owns_manager;

    return true;
}

bool vsids_initialize_with_decay(VSIDS* vsids, size_t variable_count, double decay_factor) {
    return vsids_initialize_with_manager(vsids, NULL, variable_count, decay_factor);
}

bool vsids_initialize(VSIDS* vsids, size_t variable_count) {
    return vsids_initialize_with_manager(vsids, NULL, variable_count, DEFAULT_DECAY_FACTOR);
}

void vsids_destroy(VSIDS* vsids) {
    if (!vsids) return;

    if (vsids->manager) {
        delete_record(vsids->manager, vsids->activity_handle);
        if (vsids->owns_manager) {
            destroy_memory_manager(vsids->manager);
            vsids->manager = NULL;
        }
    }

    vsids->variable_count = 0;
}

void vsids_bump_variable(VSIDS* vsids, size_t variable) {
    if (!vsids || variable < 1 || variable > vsids->variable_count) {
        return;
    }

    double current = vsids_get_activity(vsids, variable);
    current += vsids->increment;
    vsids_set_activity(vsids, variable, current);

    if (current >= RESCALE_THRESHOLD) {
        vsids_rescale_activities(vsids);
    }
}

void vsids_bump_variables(VSIDS* vsids, const size_t* variable_set, size_t set_size) {
    if (!vsids || !variable_set) {
        return;
    }

    for (size_t i = 0; i < set_size; ++i) {
        vsids_bump_variable(vsids, variable_set[i]);
    }
}

void vsids_decay_activities(VSIDS* vsids) {
    if (!vsids) return;

    vsids->increment /= vsids->decay_factor;

    if (vsids->increment >= RESCALE_THRESHOLD) {
        vsids_rescale_activities(vsids);
    }
}

void vsids_rescale_activities(VSIDS* vsids) {
    if (!vsids) return;

    Slot* slots = vsids_get_slots(vsids);
    if (!slots) return;

    for (size_t v = 1; v <= vsids->variable_count; ++v) {
        double val = slot_to_double(slots[v].data_pointer);
        val /= RESCALE_DIVISOR;
        slots[v].data_pointer = double_to_slot(val);
    }

    vsids->increment /= RESCALE_DIVISOR;
}

size_t vsids_select_variable(const VSIDS* vsids, const int* assignment) {
    if (!vsids || !assignment) {
        return NO_VARIABLE;
    }

    Slot* slots = vsids_get_slots(vsids);
    if (!slots) return NO_VARIABLE;

    size_t best_variable = NO_VARIABLE;
    double best_activity = -INFINITY;

    for (size_t v = 1; v <= vsids->variable_count; ++v) {
        if (assignment[v] == UNASSIGNED) {
            double val = slot_to_double(slots[v].data_pointer);
            if (val > best_activity) {
                best_activity = val;
                best_variable = v;
            }
        }
    }

    return best_variable;
}

double vsids_get_activity(const VSIDS* vsids, size_t variable) {
    if (!vsids || variable < 1 || variable > vsids->variable_count) {
        return 0.0;
    }

    Slot* slots = vsids_get_slots(vsids);
    if (!slots) return 0.0;

    return slot_to_double(slots[variable].data_pointer);
}
