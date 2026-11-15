#ifndef ENGINE_SOURCE_MODEL2_M2OPAQUESORT_H
#define ENGINE_SOURCE_MODEL2_M2OPAQUESORT_H

// Native replacement for `M2HeapSort(&CM2Scene::SortOpaque, pass0, ...)`.
//
// The stock sort is a heap sort whose comparator walks element -> model -> shared -> data -> batch/material/
// textures on every comparison, about 10% of the render thread in a busy outdoor scene. Here each element's sort
// key is gathered once into a flat record and the records are sorted with std::sort, so the pointer chasing is
// O(n) instead of O(n log n) and the sort itself runs over contiguous memory.
//
// The key reproduces the comparator's field order exactly (type, then for geo batches: material layer, shaders,
// shared model, optimized flag, model, bone combo, blend mode, material flags, textures, texture count, batch
// pointer). Where the stock comparator is not a strict ordering (it ignores the shaders when only one of two
// elements has an effect) the key uses a consistent one, and ties are broken by the element's position so the
// result is deterministic. Ribbon, particle and any other element types are sorted per type with the stock
// comparator. WXL_M2_KEYED_SORT=0 restores the stock sort; WXL_M2_SORT_VERIFY=1 checks every result against the
// stock comparator and logs violations.

#if defined(WOWDECOMP_LINUX)

class CM2Scene;

void M2SortOpaquePass(CM2Scene* scene);

#endif  // WOWDECOMP_LINUX

#endif
