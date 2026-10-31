// Native index upload, also compiled by the geometry regression fixture.
#ifndef ENGINE_SOURCE_MODEL2_WXLSKININDEXUPLOAD_INL
#define ENGINE_SOURCE_MODEL2_WXLSKININDEXUPLOAD_INL

// VERIFY: 59c9e83f manual
// OFFSET: 0x008360A0
int32_t CM2Shared::SetIndices() {
    if (!this->m_indexPool) {
        this->m_indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, sizeof(uint16_t) * this->m_instances * this->m_skinData->m_indices.m_count, GxPoolHintBit_Unk1, this->m_fileNameWithoutPath);
        this->m_indexBuf = g_theGxDevicePtr->BufCreate(this->m_indexPool, sizeof(uint16_t), this->m_instances * this->m_skinData->m_indices.m_count, 0);

        if (!this->m_indexPool || !this->m_indexBuf) {
            return 0;
        }
    }

    if (!this->m_indexBuf->m_hasData || !this->m_indexBuf->m_valid) {
        uint16_t* indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(this->m_indexBuf));

        if (!indices) {
            return 0;
        }

#if defined(WOWDECOMP_LINUX)
        WxlNativeDiagnostic("native-m2: uploading skin indices for %s (%u sections, %u indices, %u instances)",
                            this->m_fileNameWithoutPath, this->m_skinData->m_skinSections.m_count,
                            this->m_skinData->m_indices.m_count, this->m_instances);
#endif

        int32_t sharedVertices = (this->m_cache->m_flags & 8) || (this->m_data->m_bones.m_count == 1 && (this->m_cache->m_flags & 0x40));
        uint32_t indexStart = 0;

        for (uint32_t i = 0; i < this->m_skinData->m_skinSections.m_count; i++) {
            M2SkinSection* section = &this->m_skinSections[i];
            uint32_t start = this->m_skinData->m_skinSections[i].m_indexStart;
            uint16_t offset = sharedVertices ? 0 : static_cast<uint16_t>(-section->m_vertexStart);

            for (uint32_t instance = 0; instance < this->m_instances; instance++) {
                for (uint32_t j = 0; j < section->m_indexCount; j++) {
                    indices[j] = offset + this->m_skinData->m_indices[start + j];
                }

                indices += section->m_indexCount;
                offset += sharedVertices ? static_cast<uint16_t>(this->m_skinData->m_vertices.m_count) : section->m_vertexCount;
            }

#if defined(WOWDECOMP_LINUX)
            section->m_indexStart = indexStart;
#else
            section->m_indexStart = static_cast<uint16_t>(indexStart);
#endif
            indexStart += this->m_instances * section->m_indexCount;
        }

        CGxBuf* buf = this->m_indexBuf;
        g_theGxDevicePtr->BufUnlock(buf, 0);
        buf->m_hasData = 1;
    }

    g_theGxDevicePtr->PrimIndexPtr(this->m_indexBuf);

    return 1;
}

#endif
