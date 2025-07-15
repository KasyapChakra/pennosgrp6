/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a vector data structure for PCBs
 * File Name:           pcb_vec.h
 * File Content:        Header file for pcb vector
 * =============================================================== */

#ifndef PCB_VEC_H_
#define PCB_VEC_H_

#include <stdbool.h>
#include <stddef.h>  // for size_t
#include "./PCB.h"

//typedef pcb_t* ptr_t;
//typedef void (*ptr_dtor_fn)(ptr_t);

typedef struct pcb_vec_st {
  pcb_t** pcb_ptr_array;
  size_t length;
  size_t capacity;
  void (*pcb_dtor_fn)(pcb_t*);
} pcb_vec_t;


// ========================= Functions-like macros ========================= //

/* ==================================================================
 * Function-like macro: pcb_vec_capacity
 * ------------------------------------------------------------------
 * PURPOSE:           Returns the current capacity of the PCB vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * vec:               a pointer to the vector to grab the capacity of
 * ------------------------------------------------------------------
 * RETURN:            the current capacity of the PCB vector
 * =============================================================== */
#define pcb_vec_capacity(vec_ptr) ((vec_ptr)->capacity)

/* ==================================================================
 * Function-like macro: pcb_vec_len
 * ------------------------------------------------------------------
 * PURPOSE:           Returns the current length of the PCB vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * vec:               a pointer to the vector to grab the length of
 * ------------------------------------------------------------------
 * RETURN:            the current length of the PCB vector
 * =============================================================== */
#define pcb_vec_len(vec_ptr) ((vec_ptr)->length)

/* ==================================================================
 * Function-like macro: pcb_vec_is_empty
 * ------------------------------------------------------------------
 * PURPOSE:           Checks if the PCB vector is empty
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * vec:               a pointer to the vector to check emptiness of
 * ------------------------------------------------------------------
 * RETURN:            true iff the vector length is 0
 * =============================================================== */
#define pcb_vec_is_empty(vec_ptr) ((vec_ptr)->length == 0)

// =============================== Functions =============================== //

/* ==================================================================
 * FUNCTION:          pcb_vec_new
 * ==================================================================
 * PURPOSE:           Creates a new empty PCB vector with the specified
 * initial_capacity and specified function to clean up elements in the vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * initial_capacity:  the initial capacity of the newly created vector, non
 *                    negative
 *
 * ele_dtor_fn：      a function pointer to a function that takes in a
 *                    ptr_t (a vector element) and cleans it up. This
 *                    is commonly just 'free' but custom functions can be
 *                    passed in. NULL can also be passed in to specify
 *                    that there is no cleanup function that needs to
 *                    be called on each element
 * ------------------------------------------------------------------
 * RETURN:            a newly created vector with specified capacity, 0 length
 * and the specified element destructor (cleanup) function
 * ------------------------------------------------------------------
 * NOTE:              If memory allocation fails, then panic().
 * =============================================================== */
pcb_vec_t pcb_vec_new(size_t initial_capacity, void (*pcb_dtor_fn)(pcb_t*));



/* ==================================================================
 * FUNCTION:          pcb_vec_get
 * ==================================================================
 * PURPOSE:           Gets the specified element of the PCB vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector whose element to be obtained
 *
 * index              the index of the element to get
 * ------------------------------------------------------------------
 * RETURN:            the element at the specified index
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If the index is >= self->length, then panic().
 * =============================================================== */
pcb_t* pcb_vec_get(pcb_vec_t* self, size_t index);

/* ==================================================================
 * FUNCTION:          pcb_vec_set
 * ==================================================================
 * PURPOSE:           Sets the specified element of the PCB vector to the specified
 * value
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector whose element to be set
 *
 * index:             the index of the element to set
 *
 * new_ele:           the value used to set the element at that index to
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If the index is >= self->length, then panic().
 * Note:              Need to call cleanup function (if exists) for
 *                    the original element being replaced.
 * =============================================================== */
void pcb_vec_set(pcb_vec_t* self, size_t index, pcb_t* new_ele);

/* ==================================================================
 * FUNCTION:          pcb_vec_push_back
 * ==================================================================
 * PURPOSE:           Appends the given element to the end of the PCB vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be pushed onto
 *
 * new_ele:           the value to be added to the end of the container
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If a resize is needed and it fails, then panic().
 * Note:              If after the operation the new length is greater than
 *                    the old capacity, then a reallocation takes place and
 *                    all elements are copied over.
 *                    Capacity is doubled. If initial capacity is zero,
 *                    it is resized to capacity 1.
 * Note:              Any pointers to elements prior to this reallocation are
 * invalidated.
 * =============================================================== */
