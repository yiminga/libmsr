/* msr_counters.c
 *
 * 
*/

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stddef.h>
#include <assert.h>
#include "msr_core.h"
#include "memhdlr.h"
#include "msr_counters.h"
#include "cpuid.h"

//These defines are from the Architectural MSRs (Should not change between models)

#define IA32_FIXED_CTR_CTRL		(0x38D)	// Controls for fixed ctr0, 1, and 2 
#define IA32_PERF_GLOBAL_CTRL		(0x38F)	// Enables for fixed ctr0,1,and2 here
#define IA32_PERF_GLOBAL_STATUS		(0x38E)	// Overflow condition can be found here
#define IA32_PERF_GLOBAL_OVF_CTRL	(0x390)	// Can clear the overflow here
#define IA32_FIXED_CTR0			(0x309)	// (R/W) Counts Instr_Retired.Any
#define IA32_FIXED_CTR1			(0x30A)	// (R/W) Counts CPU_CLK_Unhalted.Core
#define IA32_FIXED_CTR2			(0x30B)	// (R/W) Counts CPU_CLK_Unhalted.Ref

//static struct ctr_data c0, c1, c2;
static int fixed_ctr_storage(struct ctr_data ** ctr0, struct ctr_data ** ctr1, struct ctr_data ** ctr2)
{
    static struct ctr_data c0, c1, c2;
    static int init = 1;
    if (init)
    {
        init = 0;
        init_ctr_data(&c0);
        init_ctr_data(&c1);
        init_ctr_data(&c2);
    }
    if (ctr0)
    {
        *ctr0 = &c0;
    }
    if (ctr1)
    {
        *ctr1 = &c1;
    }
    if (ctr2)
    {
        *ctr2 = &c2;
    }
    return 0;
}

void init_ctr_data(struct ctr_data * ctr)
{
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    ctr->enable = (uint64_t *) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t));
#ifdef LIBMSR_DEBUG
    fprintf(stderr, "DEBUG: note q1, ctr->enable is at %p\n", ctr->enable);
#endif
    ctr->ring_level = (uint64_t *) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t));
    ctr->anyThread  = (uint64_t *) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t));
    ctr->pmi        = (uint64_t *) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t));
    ctr->overflow   = (uint64_t *) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t));
    ctr->value      = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
}

void 
get_fixed_ctr_ctrl(struct ctr_data *ctr0, struct ctr_data *ctr1, struct ctr_data *ctr2){
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    static uint64_t ** perf_global_ctrl =  NULL;
    static uint64_t ** fixed_ctr_ctrl = NULL;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
        perf_global_ctrl = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
        fixed_ctr_ctrl = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
    }

	load_thread_batch(IA32_PERF_GLOBAL_CTRL, perf_global_ctrl, COUNTERS_CTR_DATA);
	load_thread_batch(IA32_FIXED_CTR_CTRL,   fixed_ctr_ctrl, COUNTERS_CTR_DATA);
    read_batch(COUNTERS_CTR_DATA);

	int i;
	for(i=0; i<NUM_THREADS_NEW; i++){
		if(ctr0){
			ctr0->enable[i] 	= MASK_VAL(*perf_global_ctrl[i], 32, 32);
			ctr0->ring_level[i] 	= MASK_VAL(*fixed_ctr_ctrl[i], 1,0);
			ctr0->anyThread[i]  	= MASK_VAL(*fixed_ctr_ctrl[i], 2,2);
			ctr0->pmi[i] 		= MASK_VAL(*fixed_ctr_ctrl[i], 3,3);
		}

		if(ctr1){
			ctr1->enable[i] 	= MASK_VAL(*perf_global_ctrl[i], 33, 33); 
			ctr1->ring_level[i] 	= MASK_VAL(*fixed_ctr_ctrl[i], 5,4);
			ctr1->anyThread[i] 	= MASK_VAL(*fixed_ctr_ctrl[i], 6,6);
			ctr1->pmi[i] 		= MASK_VAL(*fixed_ctr_ctrl[i], 7,7);	
		}

		if(ctr2){
			ctr2->enable[i] 	= MASK_VAL(*perf_global_ctrl[i], 34, 34);	
			ctr2->ring_level[i] 	= MASK_VAL(*fixed_ctr_ctrl[i], 9,8);
			ctr2->anyThread[i] 	= MASK_VAL(*fixed_ctr_ctrl[i], 10, 10);
			ctr2->pmi[i] 		= MASK_VAL(*fixed_ctr_ctrl[i], 11,11);
		}
	}
}

