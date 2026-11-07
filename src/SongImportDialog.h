#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <memory>

namespace bms::ui {
// The native picker can switch between multi-folder and multi-chart selection.
class SongPickerEvents final : public IFileDialogEvents, public IFileDialogControlEvents {
    ULONG refs_=1;
public:
    bool switchMode=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==IID_IFileDialogEvents)*out=static_cast<IFileDialogEvents*>(this);
        else if(id==IID_IFileDialogControlEvents)*out=static_cast<IFileDialogControlEvents*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{const auto refs=--refs_;if(!refs)delete this;return refs;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override{*response=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override{*response=FDEOR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize*,DWORD,DWORD) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize* custom,DWORD id) override{
        if(id!=1)return S_OK;
        Microsoft::WRL::ComPtr<IFileDialog> dialog;
        if(SUCCEEDED(custom->QueryInterface(IID_PPV_ARGS(&dialog)))){switchMode=true;return dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));}
        return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize*,DWORD,BOOL) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize*,DWORD) override{return S_OK;}
};
inline bool chooseSongSources(HWND owner,const std::filesystem::path& initial,std::vector<std::filesystem::path>& selected) {
    selected.clear();bool folders=true;
    for(;;) {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return false;
        DWORD flags{};if(FAILED(dialog->GetOptions(&flags)))return false;
        flags|=FOS_ALLOWMULTISELECT|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR;
        flags|=folders?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST;
        if(FAILED(dialog->SetOptions(flags)))return false;
        dialog->SetTitle(folders?L"곡 폴더 여러 개 선택 (Ctrl / Shift) — 아래에서 채보 파일 선택으로 전환 가능":L"채보 파일 여러 개 선택 (Ctrl / Shift) — 채보가 있는 폴더를 복사 없이 등록합니다");
        dialog->SetOkButtonLabel(L"곡 추가");
        const COMDLG_FILTERSPEC filter{L"BMS 채보 (*.bms;*.bme;*.bml;*.pms;*.bmson;*.mbm)",L"*.bms;*.bme;*.bml;*.pms;*.bmson;*.mbm"};
        if(!folders)dialog->SetFileTypes(1,&filter);
        Microsoft::WRL::ComPtr<IShellItem> initialItem;
        if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&initialItem))))dialog->SetFolder(initialItem.Get());
        Microsoft::WRL::ComPtr<IFileDialogCustomize> custom;
        Microsoft::WRL::ComPtr<SongPickerEvents> events;events.Attach(new SongPickerEvents);
        DWORD cookie{};bool advised=false;
        if(SUCCEEDED(dialog.As(&custom))) {
            custom->AddPushButton(1,folders?L"채보 파일 선택...":L"곡 폴더 선택...");
            advised=SUCCEEDED(dialog->Advise(events.Get(),&cookie));
        }
        const auto result=dialog->Show(owner);
        if(advised)dialog->Unadvise(cookie);
        if(events->switchMode){folders=!folders;continue;}
        if(FAILED(result))return false;
        Microsoft::WRL::ComPtr<IShellItemArray> items;
        DWORD count{};if(FAILED(dialog->GetResults(&items))||FAILED(items->GetCount(&count)))return false;
        std::vector<std::filesystem::path> paths;
        for(DWORD i=0;i<count;++i) {
            Microsoft::WRL::ComPtr<IShellItem> item;PWSTR path{};
            if(FAILED(items->GetItemAt(i,&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))return false;
            const std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> owned(path,CoTaskMemFree);
            paths.emplace_back(owned.get());
        }
        selected=std::move(paths);return !selected.empty();
    }
}
}
