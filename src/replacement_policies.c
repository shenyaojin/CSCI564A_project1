//
// This file contains all of the implementations of the replacement_policy
// constructors from the replacement_policies.h file.
//
// It also contains stubs of all of the functions that are added to each
// replacement_policy struct at construction time.
//
// ============================================================================
// NOTE: It is recommended that you read the comments in the
// replacement_policies.h file for further context on what each function is
// for.
// ============================================================================
//

#include "replacement_policies.h"
#include <stdlib.h>
#include "memory_system.h"

// LRU Replacement Policy
// ============================================================================
// ============================================================================
// LRU Replacement Policy
// ============================================================================
struct lru_metadata {
    uint32_t num_sets;
    uint32_t associativity;
    // order[set_idx] 是一个大小为 associativity 的数组，
    // 其中保存了 set 内各缓存行的索引，order[set_idx][0] 为 MRU，越往后越久未使用。
    uint32_t **order;
};

/**
 * 选取要驱逐(evict)的缓存行索引：
 * 由于 order[set_idx][associativity - 1] 是该 set 中最久未使用(LRU)的缓存行，
 * 因此直接返回它即可。
 */
static uint32_t lru_eviction_index(struct replacement_policy *replacement_policy,
                                   struct cache_system *cache_system,
                                   uint32_t set_idx)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;
    // 返回最久未使用(LRU)对应的行索引
    return metadata->order[set_idx][metadata->associativity - 1];
}

/**
 * 每次访问一条缓存行，都要把其从 order 中原本的位置移到最前(MRU 位置)。
 * 步骤：
 *  1. 找到本次访问行的索引(对应 set 内的第几行)。
 *  2. 在 order[set_idx] 中找到这个索引的位置 p，将其移到 0 号位(MRU)。
 *  3. 原先 0 ~ (p-1) 号位依次后移一个单位。
 */
static void lru_cache_access(struct replacement_policy *replacement_policy,
                             struct cache_system *cache_system,
                             uint32_t set_idx,
                             uint32_t tag)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;

    // 先找到当前访问的 cache line 在 set 中的“行索引”(相对于该 set)
    uint32_t set_start = set_idx * cache_system->associativity;
    struct cache_line *set_lines = &cache_system->cache_lines[set_start];

    int accessed_line_idx = -1;
    for (uint32_t i = 0; i < cache_system->associativity; i++) {
        if (set_lines[i].tag == tag && set_lines[i].status != INVALID) {
            accessed_line_idx = i;
            break;
        }
    }

    // 如果没有找到(理论上在命中或刚放入后都会能找到)，这儿仅防御性检查
    if (accessed_line_idx < 0) {
        // 理论上不会发生，如发生代表逻辑有误
        return;
    }

    // 在 order[set_idx] 中找到 accessed_line_idx 的位置 p
    int p = -1;
    for (uint32_t i = 0; i < metadata->associativity; i++) {
        if (metadata->order[set_idx][i] == (uint32_t)accessed_line_idx) {
            p = i;
            break;
        }
    }
    if (p < 0) {
        // 防御性检查
        return;
    }

    // 将 [0...p-1] 后移一位，然后把当前行索引插到 0 位置
    for (int i = p; i > 0; i--) {
        metadata->order[set_idx][i] = metadata->order[set_idx][i - 1];
    }
    metadata->order[set_idx][0] = accessed_line_idx;
}

/**
 * LRU 策略的清理函数：释放我们在 metadata 中分配的所有内存
 */
static void lru_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_metadata *metadata = (struct lru_metadata *)replacement_policy->data;
    if (!metadata) return;

    // 先释放每个 set 的 order 数组
    for (uint32_t i = 0; i < metadata->num_sets; i++) {
        free(metadata->order[i]);
    }
    // 再释放 order 指针本身
    free(metadata->order);
    // 最后释放 metadata
    free(metadata);
    replacement_policy->data = NULL;
}

/**
 * 构造函数(constructor)：
 *  为每个 set 分配一个大小为 associativity 的数组，用来维护其 LRU 顺序。
 *  初始时，可以直接令 order[set][i] = i，表示各行的顺序是 [0, 1, 2, ..., associativity-1]。
 */
