// Collection cache and current-palette copy, also compiled by the native API fixture.
#ifndef ENGINE_SOURCE_MODEL2_WXLCOLLECTIONPALETTE_INL
#define ENGINE_SOURCE_MODEL2_WXLCOLLECTIONPALETTE_INL
#include "Engine/Source/Model2/WxlBoneRemap.h"
#include "Engine/Source/Model2/WxlAttachmentId.h"

struct WxlModelBoneCache {
    const M2Data* child;
    const M2Data* parent;
    const M2CompBone* childBones;
    const M2CompBone* parentBones;
    const uint16_t* lookup;
    uint32_t childCount;
    uint32_t parentCount;
    uint32_t lookupCount;
    std::vector<uint32_t> mapping;

    bool Matches(const M2Data* a, const M2Data* b) const {
        return child == a && parent == b && childBones == a->m_bones.Data() && parentBones == b->m_bones.Data() &&
            lookup == b->m_boneIndicesById.Data() && childCount == a->m_bones.m_count && parentCount == b->m_bones.m_count &&
            lookupCount == b->m_boneIndicesById.m_count;
    }
};

static WxlModelBoneCache* BuildWxlBoneCache(const M2Data* child, const M2Data* parent) {
    std::vector<WxlBoneDescriptor> childBones, parentBones;
    childBones.reserve(child->m_bones.m_count);
    parentBones.reserve(parent->m_bones.m_count);
    auto append = [](const M2Data* data, std::vector<WxlBoneDescriptor>& output) {
        for (uint32_t i = 0; i < data->m_bones.m_count; ++i) {
            const M2CompBone& bone = data->m_bones[i];
            output.push_back({static_cast<int32_t>(bone.m_boneId), bone.m_boneNameCRC, bone.m_parentIndex});
        }
    };
    append(child, childBones);
    append(parent, parentBones);
    auto mapping = WxlBuildBoneRemap(childBones, parentBones,
        parent->m_boneIndicesById.Data(), parent->m_boneIndicesById.m_count);
    return new WxlModelBoneCache{child, parent, child->m_bones.Data(), parent->m_bones.Data(),
        parent->m_boneIndicesById.Data(), child->m_bones.m_count, parent->m_bones.m_count,
        parent->m_boneIndicesById.m_count, std::move(mapping)};
}

void CM2Model::RefreshWxlCollectionPalette() {
    CM2Model* parent = this->m_attachParent;
    if (!parent || !parent->m_shared || !parent->m_shared->m_data) return;
    if (WxlIsCollectionAttachment(this->m_attachmentId) &&
        (this->m_fFlags & 1) && (parent->m_fFlags & 1) &&
        this->m_shared && this->m_shared->m_data && this->m_boneMatrices && parent->m_boneMatrices) {
        const uint32_t childBones = this->m_shared->m_data->m_bones.m_count;
        const uint32_t parentBones = parent->m_shared->m_data->m_bones.m_count;
        // Bone order can differ even for the same race and gender. Match
        // semantic identifiers before copying; a count comparison alone
        // is not evidence of compatible skeleton layouts.
        if (childBones && childBones <= 256 && parentBones && parentBones <= 65535) {
            const M2Data* childData = this->m_shared->m_data;
            const M2Data* parentData = parent->m_shared->m_data;
            if (!this->m_wxlBoneRemap || !this->m_wxlBoneRemap->Matches(childData, parentData)) {
                delete this->m_wxlBoneRemap;
                this->m_wxlBoneRemap = nullptr;
                try {
                    this->m_wxlBoneRemap = BuildWxlBoneCache(childData, parentData);
                    const auto& map = this->m_wxlBoneRemap->mapping;
                    const uint32_t matched = static_cast<uint32_t>(std::count_if(map.begin(), map.end(),
                        [](uint32_t index) { return index != UINT32_MAX; }));
                    std::fprintf(stderr, "WarcraftXL: collection palette mapped %u/%u bones (%s -> %s)\n", matched,
                        childBones, this->m_shared->m_fileName, parent->m_shared->m_fileName);
                } catch (const std::bad_alloc&) {
                    // Retain the child's own animation if this optional cache
                    // cannot be allocated. A later animation update can retry.
                }
            }
            if (this->m_wxlBoneRemap) {
                const auto& map = this->m_wxlBoneRemap->mapping;
                for (uint32_t i = 0; i < map.size(); ++i) {
                    if (map[i] < parentBones) this->m_boneMatrices[i] = parent->m_boneMatrices[map[i]];
                }
            }
        }
    }
}
#endif
