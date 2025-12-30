// buddy_heap.c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/*
  간단한 버디 할당기 (single-threaded, 학습/임베디드용)
  - 블록 크기 = 2^order 바이트
  - order 범위: 0..MAX_ORDER (실제로 유효한 최소 order는 size_to_order에서 결정)
*/

#define MAX_ORDER   28                 // 최대 order (힙 총 크기 = 1 << MAX_ORDER)
#define HEAP_SIZE   (1ULL << MAX_ORDER)

typedef struct heap_block {
    bool used;
    uint8_t order;
    struct heap_block *next;
} heap_block;

static uint8_t heap_memory[HEAP_SIZE] __attribute__((aligned(4096)));
static heap_block *free_list[MAX_ORDER + 1];
static bool initialized = false;

/* 헬퍼: 블록 주소가 힙 내부인지 검사 */
static inline bool in_heap(void *p) {
    uintptr_t off = (uintptr_t)p - (uintptr_t)heap_memory;
    return (off < HEAP_SIZE);
}

/* 헬퍼: order -> 블록 크기(바이트) */
static inline size_t order_size(int order) {
    return (1ULL << order);
}

/* free_list 조작 헬퍼 */
static void add_free(heap_block **list, heap_block *b) {
    b->next = *list;
    *list = b;
}

static void remove_free(heap_block **list, heap_block *b) {
    heap_block **prev = list;
    while (*prev && *prev != b)
        prev = &(*prev)->next;
    if (*prev == b)
        *prev = b->next;
}

/* 초기화 */
void heap_init(void)
{
    for (int i = 0; i <= MAX_ORDER; i++)
        free_list[i] = NULL;

    // 초기 블록은 힙 전체를 차지
    heap_block *initial = (heap_block*)heap_memory;
    initial->used = false;
    initial->order = MAX_ORDER;
    initial->next = NULL;

    free_list[MAX_ORDER] = initial;
    initialized = true;
}

/* 요청 바이트 수 -> 필요한 order 계산 */
static int size_to_order(size_t user_size)
{
    // 실제 필요한 크기: 헤더 포함
    size_t needed = user_size + sizeof(heap_block);

    int order = 0;
    while (order <= MAX_ORDER && order_size(order) < needed)
        order++;

    if (order > MAX_ORDER)
        return -1; // 너무 큼

    return order;
}

void *malloc(size_t size)
{
    if (!initialized) heap_init();

    if (size == 0) return NULL;

    int order = size_to_order(size);
    if (order < 0) return NULL;

    // 빈 리스트 찾아 분할
    int i;
    for (i = order; i <= MAX_ORDER; i++) {
        if (free_list[i]) break;
    }
    if (i > MAX_ORDER) return NULL; // 메모리 부족

    // free_list[i]에서 블록 하나 꺼냄
    heap_block *block = free_list[i];
    free_list[i] = block->next;
    block->next = NULL;

    // 분할해서 원하는 order로 내린다
    while (i > order) {
        i--;
        // buddy는 현재 block 바로 다음에 위치 (2^i 바이트 떨어짐)
        uintptr_t baddr = (uintptr_t)block + order_size(i);
        if (!in_heap((void*)baddr)) {
            // 안전상 실패 처리: 분할 불가 (이론상 일어나지 않음)
            break;
        }
        heap_block *buddy = (heap_block*)baddr;
        buddy->used = false;
        buddy->order = i;
        buddy->next = NULL;
        // buddy를 해당 free_list에 추가
        add_free(&free_list[i], buddy);

        block->order = i; // 현재 블록의 크기(상태)
    }

    block->used = true;
    block->order = order;

    // 사용자에게는 헤더 뒤의 주소를 준다
    return (void*)((uint8_t*)block + sizeof(heap_block));
}

/* free */
void dfree(void *ptr)
{
    if (!initialized) heap_init();
    if (ptr == NULL) return;

    // 블록 헤더 주소 계산
    heap_block *block = (heap_block*)((uint8_t*)ptr - sizeof(heap_block));

    // 안전성 검사: 블록이 힙 안에 있어야 함
    if (!in_heap(block)) return;

    block->used = false;

    // 병합 시도
    while (block->order < MAX_ORDER) {
        size_t size = order_size(block->order);
        uintptr_t offset = (uintptr_t)block - (uintptr_t)heap_memory;
        uintptr_t buddy_offset = offset ^ size;
        if (buddy_offset >= HEAP_SIZE) break;

        heap_block *buddy = (heap_block*)(heap_memory + buddy_offset);

        // buddy가 유효한 free 블록인지 검사
        if (!in_heap(buddy) || buddy->used || buddy->order != block->order) break;

        // buddy가 free_list에 있어야만 병합 가능 (있다면 제거)
        heap_block **prev = &free_list[buddy->order];
        while (*prev && *prev != buddy)
            prev = &(*prev)->next;

        if (*prev != buddy) {
            // buddy가 free_list에 없다면 병합 불가
            break;
        }

        // buddy 제거
        *prev = buddy->next;

        // 새 블록은 더 작은 주소(둘 중 min)의 블록 시작
        if (buddy < block) block = buddy;

        block->order++;
        block->next = NULL;
        block->used = false;
    }

    // 최종 블록을 free_list에 추가
    add_free(&free_list[block->order], block);
}

