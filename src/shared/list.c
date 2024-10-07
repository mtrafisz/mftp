#include "list.h"
#include "utils.h"

#include <assert.h>
#include <string.h>

list_t list_new_ex(size_t sizeof_element, allocator_t allocator) {
    return (list_t) {
        .head = NULL,
        .tail = NULL,
        .size = 0,
        .sizeof_element = sizeof_element,
        .allocator = allocator,
    };
}

void list_clear(list_t* list) {
    list_iter_t iter = list_iter(list);
    void* data;

    while ((data = list_remove(list, LIST_FRONT)) != NULL) {
        list->allocator.free(data);
    }
}

list_iter_t list_iter(const list_t* list) {
    assert(list != NULL);

    return (list_iter_t) {
        .current = list->head,
    };
}

void* list_next(list_iter_t* iter) {
    if (iter->current == NULL) {
        return NULL;
    }

    void* data = iter->current->data;
    iter->current = iter->current->next;
    return data;
}

bool list_insert(list_t* list, const void* data, int where) {
    assert(list != NULL);
    assert(data != NULL);

    if (where != LIST_FRONT && where != LIST_BACK && !in_range(where, 0, list->size)) {
        return false;
    }

    list_node_t* new_node = list->allocator.alloc(sizeof(list_node_t));
    if (new_node == NULL) {
        return false;
    }

    // NOT TAKING OWNERSHIP OF DATA (CLONING):

    // new_node->data = list->allocator.alloc(list->sizeof_element);
    // if (new_node->data == NULL) {
    //     list->allocator.free(new_node);
    //     return false;
    // }

    // memcpy(new_node->data, data, list->sizeof_element);
    
    // TAKING OWNERSHIP OF DATA:

    new_node->data = (void*)data;

    if (list->size == 0) {
        list->head = new_node;
        list->tail = new_node;
        new_node->prev = NULL;
        new_node->next = NULL;
    } else if (where == LIST_FRONT) {
        new_node->prev = NULL;
        new_node->next = list->head;
        list->head->prev = new_node;
        list->head = new_node;
    } else if (where == LIST_BACK) {
        new_node->prev = list->tail;
        new_node->next = NULL;
        list->tail->next = new_node;
        list->tail = new_node;
    } else {
        list_node_t* current = list->head;
        for (int i = 0; i < where; i++) {
            current = current->next;
        }

        new_node->prev = current->prev;
        new_node->next = current;
        current->prev->next = new_node;
        current->prev = new_node;
    }

    list->size++;
    return true;
}

void* list_remove(list_t* list, int where) {
    assert(list != NULL);

    if (list->size == 0) {
        return NULL;
    }

    if (where != LIST_FRONT && where != LIST_BACK && !in_range(where, 0, list->size - 1)) {
        return NULL;
    }

    list_node_t* node;
    if (where == LIST_FRONT || where == 0) { // Handle both special case and index 0
        node = list->head;
        list->head = node->next;
        if (list->head) {
            list->head->prev = NULL;
        } else {
            list->tail = NULL; // List is now empty
        }
    } else if (where == LIST_BACK) {
        node = list->tail;
        list->tail = node->prev;
        if (list->tail) {
            list->tail->next = NULL;
        } else {
            list->head = NULL; // List is now empty
        }
    } else {
        node = list->head;
        for (int i = 0; i < where; i++) {
            node = node->next;
        }
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            list->head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            list->tail = node->prev;
        }
    }

    void* data = node->data;
    list->allocator.free(node);
    list->size--;

    // If the list is now empty, ensure head and tail are NULL
    if (list->size == 0) {
        list->head = NULL;
        list->tail = NULL;
    }

    return data;
}

void* list_get(const list_t* list, int where) {
    assert(list != NULL);

    if (list->size == 0) {
        return NULL;
    }

    if (where != LIST_FRONT && where != LIST_BACK && !in_range(where, 0, list->size - 1)) {
        return NULL;
    }

    list_node_t* node;
    if (where == LIST_FRONT) {
        node = list->head;
    } else if (where == LIST_BACK) {
        node = list->tail;
    } else {
        node = list->head;
        for (int i = 0; i < where; i++) {
            node = node->next;
        }
    }

    return node->data;
}