struct replacement_policy *lru_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    // 申请 replacement_policy 结构
    struct replacement_policy *policy = (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    // 申请 lru_metadata 结构
    struct lru_metadata *metadata = (struct lru_metadata *)malloc(sizeof(struct lru_metadata));
    if (!metadata) {
        free(policy);
        return NULL;
    }

    metadata->num_sets = sets;
    metadata->associativity = associativity;

    // 为 order 分配二维数组空间
    metadata->order = (uint32_t **)malloc(sizeof(uint32_t *) * sets);
    for (uint32_t s = 0; s < sets; s++) {
        metadata->order[s] = (uint32_t *)malloc(sizeof(uint32_t) * associativity);
        // 初始化为顺序 [0, 1, 2, ...]
        for (uint32_t i = 0; i < associativity; i++) {
            metadata->order[s][i] = i;
        }
    }

    // 绑定到 policy->data
    policy->data = metadata;

    // 设置三个函数指针
    policy->eviction_index = lru_eviction_index;
    policy->cache_access   = lru_cache_access;
    policy->cleanup        = lru_replacement_policy_cleanup;

    return policy;
}

// LRU_PREFER_CLEAN Replacement Policy
// ============================================================================
/**
 * LRU_PREFER_CLEAN: eviction_index
 *  1. 先从最久未使用(LRU)往前找(即从数组的尾部往头部遍历)，
 *     找到第一个 'clean(=EXCLUSIVE)' 行就返回它的索引。
 *  2. 如果没找到干净行，则返回真正 LRU 的那条(即数组尾部的那个)。
 */
static uint32_t lru_prefer_clean_eviction_index(struct replacement_policy *replacement_policy,
                                                struct cache_system *cache_system,
                                                uint32_t set_idx)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;
    uint32_t assoc = md->associativity;

    // order[set_idx] 按从MRU->LRU排，tail = associativity - 1 为最久未使用
    // 我们先遍历 tail->head, 找到第一个 status==EXCLUSIVE 的行：
    for (int i = assoc - 1; i >= 0; i--) {
        // 行索引
        uint32_t line_idx_in_set = md->order[set_idx][i];
        // 全局 cache_lines 数组中的实际下标
        uint32_t global_idx = set_idx * assoc + line_idx_in_set;
        struct cache_line *cl = &cache_system->cache_lines[global_idx];
        // 如果是 EXCLUSIVE, 那就算“干净行”
        if (cl->status == EXCLUSIVE) {
            return line_idx_in_set;
        }
    }

    // 如果找不到干净行，那么就按 LRU 规则（数组尾部那条）
    return md->order[set_idx][assoc - 1];
}

/**
 * 访问时更新顺序：和普通 LRU 完全一致
 *   找到本次访问的行索引，把它移到 order[set_idx][0]，MRU 位置
 */
static void lru_prefer_clean_cache_access(struct replacement_policy *replacement_policy,
                                          struct cache_system *cache_system,
                                          uint32_t set_idx,
                                          uint32_t tag)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;

    // 1. 找到访问的行在 set 内的“行索引”
    uint32_t set_start = set_idx * cache_system->associativity;
    struct cache_line *lines = &cache_system->cache_lines[set_start];
    int accessed_line_idx = -1;

    for (uint32_t i = 0; i < cache_system->associativity; i++) {
        if (lines[i].tag == tag && lines[i].status != INVALID) {
            accessed_line_idx = i;
            break;
        }
    }
    if (accessed_line_idx < 0) {
        // 理论上不会发生(因命中或miss插入都会能找到)，若发生可能逻辑有误
        return;
    }

    // 2. 在 order[set_idx] 中找到 accessed_line_idx 的位置 p
    int p = -1;
    for (uint32_t i = 0; i < md->associativity; i++) {
        if (md->order[set_idx][i] == (uint32_t)accessed_line_idx) {
            p = i;
            break;
        }
    }
    if (p < 0) {
        // 防御性检查
        return;
    }

    // 3. 将 [0...p-1] 整体向后移 1 位，把当前行挪到 0 位置(MRU)
    for (int i = p; i > 0; i--) {
        md->order[set_idx][i] = md->order[set_idx][i - 1];
    }
    md->order[set_idx][0] = accessed_line_idx;
}

/**
 * 析构：释放在 new 时分配的数据结构
 */