void pcb_vec_push_back(pcb_vec_t* self, pcb_t* new_ele);

/* ==================================================================
 * FUNCTION:          pcb_vec_pop
 * ==================================================================
 * PURPOSE:           Pop the last element of the PCB vector
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be popped
 * ------------------------------------------------------------------
 * RETURN:            PCB pointer (NULL if vector is empty)
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              The capacity of self stays the same.
 * =============================================================== */
pcb_t* pcb_vec_pop_back(pcb_vec_t* self);

/* ==================================================================
 * FUNCTION:          pcb_vec_insert
 * ==================================================================
 * PURPOSE:           Inserts an element at the specified location in the
 * container
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be inserted into
 *
 * index:             the index of the element to be inserted at
 *                    Elements at this index and after it are "shifted" up
 *                    one position. If index is equal to the length, then insert
 *                    at the end of the vector.
 *
 * new_ele:           the value to be inserted
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If the index > self->length, then panic().
 * Note:              If after the operation the new length is greater than the
 *                    old capacity, then a reallocation takes place and all
 *                    elements are copied over.
 *                    Capacity is doubled (or set to 1 if was 0).
 * Note:              Any pointers to elements prior to this reallocation are
 * invalidated.
 * =============================================================== */
void pcb_vec_insert(pcb_vec_t* self, size_t index, pcb_t* new_ele);

/* ==================================================================
 * FUNCTION:          pcb_vec_erase
 * ==================================================================
 * PURPOSE:           Erases an element at the specified valid location in the
 * container
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to erase from
 *
 * index:             the index of the element to be erased at
 *                    Elements after this index are "shifted" down one position.
 *
 * is_destroy:        if true, then destroy the erased data
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If the index is >= self->length, then panic().
 * Note:              Call cleanup function (if exists) for the erased element.
 * =============================================================== */
void pcb_vec_erase(pcb_vec_t* self, size_t index, bool is_destroy);

/* ==================================================================
 * FUNCTION:          pcb_vec_resize
 * ==================================================================
 * PURPOSE:           Resizes the container to a new specified capacity
 *                    Does nothing if new_capacity <= self->length
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be resized
 *
 * new_capacity:      the new capacity of the vector
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              If a resize takes place, then a reallocation takes place
 *                    and all elements are copied over.
 * Note:              Any pointers to elements prior to this reallocation are
 * invalidated.
 * =============================================================== */
void pcb_vec_resize(pcb_vec_t* self, size_t new_capacity);

/* ==================================================================
 * FUNCTION:          pcb_vec_clear
 * ==================================================================
 * PURPOSE:           Erases all elements from the container
 *                    After this, the length of the vector is zero
 *                    Capacity of the vector is unchanged
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be cleared
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              Call cleanup function (if exists) for all existing
 * elements.
 * =============================================================== */
void pcb_vec_clear(pcb_vec_t* self);

/* ==================================================================
 * FUNCTION:          pcb_vec_destroy
 * ==================================================================
 * PURPOSE:           Destruct the vector
 *                    All elements are destructed and storage is deallocated
 *                    Capacity and length are set to zero
 *                    Data is set to NULL
 * ------------------------------------------------------------------
 * PARAMETERS:
 *
 * self:              a pointer to the vector to be destructed
 * ------------------------------------------------------------------
 * RETURN:            void
 * ------------------------------------------------------------------
 * Note:              Assumes self points to a valid vector.
 * Note:              Call cleanup function (if exists) for all existing
 * elements. Note:              Data storage deallocated.
 * =============================================================== */
void pcb_vec_destroy(pcb_vec_t* self);

int pcb_vec_seek_index_by_pcb(pcb_vec_t* self, pcb_t* target_pcb_ptr);

int pcb_vec_remove_by_pcb(pcb_vec_t* self, pcb_t* target_pcb_ptr);

pcb_t* pcb_vec_seek_pcb_by_pid(pcb_vec_t* self, pid_t target_pid);

pcb_t* pcb_vec_seek_pcb_by_thrd(pcb_vec_t* self, pthread_t thrd);

void print_pcb_vec_info(pcb_vec_t* self);

#endif  // PCB_VEC_H_
