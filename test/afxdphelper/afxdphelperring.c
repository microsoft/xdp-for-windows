#ifdef WIN32
#include <windows.h>
#include <afxdp.h>
#include <afxdp_helper_linux.h>
#else
#include <bpf/xsk.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define V_EQ(a,b) \
{ \
    uint32_t x = (uint32_t)(a); \
    uint32_t y = (uint32_t)(b); \
    if (x != y) { printf("Line:%u V_EQ failed, %u != %u\n", __LINE__, x, y); } \
}

#define verbose_printf(fmt, ...) if (verbose) printf((fmt), ##__VA_ARGS__)

uint32_t gProducer;
uint32_t gConsumer;
int verbose = 0;

void InitializeProdRing(struct xsk_ring_prod *r, int size)
{
    r->cached_prod = 0;
    r->cached_cons = 0;
    r->mask = size - 1;
    r->size = size;
    r->producer = &gProducer;
    r->consumer = &gConsumer;
    *(r->producer) = 0;
    *(r->consumer) = 0;
    r->ring = NULL;
}

void InitializeConsRing(struct xsk_ring_cons *r, int size)
{
    r->cached_prod = 0;
    r->cached_cons = 0;
    r->mask = size - 1;
    r->size = size;
    r->producer = &gProducer;
    r->consumer = &gConsumer;
    *(r->producer) = 0;
    *(r->consumer) = 0;
    r->ring = NULL;
}

void PrintProdRing(struct xsk_ring_prod *r)
{
    verbose_printf("cons:%u prod:%u \n", *r->consumer, *r->producer);
}
void PrintConsRing(struct xsk_ring_cons *r)
{
    verbose_printf("cons:%u prod:%u\n", *r->consumer, *r->producer);
}
void PrintRings(struct xsk_ring_cons *c, struct xsk_ring_prod *p)
{
    verbose_printf("[consumer] cons:%u prod:%u cached_cons:%u cached_prod:%u | "
           "[producer] cons:%u prod:%u cached_cons:%u cached_prod:%u \n",
        *c->consumer, *c->producer, c->cached_cons, c->cached_prod,
        *p->consumer, *p->producer, p->cached_cons, p->cached_prod);
}

