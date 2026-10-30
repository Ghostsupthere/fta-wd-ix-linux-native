#ifndef WOW_COMMON_COMPONENTCORE_WXLCOLLECTIONSIDECAR_H
#define WOW_COMMON_COMPONENTCORE_WXLCOLLECTIONSIDECAR_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace WxlCollectionSidecar {
constexpr bool SupportedRace(uint32_t race) {
    return (race >= 1 && race <= 11) || (race >= 22 && race <= 26);
}
struct Model {
    uint32_t display = 0;
    int slot = -1;
    std::string model;
    std::string texture;
    std::string folder;
    std::string suffixPolicy;
    std::string texturePolicy;
    uint32_t flags = 0;
    int attach = -1;
    std::vector<uint16_t> geosets;
};
struct Material {
    uint32_t display = 0;
    uint32_t textureType = UINT32_MAX;
    uint32_t modelIndex = UINT32_MAX;
    int modelColumn = -1;
    std::string model;
    std::string folder;
    std::string texture;
    std::string targetMode;
};
struct IconSettings {
    uint32_t flags = 0;
    std::string folder;
    uint32_t attachment[2]{UINT32_MAX, UINT32_MAX};
    bool explicitAttachment[2]{false, false};
};
IconSettings ParseIconSettings(std::string_view icon);
void AnalyzeModelList(std::string_view list, bool& collection, bool& normal);
// Mixed rows and glove body meshes use the body root (M2 attachment 19).
uint32_t CollectionAttachment(const IconSettings& settings, uint32_t column, bool mixed, bool gloveBody);
uint32_t ComponentAttachment(const Model& entry, uint32_t nativeSlot, bool collection);
std::string ComponentFolder(std::string_view model, bool collection, std::string_view custom,
                            std::string_view slotFolder);

// Parses the model table independently of the archive layer. Returns false for
// a malformed CSV or missing required columns without modifying the result.
bool ParseModels(std::string_view csv, std::vector<Model>& result);
bool ParseMaterials(std::string_view csv, std::vector<Material>& result);
// ItemDisplayInfo Icon2 encodes two attachment fields, path flags, and an
// optional object-component folder. Malformed settings leave defaults intact.
void ParseIconSettings(std::string_view icon, uint32_t& flags, std::string& folder);
bool BuildModelPath(std::string_view stem, std::string_view race, std::string_view gender,
                    uint32_t flags, std::string_view folder, std::string& result);
// Append sidecars found under loose Data/*.mpq/DBFilesClient directories.
void LoadLooseSidecars(const std::filesystem::path& dataDirectory,
                       std::vector<Model>& models, std::vector<Material>& materials);
bool BuildTexturePath(const Model& entry, std::string_view stem, std::string_view race,
                      std::string_view gender, std::string& result, uint32_t dbcFlags = 0);
const std::vector<Model>& Models();
const std::vector<Material>& Materials();
}
#endif
