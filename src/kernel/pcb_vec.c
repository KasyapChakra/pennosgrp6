/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a vector data structure for PCBs
 * File Name:           pcb_vec.c
 * File Content:        Implementation of functions for pcb vector
 * =============================================================== */

#include "./pcb_vec.h"
#include "../util/panic.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//Vec vec_new(size_t initial_capacity, ptr_dtor_fn ele_dtor_fn)

pcb_vec_t pcb_vec_new(size_t initial_capacity, void (*pcb_dtor_fn)(pcb_t*)) {
  pcb_t** temp_ptr_array;  
  if (initial_capacity == 0) {
    temp_ptr_array = NULL;
  } else {
    temp_ptr_array = calloc(initial_capacity, sizeof(pcb_t*));
    if (temp_ptr_array == NULL) {
      panic("pcb_vec_new(): Memory allocation (calloc) failed.\n");
    }
  }

  pcb_vec_t new_vector = (pcb_vec_t){
      .pcb_ptr_array = temp_ptr_array,
      .length = 0,
      .capacity = initial_capacity,
      .pcb_dtor_fn = pcb_dtor_fn,
  };
  return new_vector;
}

void pcb_vec_clear(pcb_vec_t* self) {  
  if (self->pcb_dtor_fn != NULL) {
    int len = pcb_vec_len(self);
    for (size_t i = 0; i < len; i++) {
      self->pcb_dtor_fn(self->pcb_ptr_array[i]);
    }
  }

  self->length = 0;
}

void pcb_vec_destroy(pcb_vec_t* self) {  

  pcb_vec_clear(self);

  free(self->pcb_ptr_array);
  self->pcb_ptr_array = NULL;  
  self->capacity = 0;
}

void pcb_vec_resize(pcb_vec_t* self, size_t new_capacity) {
  // Does nothing if new_capacity <= self->length
  if (new_capacity <= pcb_vec_len(self)) {
    return;
  }

  pcb_t** temp_ptr_array = realloc(self->pcb_ptr_array, new_capacity * sizeof(pcb_t*));
  if (temp_ptr_array == NULL) {
    panic("pcb_vec_resize(): Resize (realloc) failed.\n");
  }
  self->pcb_ptr_array = temp_ptr_array;
  self->capacity = new_capacity;
}

void pcb_vec_push_back(pcb_vec_t* self, pcb_t* new_ele) {
  // Adjust vector capacity if necessary
  if (pcb_vec_capacity(self) == 0) {
    // Original capacity is 0, change to 1
    pcb_vec_resize(self, 1);
  } else if (pcb_vec_len(self) == pcb_vec_capacity(self)) {
    // At capacity, resize to double capacity
    pcb_vec_resize(self, 2 * pcb_vec_capacity(self));
  }
  self->pcb_ptr_array[pcb_vec_len(self)] = new_ele;
  self->length++;
}

pcb_t* pcb_vec_pop_back(pcb_vec_t* self) {  
  if (pcb_vec_is_empty(self)) {
    return NULL;
  }

  pcb_t* result_pcb_ptr = self->pcb_ptr_array[pcb_vec_len(self) - 1];
  self->pcb_ptr_array[pcb_vec_len(self) - 1] = NULL;  
  self->length--;
  return result_pcb_ptr;
}

pcb_t* pcb_vec_get(pcb_vec_t* self, size_t index) {
  // Index value has to be < vec_len
  if (index >= pcb_vec_len(self)) {
    panic("pcb_vec_get(): Index value out of range (>= vec_len)!\n");
  }
  return self->pcb_ptr_array[index];
}

void pcb_vec_set(pcb_vec_t* self, size_t index, pcb_t* new_ele) {
  // Index value has to be < vec_len
  if (index >= pcb_vec_len(self)) {
    panic("pcb_vec_set(): Index value out of range (>= vec_len)!\n");
  }

  // If there exists element destructor function, call it for the replaced
  // element
  if (self->pcb_dtor_fn != NULL) {
    self->pcb_dtor_fn(self->pcb_ptr_array[index]);
  }

  self->pcb_ptr_array[index] = new_ele;
}