void 
set_fixed_ctr_ctrl(struct ctr_data *ctr0, struct ctr_data *ctr1, struct ctr_data *ctr2) {
    static uint64_t coresPerSocket = 0, 
                    threadsPerCore = 0, 
                    sockets        = 0;
    static uint64_t ** perf_global_ctrl = NULL,
             ** fixed_ctr_ctrl   = NULL,
             ** ctr0_zero        = NULL,
             ** ctr1_zero        = NULL,
             ** ctr2_zero        = NULL;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
        perf_global_ctrl = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
        fixed_ctr_ctrl = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
        // Limitation of batch reads, can't use 1 array for multiple items
        ctr0_zero = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
        ctr1_zero = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
        ctr2_zero = (uint64_t **) libmsr_malloc(NUM_THREADS_NEW * sizeof(uint64_t *));
    }

	int i;
	load_thread_batch( IA32_PERF_GLOBAL_CTRL, perf_global_ctrl, COUNTERS_CTR_DATA);
	load_thread_batch( IA32_FIXED_CTR_CTRL,   fixed_ctr_ctrl, COUNTERS_CTR_DATA);
    read_batch(COUNTERS_CTR_DATA);
	load_thread_batch(IA32_FIXED_CTR0, ctr0_zero, COUNTERS_DATA);
	load_thread_batch(IA32_FIXED_CTR1, ctr1_zero, COUNTERS_DATA);
	load_thread_batch(IA32_FIXED_CTR2, ctr2_zero, COUNTERS_DATA);
    //read_batch(COUNTERS_DATA);
    
	for(i=0; i<NUM_THREADS_NEW; i++){	
        *ctr0_zero[i] = 0;
        *ctr1_zero[i] = 0;
        *ctr2_zero[i] = 0;
		*perf_global_ctrl[i] =  ( *perf_global_ctrl[i] & ~(1ULL<<32) ) | ctr0->enable[i] << 32; 
		*perf_global_ctrl[i] =  ( *perf_global_ctrl[i] & ~(1ULL<<33) ) | ctr1->enable[i] << 33; 
		*perf_global_ctrl[i] =  ( *perf_global_ctrl[i] & ~(1ULL<<34) ) | ctr2->enable[i] << 34; 
        fprintf(stderr, "qq1: perf_global_ctrl for dev %d is at %p, has 0x%lx\n", i, perf_global_ctrl[i], *perf_global_ctrl[i]);
        fprintf(stderr, "qq1: perf_global_ctrl for dev %d is at %p, has 0x%lx\n", i, perf_global_ctrl[i], *perf_global_ctrl[i]);

		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(3ULL<<0))) | (ctr0->ring_level[i] << 0);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<2))) | (ctr0->anyThread[i] << 2);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<3))) | (ctr0->pmi[i] << 3);

		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(3ULL<<4))) | (ctr1->ring_level[i] << 4);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<6))) | (ctr1->anyThread[i] << 6);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<7))) | (ctr1->pmi[i] << 7);

		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(3ULL<<8)))  | (ctr2->ring_level[i] << 8);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<10))) | (ctr2->anyThread[i] << 10);
		*fixed_ctr_ctrl[i] = (*fixed_ctr_ctrl[i] & (~(1ULL<<11))) | (ctr2->pmi[i] << 11);
	}
    write_batch(COUNTERS_CTR_DATA);
    write_batch(COUNTERS_DATA);
}

void 
get_fixed_counter_data(struct fixed_counter_data *data)
{
	data->num_counters = cpuid_num_fixed_perf_counters();
	data->width = cpuid_width_fixed_perf_counters();
}

