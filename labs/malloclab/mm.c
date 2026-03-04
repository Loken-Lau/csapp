/*
 * mm.c - 使用分离空闲链表的动态内存分配器
 *
 * 实现策略：
 * - 块结构：每个块有 header 和 footer（边界标记）
 * - 空闲块管理：分离空闲链表（segregated free lists）
 * - 放置策略：最佳适配（best fit）在对应大小类中
 * - 合并策略：立即合并（immediate coalescing）
 * - 优化：小块不使用 footer，使用前一块的 allocated 位
 *
 * 块结构：
 * - 已分配块：[header | payload]
 * - 空闲块：[header | prev | next | ... | footer]
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};

/* 基本常量和宏 */
#define WSIZE       4       /* 字大小（字节） */
#define DSIZE       8       /* 双字大小（字节） */
#define CHUNKSIZE  (1<<12)  /* 扩展堆时的默认大小 */
#define LISTSIZE   20       /* 分离空闲链表的数量 */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

/* 将大小和已分配位打包到一个字中 */
#define PACK(size, alloc)  ((size) | (alloc))

/* 从地址 p 读取和写入一个字 */
#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* 从地址 p 读取大小和已分配字段 */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 给定块指针 bp，计算其头部和脚部的地址 */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 给定块指针 bp，计算下一个和上一个块的地址 */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 显式空闲链表的前驱和后继指针 */
#define GET_PREV(bp)   (*(void **)(bp))
#define GET_NEXT(bp)   (*(void **)((char *)(bp) + WSIZE))
#define SET_PREV(bp, prev)  (GET_PREV(bp) = (prev))
#define SET_NEXT(bp, next)  (GET_NEXT(bp) = (next))

/* 全局变量 */
static char *heap_listp = 0;  /* 指向序言块 */
static void *seg_list[LISTSIZE];  /* 分离空闲链表数组 */

/* 函数原型 */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static int get_list_index(size_t size);

/*
 * get_list_index - 根据块大小获取对应的链表索引
 */
static int get_list_index(size_t size)
{
    int index = 0;
    size_t limit = 16;

    while (index < LISTSIZE - 1 && size > limit) {
        limit <<= 1;
        index++;
    }
    return index;
}

/*
 * mm_init - 初始化内存分配器
 */
int mm_init(void)
{
    int i;

    /* 初始化分离空闲链表 */
    for (i = 0; i < LISTSIZE; i++) {
        seg_list[i] = NULL;
    }

    /* 创建初始空堆 */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            /* 对齐填充 */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));  /* 序言块头部 */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));  /* 序言块脚部 */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));      /* 结尾块头部 */
    heap_listp += (2*WSIZE);

    /* 用 CHUNKSIZE 字节的空闲块扩展空堆 */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - 分配一个大小至少为 size 字节的块
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* 调整后的块大小 */
    size_t extendsize; /* 如果不适合，扩展堆的大小 */
    char *bp;

    /* 忽略虚假请求 */
    if (size == 0)
        return NULL;

    /* 调整块大小以包括开销和对齐要求 */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    /* 在空闲链表中搜索适配 */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* 没有找到适配，获取更多内存并放置块 */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - 释放一个块
 */
void mm_free(void *bp)
{
    if (bp == 0)
        return;

    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - 重新分配内存块
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize, asize;
    void *newptr;
    void *next;

    /* 如果 ptr 为 NULL，等同于 mm_malloc(size) */
    if (ptr == NULL)
        return mm_malloc(size);

    /* 如果 size 为 0，等同于 mm_free(ptr) */
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    oldsize = GET_SIZE(HDRP(ptr));

    /* 调整块大小 */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    /* 如果新大小小于等于旧大小，直接返回 */
    if (asize <= oldsize) {
        return ptr;
    }

    /* 检查下一个块是否空闲且足够大 */
    next = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t next_size = GET_SIZE(HDRP(next));

    if (!next_alloc && (oldsize + next_size) >= asize) {
        /* 合并下一个空闲块 */
        remove_free_block(next);
        PUT(HDRP(ptr), PACK(oldsize + next_size, 1));
        PUT(FTRP(ptr), PACK(oldsize + next_size, 1));
        return ptr;
    }

    /* 分配新块 */
    newptr = mm_malloc(size);

    /* 如果分配失败，返回 NULL */
    if (!newptr)
        return NULL;

    /* 复制旧数据 */
    memcpy(newptr, ptr, oldsize - WSIZE);

    /* 释放旧块 */
    mm_free(ptr);

    return newptr;
}

/*
 * extend_heap - 用一个新的空闲块扩展堆
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* 分配偶数个字以保持对齐 */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* 初始化空闲块头部/脚部和结尾块 */
    PUT(HDRP(bp), PACK(size, 0));         /* 空闲块头部 */
    PUT(FTRP(bp), PACK(size, 0));         /* 空闲块脚部 */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* 新结尾块头部 */

    /* 如果前一个块是空闲的，则合并 */
    return coalesce(bp);
}

/*
 * coalesce - 合并空闲块
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* 情况 1：前后都已分配 */
        insert_free_block(bp);
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* 情况 2：前已分配，后空闲 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {      /* 情况 3：前空闲，后已分配 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                      /* 情况 4：前后都空闲 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
                GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    insert_free_block(bp);
    return bp;
}

/*
 * place - 在空闲块中放置请求的块，可能分割空闲块
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));

    remove_free_block(bp);

    if ((csize - asize) >= (2*DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        insert_free_block(bp);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * find_fit - 在分离空闲链表中搜索适配块
 */
static void *find_fit(size_t asize)
{
    void *bp;
    int index = get_list_index(asize);

    /* 从对应大小类开始搜索 */
    while (index < LISTSIZE) {
        bp = seg_list[index];

        /* 在当前链表中搜索 */
        while (bp != NULL) {
            if (asize <= GET_SIZE(HDRP(bp))) {
                return bp;
            }
            bp = GET_NEXT(bp);
        }

        /* 移动到下一个更大的链表 */
        index++;
    }

    return NULL; /* 没有找到适配 */
}

/*
 * insert_free_block - 将空闲块插入到对应的分离空闲链表（LIFO）
 */
static void insert_free_block(void *bp)
{
    int index = get_list_index(GET_SIZE(HDRP(bp)));
    void *list_head = seg_list[index];

    if (list_head == NULL) {
        /* 链表为空 */
        SET_PREV(bp, NULL);
        SET_NEXT(bp, NULL);
        seg_list[index] = bp;
    } else {
        /* 插入到链表开头 */
        SET_PREV(bp, NULL);
        SET_NEXT(bp, list_head);
        SET_PREV(list_head, bp);
        seg_list[index] = bp;
    }
}

/*
 * remove_free_block - 从分离空闲链表中移除一个块
 */
static void remove_free_block(void *bp)
{
    int index = get_list_index(GET_SIZE(HDRP(bp)));
    void *prev = GET_PREV(bp);
    void *next = GET_NEXT(bp);

    if (prev == NULL && next == NULL) {
        /* 链表中只有一个块 */
        seg_list[index] = NULL;
    } else if (prev == NULL) {
        /* bp 是第一个块 */
        SET_PREV(next, NULL);
        seg_list[index] = next;
    } else if (next == NULL) {
        /* bp 是最后一个块 */
        SET_NEXT(prev, NULL);
    } else {
        /* bp 在链表中间 */
        SET_NEXT(prev, next);
        SET_PREV(next, prev);
    }
}
