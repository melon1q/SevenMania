#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace bms::ui {
class RmlSettingsHost {
public:
    using Change = std::function<void(const std::string&, const std::string&)>;
    using Failure = std::function<void()>;
    struct Metrics { unsigned frames{}; double renderMilliseconds{}; };
    RmlSettingsHost();
    static bool available() noexcept;
    ~RmlSettingsHost();
    bool open(HWND owner, const std::filesystem::path& document, Change change = {}, bool visible = true);
    bool openEmbedded(HWND parent, const std::filesystem::path& document, Change change = {}, Failure failure = {}, bool visible = true);
    bool setChecked(const std::string& id, bool checked);
    bool setText(const std::string& id, const std::string& text);
    bool setValue(const std::string& id, const std::string& value);
    bool setRange(const std::string& id, int minimum, int maximum);
    bool setOptions(const std::string& id, const std::vector<std::pair<std::string,std::string>>& options);
    bool setVisible(const std::string& id, bool visible);
    bool setEnabled(const std::string& id, bool enabled);
    bool setClass(const std::string& id, const std::string& name, bool active);
    // An <img>'s picture: a UTF-8 file path ("#preview" at the end loads it large, see RmlUiRuntime). Empty clears it.
    bool setImage(const std::string& id, const std::string& path);
    // Embedded only: these virtual keys go to the parent window instead of the document.
    void forwardKeys(std::vector<int> keys);
    bool scrollTop(const std::string& id);
    // Scrolls the element's scrolling ancestors just enough to show it.
    bool scrollIntoView(const std::string& id);
    void close();
    HWND window() const noexcept;
    bool owns(HWND window) const noexcept;
    bool focus(const std::string& id);
    std::string focused() const;
    RECT bounds(const std::string& id) const;
    std::string value(const std::string& id) const;
    Metrics metrics() const noexcept;
    const std::wstring& error() const noexcept;
private:
    bool openImpl(HWND owner, const std::filesystem::path&, Change, Failure, bool visible, bool embedded);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::wstring error_;
};
}