/* calculate difference between counters now, and last time of general sample
void delta_fixed_ctr_values()
{
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    int thread_idx;
    for (thread_idx = 0; thread_idx < NUM_THREADS_NEW; thread_idx++)
    {
        ctr0->old[thread_idx] = ctr0->value[thread_idx];
        ctr1->old[thread_idx] = ctr1->value[thread_idx];
        ctr2->old[thread_idx] = ctr2->value[thread_idx];
    }
    read_all_threads(IA32_FIXED_CTR0, ctr0->

}
*/

void 
get_fixed_ctr_values(struct ctr_data *ctr0, struct ctr_data *ctr1, struct ctr_data *ctr2){
    load_thread_batch(IA32_FIXED_CTR0, ctr0->value, COUNTERS_DATA);
    load_thread_batch(IA32_FIXED_CTR1, ctr1->value, COUNTERS_DATA);
    load_thread_batch(IA32_FIXED_CTR2, ctr2->value, COUNTERS_DATA);
    read_batch(COUNTERS_DATA);
}

// These four functions use the structs defined in fixed_ctr_storage.

void
enable_fixed_counters(){
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    struct ctr_data * c0, * c1, * c2;
    fixed_ctr_storage(&c0, &c1, &c2);

	int i;
	for(i=0; i<NUM_THREADS_NEW; i++){
		c0->enable[i] 		= c1->enable[i] 		= c2->enable[i] 		= 1;
		c0->ring_level[i] 	= c1->ring_level[i] 	= c2->ring_level[i] 	= 3; // usr + os	
		c0->anyThread[i] 	= c1->anyThread[i] 	    = c2->anyThread[i] 	= 1; 
		c0->pmi[i] 		    = c1->pmi[i] 		    = c2->pmi[i] 		= 0;
	}
	set_fixed_ctr_ctrl( c0, c1, c2 );	
}

void
disable_fixed_counters(){
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    struct ctr_data * c0, * c1, * c2;
    fixed_ctr_storage(&c0, &c1, &c2);

	int i;
	for(i=0; i<NUM_THREADS_NEW; i++){
		c0->enable[i] = c1->enable[i] = c2->enable[i] = 0;
		c0->ring_level[i] = c1->ring_level[i] = c2->ring_level[i] = 3; // usr + os	
		c0->anyThread[i] = c1->anyThread[i] = c2->anyThread[i] = 1; 
		c0->pmi[i] = c1->pmi[i] = c2->pmi[i] = 0;
	}
	set_fixed_ctr_ctrl( c0, c1, c2 );	
}

void
dump_fixed_terse(FILE *writeFile){
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    struct ctr_data * c0, * c1, * c2;
    fixed_ctr_storage(&c0, &c1, &c2);

	int i;
	get_fixed_ctr_values( c0, c1, c2 );
	for(i=0; i<NUM_THREADS_NEW; i++){
		fprintf(writeFile, "%lu %lu %lu ", *c0->value[i], *c1->value[i], *c2->value[i]);
	}

}

void
dump_fixed_terse_label(FILE *writeFile){
	/*
	 * 0 	Unhalted core cycles
	 * 1	Instructions retired
	 * 2	Unhalted reference cycles
	 * 3	LLC Reference
	 * 4	LLC Misses
	 * 5	Branch Instructions Retired
	 * 6	Branch Misses Retired
	 */
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }

	int i;
	for(i=0; i<NUM_THREADS_NEW; i++){
		fprintf(writeFile, "IR%02d UCC%02d URC%02d ", i, i, i);
	}
}

void dump_fixed_readable(FILE * writeFile)
{
    static uint64_t coresPerSocket = 0, threadsPerCore = 0, sockets = 0;
    if (!coresPerSocket || !threadsPerCore || !sockets)
    {
        core_config(&coresPerSocket, &threadsPerCore, &sockets, NULL);
    }
    struct ctr_data * c0, * c1, * c2;
    fixed_ctr_storage(&c0, &c1, &c2);

	int i;
	get_fixed_ctr_values( c0, c1, c2 );
	for(i=0; i<NUM_THREADS_NEW; i++){
		fprintf(writeFile, "IR%02d: %lu UCC%02d:%lu URC%02d:%lu\n", i, *c0->value[i], i, *c1->value[i], i, *c2->value[i]);
	}

}