void pcb_vec_insert(pcb_vec_t* self, size_t index, pcb_t* new_ele) {
  // Index value has to be <= vec_len
  if (index > pcb_vec_len(self)) {
    panic("pcb_vec_insert(): Index value out of range (> vec_len)!\n");
  }

  // Adjust vector capacity if necessary
  if (pcb_vec_capacity(self) == 0) {
    // Original capacity is 0, change to 1
    pcb_vec_resize(self, 1);
  } else if (pcb_vec_len(self) == pcb_vec_capacity(self)) {
    // At capacity, resize to double capacity
    pcb_vec_resize(self, 2 * pcb_vec_capacity(self));
  }

  // Shift up elements at and after the index for insert
  // Because index is size_t type, i is int type, the comparison between
  // i and index will automatically convert i into size_t type, and a
  // negative int will be converted into a very large size_t. Therefore,
  // need to make sure i >=0 for the loop.
  for (int i = pcb_vec_len(self) - 1; i >= 0 && (size_t)i >= index; i--) {
    self->pcb_ptr_array[i + 1] = self->pcb_ptr_array[i];
  }

  self->pcb_ptr_array[index] = new_ele;
  self->length++;
}

void pcb_vec_erase(pcb_vec_t* self, size_t index, bool is_destroy) {
  // Index value has to be < vec_len
  if (index >= pcb_vec_len(self)) {
    panic("pcb_vec_erase(): Index value out of range (>= vec_len)!\n");
  }

  // If there exists element destructor function, call it for the element erased
  if ((self->pcb_dtor_fn != NULL) && (is_destroy)) {
    self->pcb_dtor_fn(self->pcb_ptr_array[index]);
  }

  // Shift down elements after the index for erase
  int len_1 = pcb_vec_len(self) - 1;
  for (size_t i = index; i < len_1; i++) {
    self->pcb_ptr_array[i] = self->pcb_ptr_array[i + 1];
  }

  self->length--;
}

int pcb_vec_seek_index_by_pcb(pcb_vec_t* self, pcb_t* target_pcb_ptr) {
  if (pcb_vec_is_empty(self)) {
    return -1;
  }
  
  int index;
  for (index = 0; index < pcb_vec_len(self); index++) {
    if (self->pcb_ptr_array[index] == target_pcb_ptr) {
      return index;
    }    
  }

  return -2; // target pcb not found
}

int pcb_vec_remove_by_pcb(pcb_vec_t* self, pcb_t* target_pcb_ptr) {  
  int index = pcb_vec_seek_index_by_pcb(self, target_pcb_ptr);
  if (index < 0) {
    return -1; // fail to find the target pcb
  }

  pcb_vec_erase(self, index, false);
  return 0;
}

pcb_t* pcb_vec_seek_pcb_by_pid(pcb_vec_t* self, pid_t target_pid) {
  if (pcb_vec_is_empty(self)) {
    return NULL;
  }
  
  for (int i = 0; i < pcb_vec_len(self); i++) {
    if (thrd_pid(self->pcb_ptr_array[i]) == target_pid) {
      return self->pcb_ptr_array[i];
    }    
  }

  return NULL; // target pcb not found  

}

pcb_t* pcb_vec_seek_pcb_by_thrd(pcb_vec_t* self, pthread_t thrd) {
  if (pcb_vec_is_empty(self)) {
    return NULL;
  }
  
  for (int i = 0; i < pcb_vec_len(self); i++) {
    if (pthread_equal(thrd_handle(self->pcb_ptr_array[i]).thread, thrd)) {
      return self->pcb_ptr_array[i];
    }    
  }

  return NULL; // target pcb not found    
}

void print_pcb_vec_info(pcb_vec_t* self) {    
  dprintf(STDERR_FILENO, "============ Print PCB vector info ============\n");
  dprintf(STDERR_FILENO, "PCB vec length: %zu\n", pcb_vec_len(self));    
  dprintf(STDERR_FILENO, "~~~~~~ Now print each PCB info ~~~~~~\n");
  for (int i = 0; i < pcb_vec_len(self); i++) {
    print_pcb_info(self->pcb_ptr_array[i]);
  }
}


