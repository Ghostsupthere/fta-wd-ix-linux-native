// Production attachment policy, shared with the native API regression fixture.
#ifndef ENGINE_SOURCE_MODERNASSETS_NATIVEEQUIPMENTSCALE_INL
#define ENGINE_SOURCE_MODERNASSETS_NATIVEEQUIPMENTSCALE_INL
void ApplyNativeEquipmentScale(CM2Model* model) {
    if (!model || !model->m_attachParent || !model->m_shared) return;
    const auto* owner = model->m_attachParent;
    const bool preview = owner->m_wxlEquipmentPreviewOwner &&
        owner->m_wxlEquipmentPreviewGeneration == NativeEquipmentPreviewGeneration();
    const bool local = preview || owner == localModel.load(std::memory_order_relaxed);
    ApplyNativeWeaponOrientation(model, local);
    if (!local) return;
    const auto kind = wxl::equipmentscale::ForAttachment(WxlAttachmentPoint(model->m_attachmentId),
        wxl::equipmentscale::ClassifyPath(model->m_shared->m_fileName, sizeof(model->m_shared->m_fileName)));
    float scale = 1.0f;
    if (kind == wxl::equipmentscale::Kind::Weapon) scale = weaponScale.load(std::memory_order_relaxed);
    else if (kind == wxl::equipmentscale::Kind::Shield) scale = shieldScale.load(std::memory_order_relaxed);
    if (scale != 1.0f) wxl::equipmentscale::Apply(&model->m_matrixF4.a0, scale);
}
#endif
