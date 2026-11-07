#include "SkinData.h"
#include <windows.h>
#include <charconv>
#include <cmath>
#include <algorithm>

namespace sevenmania::skin {
namespace {
constexpr BindingDefinition definitions[]{
    {BindingId::SongTitle,"song.title",BindingType::String,31},
    {BindingId::SongArtist,"song.artist",BindingType::String,31},
    {BindingId::SongGenre,"song.genre",BindingType::String,31},
    {BindingId::SongBpm,"song.bpm",BindingType::Real,31},
    {BindingId::ChartLevel,"chart.level",BindingType::Real,31},
    {BindingId::ChartLevelText,"chart.levelText",BindingType::String,31},
    {BindingId::ChartDifficultyColor,"chart.difficultyColor",BindingType::Integer,1},
    {BindingId::PlayCombo,"play.combo",BindingType::Integer,2},
    {BindingId::PlayScore,"play.score",BindingType::Integer,2},
    {BindingId::PlayGauge,"play.gauge",BindingType::Real,2},
    {BindingId::PlayJudgement,"play.judgement",BindingType::String,2},
    {BindingId::PlayJudgementCombo,"play.judgementCombo",BindingType::String,2},
    {BindingId::PlayPaused,"play.paused",BindingType::Boolean,2},
    {BindingId::ResultScore,"result.score",BindingType::Integer,4},
    {BindingId::ResultRank,"result.rank",BindingType::String,4},
    {BindingId::ResultClearType,"result.clearType",BindingType::String,4},
    {BindingId::ResultMaxCombo,"result.maxCombo",BindingType::Integer,4},
    {BindingId::ResultCleared,"result.cleared",BindingType::Boolean,4},
    {BindingId::PlayGaugeMode,"play.gaugeMode",BindingType::String,2},
    {BindingId::SelectSpeedText,"select.speedText",BindingType::String,1},
    {BindingId::SelectJudgementText,"select.judgementText",BindingType::String,1},
    {BindingId::SelectGaugeMode,"select.gaugeMode",BindingType::String,1},
    {BindingId::PlayJudgementGrade,"play.judgementGrade",BindingType::Integer,2},
    {BindingId::PlayJudgementAge,"play.judgementAge",BindingType::Real,2},
    {BindingId::PlayJudgementProfile,"play.judgementProfile",BindingType::String,2},
    {BindingId::SelectRecordBestScore,"select.record.bestScore",BindingType::String,1},
    {BindingId::SelectRecordBestRank,"select.record.bestRank",BindingType::String,1},
    {BindingId::SelectRecordClearType,"select.record.clearType",BindingType::String,1},
    {BindingId::SelectRecordMaxCombo,"select.record.maxCombo",BindingType::String,1},
    {BindingId::InfoNormalNotes,"songinformation.normalNotes",BindingType::Integer,8},
    {BindingId::InfoLongNotes,"songinformation.longNotes",BindingType::Integer,8},
    {BindingId::InfoDuration,"songinformation.duration",BindingType::Real,8},
    {BindingId::SongSubtitle,"song.subtitle",BindingType::String,16},
    {BindingId::SummarySpeed,"summary.speed",BindingType::String,32},
    {BindingId::SummaryGauge,"summary.gauge",BindingType::String,32},
    {BindingId::SummaryAccuracy,"summary.accuracy",BindingType::String,32},
    {BindingId::SummaryVolume,"summary.volume",BindingType::String,32},

};
static_assert(std::size(definitions)==static_cast<std::size_t>(BindingId::Count));
static_assert([]{for(std::size_t i=0;i<std::size(definitions);++i)if(static_cast<std::size_t>(definitions[i].id)!=i)return false;return true;}());
bool matches(BindingType type,const SkinValue& value) {
    return (type==BindingType::String&&std::holds_alternative<std::string>(value))||
        (type==BindingType::Integer&&std::holds_alternative<std::int64_t>(value))||
        (type==BindingType::Real&&std::holds_alternative<double>(value))||
        (type==BindingType::Boolean&&std::holds_alternative<bool>(value));
}
void format(const SkinValue& value,const SkinElement& element,std::string& output) {
    if(const auto* s=std::get_if<std::string>(&value)){output.assign(*s);return;}
    if(const auto* b=std::get_if<bool>(&value)){output.assign(*b?"true":"false");return;}
    char buffer[512];std::to_chars_result result{buffer,{}};
    const bool numeric=element.kind==ElementKind::Number;
    if(const auto* i=std::get_if<std::int64_t>(&value)) {
        result=std::to_chars(buffer,buffer+sizeof(buffer),*i);
        if(numeric&&element.numberFormat.decimals&&result.ec==std::errc{}) {
            *result.ptr++='.';std::fill_n(result.ptr,element.numberFormat.decimals,'0');result.ptr+=element.numberFormat.decimals;
        }
    } else if(const auto* d=std::get_if<double>(&value)) {
        if(!std::isfinite(*d)){output.assign(element.text);return;}
        result=std::to_chars(buffer,buffer+sizeof(buffer),*d,std::chars_format::fixed,numeric?element.numberFormat.decimals:2);
    } else {output.assign(element.text);return;}
    if(result.ec!=std::errc{}){output.assign(element.text);return;}
    std::string_view raw(buffer,static_cast<std::size_t>(result.ptr-buffer));
    const auto dot=raw.find('.');const auto end=dot==std::string_view::npos?raw.size():dot;
    const auto sign=raw.starts_with('-')?1u:0u;
    const std::size_t digits=end-sign;
    const std::size_t count=numeric?std::max<std::size_t>(digits,element.numberFormat.minDigits):digits;
    output.clear();if(sign)output.push_back('-');
    for(std::size_t i=0;i<count;++i) {
        if(numeric&&element.numberFormat.grouping&&i&&(count-i)%3==0)output.push_back(',');
        output.push_back(i<count-digits?'0':raw[sign+i-(count-digits)]);
    }
    if(dot!=std::string_view::npos)output.append(raw.substr(dot));
}
}
std::span<const BindingDefinition> SkinBindingRegistry::definitions() noexcept {return sevenmania::skin::definitions;}
const BindingDefinition* SkinBindingRegistry::find(std::string_view name) noexcept {
    for(const auto& d:sevenmania::skin::definitions)if(d.name==name)return &d;return nullptr;
}
const BindingDefinition* SkinBindingRegistry::find(BindingId id) noexcept {
    const auto index=static_cast<std::size_t>(id);
    return index<std::size(sevenmania::skin::definitions)?&sevenmania::skin::definitions[index]:nullptr;
}
bool SkinBindingRegistry::allowed(BindingId id,ScreenKind screen) noexcept {
    const auto* d=find(id);const unsigned s=static_cast<unsigned>(screen);
    return d&&s<6&&(d->screens&(1u<<s));
}
const SkinValue& SkinDataSnapshot::value(BindingId id) const noexcept {
    static const SkinValue absent;
    const auto i=static_cast<std::size_t>(id);return active_&&i<values_.size()?values_[i]:absent;
}
void SkinDataProvider::clear() {
    for(auto& value:snapshot_.values_)value=std::monostate{};
    snapshot_.active_=false;++snapshot_.revision_;seen_.reset();
}
void SkinDataProvider::beginFrame(ScreenKind screen) {
    if(!screenName(screen)){clear();return;}
    if(!snapshot_.active_||snapshot_.screen_!=screen){clear();snapshot_.screen_=screen;snapshot_.active_=true;}
    seen_.reset();
}
bool SkinDataProvider::set(BindingId id,const SkinValue& value) {
    const auto* d=SkinBindingRegistry::find(id);
    if(!snapshot_.active_||!d||!SkinBindingRegistry::allowed(id,snapshot_.screen_)||!matches(d->type,value))return false;
    if(const auto* real=std::get_if<double>(&value);real&&!std::isfinite(*real))return false;
    if(const auto* s=std::get_if<std::string>(&value);s) {
        if(s->size()>16384||s->find('\0')!=std::string::npos)return false;
        const auto count=s->empty()?0:MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s->data(),static_cast<int>(s->size()),nullptr,0);
        if((!s->empty()&&!count)||count>4096)return false;
    }
    const auto i=static_cast<std::size_t>(id);seen_.set(i);
    wideValid_.reset(i);
    auto& old=snapshot_.values_[i];if(old!=value){old=value;++snapshot_.revision_;}return true;
}
bool SkinDataProvider::setWide(BindingId id,std::wstring_view value) {
    const auto* d=SkinBindingRegistry::find(id);
    if(!snapshot_.active_||!d||d->type!=BindingType::String||!SkinBindingRegistry::allowed(id,snapshot_.screen_)||value.size()>4096)return false;
    const auto i=static_cast<std::size_t>(id);
    if(wideValid_[i]&&wideCache_[i]==value&&std::holds_alternative<std::string>(snapshot_.values_[i])){seen_.set(i);return true;}
    const auto length=value.empty()?0:WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!value.empty()&&!length)return false;
    std::string utf8(static_cast<std::size_t>(length),'\0');
    if(length)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),utf8.data(),length,nullptr,nullptr);
    if(!set(id,utf8))return false;wideCache_[i].assign(value);wideValid_.set(i);return true;
}
void SkinDataProvider::endFrame() {
    for(std::size_t i=0;i<snapshot_.values_.size();++i)if(!seen_[i]&&!std::holds_alternative<std::monostate>(snapshot_.values_[i])) {
        snapshot_.values_[i]=std::monostate{};++snapshot_.revision_;
    }
}
CompiledBinding SkinBindingResolver::compile(const SkinElement& e,ScreenKind screen,std::vector<BindingDiagnostic>& diagnostics) {
    if(e.binding.empty())return {};
    const auto* d=SkinBindingRegistry::find(e.binding);
    const char* message=nullptr;
    if(!d)message="Unknown binding";
    else if(!SkinBindingRegistry::allowed(d->id,screen))message="Binding belongs to a different screen";
    else if(e.kind==ElementKind::Number&&d->type!=BindingType::Integer&&d->type!=BindingType::Real)message="Number element needs a numeric binding";
    if(message){diagnostics.push_back({e.id,e.binding,message});return {BindingId::Invalid,true,screen};}
    return {d->id,true,screen};
}
void SkinBindingResolver::resolve(const SkinElement& e,CompiledBinding binding,const SkinDataSnapshot* snapshot,std::string& output) {
    if(e.numberFormat.decimals>9||e.numberFormat.minDigits>32){output.assign(e.text);return;}
    const SkinValue* value=&e.defaultValue;
    if(binding.declared&&snapshot&&snapshot->active()&&snapshot->screen()==binding.screen&&SkinBindingRegistry::allowed(binding.id,snapshot->screen())) {
        const auto& current=snapshot->value(binding.id);
        if(!std::holds_alternative<std::monostate>(current))value=&current;
    } else if(!binding.declared&&e.kind==ElementKind::Text){output.assign(e.text);return;}
    format(*value,e,output);
}
}