static void lru_prefer_clean_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_metadata *md = (struct lru_metadata *)replacement_policy->data;
    if (!md) return;

    // 先释放每个 set 的 order 数组
    for (uint32_t s = 0; s < md->num_sets; s++) {
        free(md->order[s]);
    }
    free(md->order);
    free(md);
    replacement_policy->data = NULL;
}

/**
 * 构造函数：和普通 LRU 一样，只是 eviction_index 函数改成 lru_prefer_clean_eviction_index
 */
struct replacement_policy *lru_prefer_clean_replacement_policy_new(uint32_t sets,
                                                                   uint32_t associativity)
{
    // 1. 分配 replacement_policy 结构
    struct replacement_policy *policy = (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    // 2. 分配 lru_metadata
    struct lru_metadata *md = (struct lru_metadata *)malloc(sizeof(struct lru_metadata));
    if (!md) {
        free(policy);
        return NULL;
    }

    md->num_sets       = sets;
    md->associativity = associativity;

    // 3. 分配二位数组 order[sets][associativity]，并初始化
    md->order = (uint32_t **)malloc(sizeof(uint32_t *) * sets);
    for (uint32_t s = 0; s < sets; s++) {
        md->order[s] = (uint32_t *)malloc(sizeof(uint32_t) * associativity);
        for (uint32_t i = 0; i < associativity; i++) {
            md->order[s][i] = i; // 初始顺序 [0, 1, 2, ..., associativity-1]
        }
    }

    policy->data = md;

    // 4. 绑定函数指针
    policy->eviction_index = lru_prefer_clean_eviction_index;
    policy->cache_access   = lru_prefer_clean_cache_access;
    policy->cleanup        = lru_prefer_clean_replacement_policy_cleanup;

    return policy;
}

// RAND Replacement Policy
// ============================================================================

// ============================================================================
// RAND Replacement Policy
// ============================================================================

// 可选：如果想把 sets、associativity 等信息放进 metadata，可以定义一个结构
// 如果你觉得没必要存储任何东西，也可以直接把 data = NULL
struct rand_metadata {
    uint32_t num_sets;
    uint32_t associativity;
};

// 选取要驱逐(evict)的缓存行索引：随机返回 [0, associativity - 1] 范围的整数
static uint32_t rand_eviction_index(struct replacement_policy *replacement_policy,
                                    struct cache_system *cache_system,
                                    uint32_t set_idx)
{
    // 如果有 rand_metadata，可在这里拿到 associativity
    struct rand_metadata *md = (struct rand_metadata *)replacement_policy->data;
    uint32_t assoc = md->associativity;

    // 直接返回 rand() % assoc
    return rand() % assoc;
}

// 访问时更新：随机策略对访问不敏感，可以什么都不做
static void rand_cache_access(struct replacement_policy *replacement_policy,
                              struct cache_system *cache_system,
                              uint32_t set_idx,
                              uint32_t tag)
{
    // Do nothing for RAND
    (void)replacement_policy; // 避免 unused parameter 的警告
    (void)cache_system;
    (void)set_idx;
    (void)tag;
}

// 清理：如果分配了 rand_metadata，就在这里 free；否则留空也行
static void rand_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    if (!replacement_policy || !replacement_policy->data) {
        return;
    }
    free(replacement_policy->data);
    replacement_policy->data = NULL;
}

// 构造函数(constructor)
struct replacement_policy *rand_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    // 1. 分配 replacement_policy 结构
    struct replacement_policy *policy = (struct replacement_policy *)malloc(sizeof(struct replacement_policy));
    if (!policy) {
        return NULL;
    }

    // 2. 如有需要，分配 rand_metadata 结构保存 sets/associativity 等信息
    struct rand_metadata *md = (struct rand_metadata *)malloc(sizeof(struct rand_metadata));
    if (!md) {
        free(policy);
        return NULL;
    }
    md->num_sets = sets;
    md->associativity = associativity;

    // 3. 将 metadata 赋值给 policy->data
    policy->data = md;

    // 4. 初始化随机种子(可选)。若想重现测试，可注释掉或改成 srand(0)
    srand(time(NULL));

    // 5. 绑定三个函数指针
    policy->eviction_index = rand_eviction_index;
    policy->cache_access   = rand_cache_access;
    policy->cleanup        = rand_replacement_policy_cleanup;

    return policy;
}