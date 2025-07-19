/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             PennOS main entry point
 * File Name:           pennos.c
 * File Content:        PennOS main program start point
 * =============================================================== */

#include <stdlib.h>

#include "./kernel/kernel_fn.h"

int main(void) {

    //pennos_init();
    pennos_kernel();

    return EXIT_SUCCESS;
}