int main(int argc, char **argv)
{
    struct xsk_ring_prod prod = { 0 };
    struct xsk_ring_cons cons = { 0 };
    uint32_t index;

    if (argc == 2 && !strcmp(argv[1], "-v")) {
        verbose = 1;
    }

    verbose_printf("\nsubmit and release\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("increment prod by 1\n");
    xsk_ring_prod__submit(&prod, 1);
    V_EQ(*prod.producer, 1);
    PrintRings(&cons, &prod);
    verbose_printf("increment cons by 1\n");
    xsk_ring_cons__release(&cons, 1);
    V_EQ(*cons.consumer, 1);
    PrintRings(&cons, &prod);
    verbose_printf("increment prod by 2\n");
    xsk_ring_prod__submit(&prod, 2);
    V_EQ(*prod.producer, 3);
    PrintRings(&cons, &prod);
    verbose_printf("increment cons by 2\n");
    xsk_ring_cons__release(&cons, 2);
    V_EQ(*cons.consumer, 3);
    PrintRings(&cons, &prod);
    verbose_printf("increment prod by 5\n");
    xsk_ring_prod__submit(&prod, 5);
    V_EQ(*prod.producer, 8);
    PrintRings(&cons, &prod);
    verbose_printf("increment cons by 5\n");
    xsk_ring_cons__release(&cons, 5);
    V_EQ(*cons.consumer, 8);
    PrintRings(&cons, &prod);

    verbose_printf("\nreserve and submit\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve and submit 1\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 1);
    V_EQ(index, 0);
    xsk_ring_prod__submit(&prod, 1);
    PrintRings(&cons, &prod);
    verbose_printf("reserve and submit 2\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    V_EQ(index, 1);
    xsk_ring_prod__submit(&prod, 2);
    PrintRings(&cons, &prod);
    verbose_printf("reserve too many\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 0);

    verbose_printf("\nreserve\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 1\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 1);
    V_EQ(index, 0);
    verbose_printf("reserve 2\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    V_EQ(index, 1);
    verbose_printf("reserve 1\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 1);
    V_EQ(index, 3);
    PrintRings(&cons, &prod);
    verbose_printf("reserve empty\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 0);
    V_EQ(index, 3); // Should not update index upon failure
    verbose_printf("submit 4 release 4\n");
    xsk_ring_prod__submit(&prod, 4);
    xsk_ring_cons__release(&cons, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 3\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 3);
    V_EQ(index, 4);
    verbose_printf("reserve leftover\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 0);
    V_EQ(index, 4);
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 1);
    V_EQ(index, 7);
    verbose_printf("submit 4 release 4\n");
    xsk_ring_prod__submit(&prod, 4);
    xsk_ring_cons__release(&cons, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 2 submit 2 release 2\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    xsk_ring_prod__submit(&prod, 2);
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 4\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 4);

    verbose_printf("\npeek and release\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 4 and submit 4\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 4);
    V_EQ(index, 0);
    xsk_ring_prod__submit(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("peek 1 and release 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    V_EQ(index, 0);
    xsk_ring_cons__release(&cons, 1);
    PrintRings(&cons, &prod);
    verbose_printf("peek 2 and release 2\n");
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 2);
    V_EQ(index, 1);
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);
    verbose_printf("peek 1 and release 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    V_EQ(index, 3);
    xsk_ring_cons__release(&cons, 1);
    verbose_printf("reserve 4 and submit 4\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 4);
    xsk_ring_prod__submit(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("peek 3 and release 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 3, &index), 3);
    V_EQ(index, 4);
    xsk_ring_cons__release(&cons, 1);
    verbose_printf("peek 3\n");
    V_EQ(xsk_ring_cons__peek(&cons, 3, &index), 1);

    verbose_printf("\npeek\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve and submit 4\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 4);
    xsk_ring_prod__submit(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("peek 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    V_EQ(index, 0);
    verbose_printf("peek 2\n");
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 2);
    V_EQ(index, 1);
    verbose_printf("peek 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    V_EQ(index, 3);
    verbose_printf("peek empty\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 3, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 4, &index), 0);
    V_EQ(index, 3); // Should not update index upon failure
    PrintRings(&cons, &prod);
    verbose_printf("release 4\n");
    xsk_ring_cons__release(&cons, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve and submit 3\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 3);
    xsk_ring_prod__submit(&prod, 3);
    PrintRings(&cons, &prod);
    verbose_printf("peek 4 leftover\n");
    V_EQ(xsk_ring_cons__peek(&cons, 4, &index), 3);
    V_EQ(index, 4);


    verbose_printf("\navail\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("avail empty\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 0);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 0);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 0);
    V_EQ(xsk_cons_nb_avail(&cons, 4), 0);
    verbose_printf("submit 3\n");
    xsk_ring_prod__submit(&prod, 3);
    PrintRings(&cons, &prod);
    verbose_printf("avail non-empty\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 1);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 3);
    V_EQ(xsk_cons_nb_avail(&cons, 4), 3);
    verbose_printf("peek 1 and release 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    xsk_ring_cons__release(&cons, 1);
    PrintRings(&cons, &prod);
    verbose_printf("avail non-empty after peek and release\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 1);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 4), 2);
    verbose_printf("submit 1\n");
    xsk_ring_prod__submit(&prod, 1);
    PrintRings(&cons, &prod);
    verbose_printf("continue avail after submit\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 1);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 2); // After submit, avail still uses cached_prod
    V_EQ(xsk_cons_nb_avail(&cons, 4), 2);
    verbose_printf("peek 2 and release 2\n");
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 2);
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);
    verbose_printf("peek 1 and release 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    xsk_ring_cons__release(&cons, 1);
    PrintRings(&cons, &prod);
    verbose_printf("submit 3\n");
    xsk_ring_prod__submit(&prod, 3);
    PrintRings(&cons, &prod);
    verbose_printf("avail\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 1);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 3);
    V_EQ(xsk_cons_nb_avail(&cons, 4), 3);
    verbose_printf("peek 1\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    PrintRings(&cons, &prod);
    verbose_printf("avail after peek\n");
    V_EQ(xsk_cons_nb_avail(&cons, 1), 1);
    V_EQ(xsk_cons_nb_avail(&cons, 2), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 3), 2);
    V_EQ(xsk_cons_nb_avail(&cons, 4), 2);

    verbose_printf("\nfree\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("free full\n");
    V_EQ(xsk_prod_nb_free(&prod, 1), 4); // free updates cached_cons if avail < thresh
    PrintRings(&cons, &prod);
    V_EQ(xsk_prod_nb_free(&prod, 2), 4);
    V_EQ(xsk_prod_nb_free(&prod, 3), 4);
    V_EQ(xsk_prod_nb_free(&prod, 4), 4);
    verbose_printf("reserve and submit 3\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 3);
    xsk_ring_prod__submit(&prod, 3);
    PrintRings(&cons, &prod);
    verbose_printf("full non-full\n");
    V_EQ(xsk_prod_nb_free(&prod, 1), 1);
    V_EQ(xsk_prod_nb_free(&prod, 2), 1);
    V_EQ(xsk_prod_nb_free(&prod, 3), 1);
    V_EQ(xsk_prod_nb_free(&prod, 4), 1);
    verbose_printf("release 2\n");
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);
    verbose_printf("continue non-full after release\n");
    V_EQ(xsk_prod_nb_free(&prod, 1), 1); // free does not update cached_cons if avail >= thresh
    V_EQ(xsk_prod_nb_free(&prod, 2), 3); // free updates cached_cons if avail < under thresh
    V_EQ(xsk_prod_nb_free(&prod, 3), 3);
    V_EQ(xsk_prod_nb_free(&prod, 4), 3);
    PrintRings(&cons, &prod);
    verbose_printf("release 1\n");
    xsk_ring_cons__release(&cons, 1);
    PrintRings(&cons, &prod);
    verbose_printf("reserve 2\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    PrintRings(&cons, &prod);
    verbose_printf("free after reserve\n");
    V_EQ(xsk_prod_nb_free(&prod, 1), 1);
    V_EQ(xsk_prod_nb_free(&prod, 2), 2);
    V_EQ(xsk_prod_nb_free(&prod, 3), 2);
    V_EQ(xsk_prod_nb_free(&prod, 4), 2);


    verbose_printf("\nwraparound\n\n");
    InitializeConsRing(&cons, 4);
    InitializeProdRing(&prod, 4);
    *cons.consumer = UINT32_MAX - 1;
    cons.cached_cons = UINT32_MAX - 1;
    cons.cached_prod = UINT32_MAX - 1;
    *prod.producer = UINT32_MAX - 1;
    prod.cached_cons = UINT32_MAX - 1;
    prod.cached_prod = UINT32_MAX - 1;
    PrintRings(&cons, &prod);
    verbose_printf("peek empty\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 3, &index), 0);
    V_EQ(xsk_ring_cons__peek(&cons, 4, &index), 0);
    PrintRings(&cons, &prod);
    verbose_printf("reserve empty\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    V_EQ(index, UINT32_MAX - 1);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    V_EQ(index, 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 0);
    PrintRings(&cons, &prod);
    verbose_printf("submit 4\n");
    xsk_ring_prod__submit(&prod, 4);
    PrintRings(&cons, &prod);
    verbose_printf("reserve full\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 0);
    PrintRings(&cons, &prod);
    verbose_printf("peek full\n");
    V_EQ(xsk_ring_cons__peek(&cons, 1, &index), 1);
    V_EQ(index, UINT32_MAX - 1);
    V_EQ(xsk_ring_cons__peek(&cons, 2, &index), 2);
    V_EQ(index, UINT32_MAX);
    V_EQ(xsk_ring_cons__peek(&cons, 3, &index), 1);
    V_EQ(index, 1);
    V_EQ(xsk_ring_cons__peek(&cons, 4, &index), 0);
    PrintRings(&cons, &prod);
    verbose_printf("release 2\n");
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);
    verbose_printf("reserve partial\n");
    V_EQ(xsk_ring_prod__reserve(&prod, 4, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 3, &index), 0);
    V_EQ(xsk_ring_prod__reserve(&prod, 2, &index), 2);
    V_EQ(index, 2);
    V_EQ(xsk_ring_prod__reserve(&prod, 1, &index), 0);
    PrintRings(&cons, &prod);
    verbose_printf("release 2\n");
    xsk_ring_cons__release(&cons, 2);
    PrintRings(&cons, &prod);

    printf("\nDone.\n");
    return 0;
}