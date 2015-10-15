#ifndef _page_h_
#define _page_h_ 1

#include "lib/kernel/hash.h"

#define PGSIZE 4096
/*Supplimental pte structure*/
typedef struct sup_pte{
	void* uaddr;    
	void* kaddr;
	struct hash_elem elem;
	bool swapped;
}sup_pte;

bool init_sup_pte(void* uaddr);

void delete_sup_pt();

void free_spte(void* uaddr);

bool set_kaddr(void* uaddr, void* kaddr);

sup_pte* get_pte(void* uaddr);


#endif //_Page_h_
