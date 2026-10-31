// Production specialized-shader combo conversion, shared with the lifetime regression.
// VERIFY: 77d0f7de manual
// OFFSET: 0x00835F90
static void ConvertTextureComboEntry(TextureComboList* list, int32_t adjust, uint16_t value) {
    uint16_t first = value & 0xFF;
    uint16_t second = value >> 8;

    if (adjust) {
        first = first ? first - 1 : 0xFFFF;
        second = second ? second - 1 : 0xFFFF;
    }

    uint32_t count = list->m_count;
    int32_t last = static_cast<int32_t>(count) - 1;

    for (int32_t i = 0; i < last; i++) {
        if (list->m_data[i] == first && list->m_data[i + 1] == second) {
            return;
        }
    }

    uint32_t index = count;

    for (int32_t i = 0; i < last; i += 2) {
        if (list->m_data[i] > first || (list->m_data[i] == first && list->m_data[i + 1] > second)) {
            index = i;
            break;
        }
    }

    if (count + 2 != count) {
        list->m_count = count + 2;
    }

    memmove(&list->m_data[index + 2], &list->m_data[index], sizeof(uint16_t) * (count - index));
    list->m_data[index] = first;
    list->m_data[index + 1] = second;
}

// VERIFY: 6ef16f8f manual
// OFFSET: 0x00837250
bool CM2Shared::ConvertTextureValuesToCombos() {
    uint32_t count = this->m_skinData->m_batches.m_count;

    TextureComboList combos;
    combos.m_data = static_cast<uint16_t*>(_alloca(sizeof(uint16_t) * 2 * count));
    combos.m_count = 0;

    TextureComboList transforms;
    transforms.m_data = static_cast<uint16_t*>(_alloca(sizeof(uint16_t) * 2 * count));
    transforms.m_count = 0;

    uint32_t materialIndex = 0xFFFFFFFF;

    for (uint32_t i = 0; i < this->m_skinData->m_batches.m_count; i++) {
        M2Batch* batch = &this->m_skinData->m_batches[i];

        if (batch->m_materialIndex != materialIndex) {
            materialIndex = batch->m_materialIndex;

            if (batch->m_textureCount >= 2) {
                ConvertTextureComboEntry(&combos, 0, batch->m_textureComboIndex);
                ConvertTextureComboEntry(&transforms, 1, batch->m_textureTransformComboIndex);
            }
        }
    }

    materialIndex = 0xFFFFFFFF;

    for (uint32_t i = 0; i < this->m_skinData->m_batches.m_count; i++) {
        M2Batch* batch = &this->m_skinData->m_batches[i];

        if (batch->m_materialIndex == materialIndex) {
            continue;
        }

        materialIndex = batch->m_materialIndex;

        if (batch->m_textureCount > 1) {
            continue;
        }

        uint16_t* combo = combos.m_data;
        uint16_t* combosEnd = combos.m_data + combos.m_count;

        while (combo != combosEnd && *combo != batch->m_textureComboIndex) {
            combo++;
        }

        if (combo == combosEnd) {
            new (&combos.m_data[combos.m_count++]) uint16_t(batch->m_textureComboIndex);
        }

        uint16_t transform = batch->m_textureTransformComboIndex ? batch->m_textureTransformComboIndex - 1 : 0xFFFF;
        uint16_t* entry = transforms.m_data;
        uint16_t* transformsEnd = transforms.m_data + transforms.m_count;

        while (entry != transformsEnd && *entry != transform) {
            entry++;
        }

        if (entry == transformsEnd) {
            new (&transforms.m_data[transforms.m_count++]) uint16_t(transform);
        }
    }

#if defined(WOWDECOMP_LINUX)
    const bool growing = combos.m_count > this->m_data->m_textureCombos.m_count ||
                         transforms.m_count > this->m_data->m_textureTransformCombos.m_count;
    if (!StoreNativeM2TextureCombos(this->m_data, combos.m_data, combos.m_count,
                                    transforms.m_data, transforms.m_count)) {
        WxlNativeDiagnostic("native-m2: combo table allocation failed for %s", this->m_fileName);
        return false;
    }
    if (growing) WxlNativeDiagnostic("native-m2: grew shader tables without relocating %s (%u combos, %u transforms)",
                                    this->m_fileName, combos.m_count, transforms.m_count);
    return true;
#else
#if defined(WOWDECOMP_ARCH_64)
    uint32_t comboBytes = combos.m_count > this->m_data->m_textureCombos.m_count ? sizeof(uint16_t) * combos.m_count : 0;
    uint32_t transformBytes = transforms.m_count > this->m_data->m_textureTransformCombos.m_count ? sizeof(uint16_t) * transforms.m_count : 0;

    if (comboBytes || transformBytes) {
        uint32_t size = (this->m_fileSize + 15) & ~15u;
        M2Data* data = static_cast<M2Data*>(SIMD::AllocA16(size + comboBytes + transformBytes, __FILE__, __LINE__));
        memcpy(data, this->m_data, this->m_fileSize);
        SIMD::FreeA16(this->m_data);
        this->m_data = data;
        this->m_fileSize = size + comboBytes + transformBytes;
        uint8_t* tail = reinterpret_cast<uint8_t*>(data) + size;

        if (comboBytes) {
            data->m_textureCombos.Set(reinterpret_cast<uint16_t*>(tail));
            tail += comboBytes;
        }

        if (transformBytes) {
            data->m_textureTransformCombos.Set(reinterpret_cast<uint16_t*>(tail));
        }
    }
#else
    if (combos.m_count > this->m_data->m_textureCombos.m_count) {
        this->m_data->m_textureCombos.Set(static_cast<uint16_t*>(SMemAlloc(sizeof(uint16_t) * combos.m_count, __FILE__, __LINE__, 0)));
        this->m_data->m_flags |= 0x20;
    }

    if (transforms.m_count > this->m_data->m_textureTransformCombos.m_count) {
        this->m_data->m_textureTransformCombos.Set(static_cast<uint16_t*>(SMemAlloc(sizeof(uint16_t) * transforms.m_count, __FILE__, __LINE__, 0)));
        this->m_data->m_flags |= 0x40;
    }
#endif

    memcpy(this->m_data->m_textureCombos.Data(), combos.m_data, sizeof(uint16_t) * combos.m_count);
    this->m_data->m_textureCombos.m_count = combos.m_count;

    memcpy(this->m_data->m_textureTransformCombos.Data(), transforms.m_data, sizeof(uint16_t) * transforms.m_count);
    this->m_data->m_textureTransformCombos.m_count = transforms.m_count;
    return true;
#endif
}

