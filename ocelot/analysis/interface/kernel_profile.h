/**
 * @file kernel_profile.h
 * @author Naila Farooqui, naila@cc.gatech.edu
 * @date 2011-11-15
 * @brief API for kernel profile
 *
 */
 
#ifndef _KERNEL_PROFILE_H
#define _KERNEL_PROFILE_H

#define MSG_QUEUE "/lynx"

#define MAX_KERNEL_NAME_SIZE    256


/*-------------------------------------- DEFINITIONS -------------------------*/

typedef enum { NONE, KERNEL_RUNTIME, MEM_EFFICIENCY, INST_COUNT, EXECUTION_COUNT, BRANCH_DIVERGENCE } profile_type;

typedef struct _kernel_profile {
    
    int pid;

    profile_type type;
    union {
        unsigned long kernel_runtime;
        unsigned long memory_efficiency;
        unsigned long instruction_count;
        unsigned long branch_divergence;
    } data;
    
    char *name;
    
} kernel_profile;

#endif /* _KERNEL_PROFILE_H */


