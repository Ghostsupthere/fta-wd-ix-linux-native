#include "WoW/Common/ComponentCore/WxlCollectionSidecar.h"
#include <cassert>
#include <cstdlib>
#include <fstream>

int main() {
    using namespace WxlCollectionSidecar;
    assert(SupportedRace(1) && SupportedRace(11) && SupportedRace(22) && SupportedRace(26));
    assert(!SupportedRace(0) && !SupportedRace(12) && !SupportedRace(21) && !SupportedRace(27));
    std::vector<Model> models;
    assert(ParseModels("DisplayID,Slot,Model,Geosets,Texture\r\n"
                       "123,Chest,collections_set,\"1001,2201\",set_tex\r\n"
                       "456,Head,collections_helm:2401,,\"quoted,texture\"\r\n", models));
    assert(models.size() == 2 && models[0].display == 123 && models[0].slot == 3);
    assert(models[0].geosets.size() == 2 && models[0].geosets[1] == 2201);
    assert(models[1].model == "collections_helm" && models[1].geosets[0] == 2401);
    assert(models[1].texture == "quoted,texture");
    std::string path;
    assert(BuildModelPath("collections_set", "Hu", "F", 0x40, "", path));
    assert(path == "Item\\ObjectComponents\\Collections\\collections_set_Hu_F.m2");
    assert(BuildModelPath("collections_set", "Hu", "F", 0x26, "Custom", path));
    assert(path == "Item\\ObjectComponents\\Custom\\collections_set\\collections_set.m2");
    assert(BuildModelPath("collections_set", "Hu", "F", 0x2, "", path));
    assert(path == "Item\\ObjectComponents\\Collections\\collections_set_Hu.m2");
    assert(!BuildModelPath(std::string(170, 'x'), "Hu", "F", 0x20, "", path));
    models[0].texturePolicy = "retail";
    assert(BuildTexturePath(models[0], "collections_set", "Hu", "F", path));
    assert(path == "Item\\ObjectComponents\\Collections\\set_tex_Hu_F.blp");
    models[0].texturePolicy = "legacy";
    assert(BuildTexturePath(models[0], "collections_set", "Hu", "F", path));
    assert(path == "Item\\ObjectComponents\\Collections\\set_tex_HuF.blp");
    uint32_t flags = 0;
    std::string folder;
    ParseIconSettings("19:19:120:Custom", flags, folder);
    assert(flags == 120 && folder == "Custom");
    models[0].texturePolicy = "dbc";
    models[0].folder = folder;
    models[0].texture = "set_tex.blp";
    assert(BuildTexturePath(models[0], "collections_set", "Hu", "F", path, flags));
    assert(path == "Item\\ObjectComponents\\Custom\\collections_set\\set_tex_Hu_F.blp");
    models[0].texture = "Glove/set_tex";
    assert(BuildTexturePath(models[0], "collections_set", "Hu", "F", path, flags));
    assert(path == "Item\\ObjectComponents\\Glove\\collections_set\\set_tex_Hu_F.blp");
    ParseIconSettings("normal_icon", flags, folder);
    assert(flags == 0 && folder.empty());
    ParseIconSettings("::8:../bad", flags, folder);
    assert(flags == 8 && folder.empty());
    const auto anchors = ParseIconSettings("53|54:11:64:Collections");
    assert(anchors.explicitAttachment[0] && anchors.attachment[0] == 53);
    assert(anchors.explicitAttachment[1] && anchors.attachment[1] == 11);
    assert(anchors.flags == 64 && anchors.folder == "Collections");
    assert(CollectionAttachment(anchors, 0, false, false) == 53);
    assert(CollectionAttachment(anchors, 1, false, false) == 11);
    assert(CollectionAttachment(anchors, 0, true, false) == 19);
    assert(CollectionAttachment(anchors, 0, false, true) == 19);
    assert(CollectionAttachment(ParseIconSettings("::64"), 0, false, false) == 19);
    assert(CollectionAttachment(ParseIconSettings("55"), 0, false, false) == 55);
    assert(CollectionAttachment(ParseIconSettings("4294967295"), 0, false, false) == UINT32_MAX);
    const auto badAnchors = ParseIconSettings("70000:12|oops:0");
    assert(!badAnchors.explicitAttachment[0] && !badAnchors.explicitAttachment[1]);
    bool collection = false, normal = false;
    AnalyzeModelList("ordinary;COLLECTIONS_body:1001", collection, normal);
    assert(collection && normal);
    collection = normal = false;
    AnalyzeModelList("custom:1001;collections_body", collection, normal);
    assert(collection && !normal);
    Model component;
    component.model = "rshoulder_plate.mdx";
    assert(ComponentAttachment(component, 3, false) == 5);
    assert(ComponentFolder(component.model, false, "", "Chest") == "Shoulder");
    component.attach = 11;
    assert(ComponentAttachment(component, 3, false) == 11);
    assert(ComponentFolder(component.model, false, "Custom", "Chest") == "Custom");
    component.attach = -1;
    component.model = "cape_artifact";
    assert(ComponentAttachment(component, 3, false) == 12);
    assert(ComponentFolder(component.model, false, "", "Chest") == "Cape");
    component.model = "collections_set";
    assert(ComponentAttachment(component, 1, true) == 6);
    assert(ComponentAttachment(component, 9, true) == 34);
    assert(ComponentAttachment(component, 10, true) == 12);
    component.model = "collections_set_glove"; component.attach = 11;
    assert(ComponentAttachment(component, 8, true) == 19);
    assert(ComponentFolder(component.model, true, "", "Glove") == "Collections");
    models[0].texture = "../invalid";
    assert(!BuildTexturePath(models[0], "collections_set", "Hu", "F", path));
    assert(!ParseModels("DisplayID,Model\n1,\"unclosed", models));
    assert(models.size() == 2);
    assert(!ParseModels("DisplayID,Texture\n1,test", models));
    assert(ParseModels("DisplayID,Model,Geosets\n7,collections_bad,\"1,99999\"\n", models));
    assert(models.empty());
    assert(ParseModels("DisplayID,Slot,Model\n1,9,collections_cape\n2,10,collections_tabard\n"
                       "3,Cape,collections_cape\n4,Tabard,collections_tabard\n", models));
    assert(models.size() == 4 && models[0].slot == 10 && models[1].slot == 9 &&
           models[2].slot == 10 && models[3].slot == 9);
    std::vector<Material> materials;
    assert(ParseMaterials("DisplayID,ModelColumn,TextureType,Texture,TargetMode\n"
                          "123,ModelName_1,0,metal,\n"
                          "123,1,2,__hide__thing,\n"
                          "123,1,3,skin,Hide\n", materials));
    assert(materials.size() == 1 && materials[0].textureType == 0 && materials[0].modelColumn == 0);
    assert(!ParseMaterials("DisplayID,Texture\n1,\"broken", materials));
    assert(materials.size() == 1);

    char temporary[] = "/tmp/wxl-sidecar-test-XXXXXX";
    const char* created = mkdtemp(temporary);
    assert(created);
    const std::filesystem::path data(created);
    std::filesystem::create_directories(data / "patch-ZB.MPQ" / "dbfilesclient");
    std::filesystem::create_directories(data / "patch-ZA.mpq" / "DBFilesClient");
    std::filesystem::create_directories(data / "ignored" / "DBFilesClient");
    { std::ofstream f(data / "patch-ZA.mpq/DBFilesClient/WXLItemDisplayModels.csv");
      f << "DisplayID,Model\n10,collections_first\n"; }
    { std::ofstream f(data / "patch-ZB.MPQ/dbfilesclient/wxlitemdisplaymodels.CSV");
      f << "DisplayID,Model\n11,collections_second\n"; }
    { std::ofstream f(data / "patch-ZB.MPQ/dbfilesclient/WXLItemDisplayModelMaterials.csv");
      f << "DisplayID,TextureType,Texture\n11,2,second\n"; }
    { std::ofstream f(data / "ignored/DBFilesClient/WXLItemDisplayModels.csv");
      f << "DisplayID,Model\n12,collections_ignored\n"; }
    models.clear(); materials.clear();
    LoadLooseSidecars(data, models, materials);
    assert(models.size() == 2 && models[0].display == 10 && models[1].display == 11);
    assert(materials.size() == 1 && materials[0].display == 11);
    std::filesystem::remove_all(data);
}
