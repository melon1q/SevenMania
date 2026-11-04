#include "SkinPackage.h"

namespace sevenmania::skin {
std::optional<SkinError> SkinManager::add(LoadResult loaded) {
    if(!loaded)return loaded.error;
    const auto& id=loaded.package->manifest.id;
    if(packages_.contains(id))return SkinError{ErrorCode::DuplicateId,id,"Skin ID is already registered"};
    packages_.emplace(id,std::move(loaded.package));return {};
}
std::optional<SkinError> SkinManager::registerFile(const std::filesystem::path& file){return add(loader_.load(file));}
std::optional<SkinError> SkinManager::registerModel(SkinPackage model){return add(loader_.prepare(std::move(model)));}
std::optional<SkinError> SkinManager::select(ScreenKind screen,const std::string& id) {
    const auto p=find(id);
    if(!p)return SkinError{ErrorCode::NotFound,id,"Skin is not registered"};
    if(!p->screens.contains(screen))return SkinError{ErrorCode::InvalidScreen,id,"Skin does not support this screen; current selection retained"};
    selections_.insert_or_assign(screen,id);return {};
}
std::shared_ptr<const SkinPackage> SkinManager::find(const std::string& id) const {
    const auto i=packages_.find(id);return i==packages_.end()?nullptr:i->second;
}
std::shared_ptr<const SkinPackage> SkinManager::selected(ScreenKind screen) const {
    const auto i=selections_.find(screen);return i==selections_.end()?nullptr:find(i->second);
}
bool SkinManager::unregisterSkin(const std::string& id) {
    if(!packages_.erase(id))return false;
    std::erase_if(selections_,[&](const auto& item){return item.second==id;});return true;
}
}
