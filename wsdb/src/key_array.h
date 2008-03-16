// Copyright (C) 2007 Codership Oy <info@codership.com>

/*!
 * @file key_array.h
 * @brief keyed variable length array utility API 
 * 
 */
#ifndef KEY_ARRAY
#define KEY_ARRAY

struct key_array {
    char                     ident;
    uint32_t                 elem_count;
    struct key_array_entry  *elems;
};
#define IDENT_key_array 'k'


/*!
 * @brief initilizes keyed array
 */
void key_array_open(struct key_array *array);

/*!
 * @brief closes keyed array use
 */
int key_array_close(struct key_array *array);

/*!
 * @brief return the element count of keyed array
 */
int key_array_get_size(struct key_array *array);

/*!
 * @brief inserts or updates a keyed array element
 *
 * @param array the keyed array
 * @param key key of the element to be inserted
 * @param data for the element
 */
int key_array_replace(
    struct key_array *array, 
    char *key, uint16_t key_len,
    char *data, uint16_t data_len
);

/*!
 * @brief deletes a keyed array element
 *
 * @param array the keyed array
 * @param key key of the element to be deleted
 */
int key_array_delete_entry(
    struct key_array *array, char *key, uint16_t key_len
);

/* handler for array element scanner */
typedef int (*key_array_fun_t)(
    void *context, char *key, char *data
);

/*!
 * @brief scans throug a keyed array calling handler for each element
 *
 * @param array the keyed array
 * @param handler handler function to be called for each element
 * @param context context pointer to be passed for handler
 */
int key_array_scan_entries(
    struct key_array *array, key_array_fun_t handler, void *context
);
#endif
