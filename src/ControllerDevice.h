#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <string>
#include <vector>
#include <chrono>
#include "ControllerModel.h"
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

namespace bms::controller {
inline std::uint64_t milliseconds() noexcept {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct DeviceInfo {GUID guid;std::wstring name,id;};
class Device {
    IDirectInput8W* input_{};
    IDirectInputDevice8W* device_{};
    HWND owner_{};
    Sample sample_;
    std::uint64_t retryAt_{};
    static BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* instance,void* data){
        auto& devices=*static_cast<std::vector<DeviceInfo>*>(data);wchar_t guid[64]{};StringFromGUID2(instance->guidInstance,guid,64);
        devices.push_back({instance->guidInstance,instance->tszProductName,guid});return DIENUM_CONTINUE;
    }
    static BOOL CALLBACK axisRange(const DIDEVICEOBJECTINSTANCEW* object,void* data){
        DIPROPRANGE range{};range.diph.dwSize=sizeof(range);range.diph.dwHeaderSize=sizeof(range.diph);range.diph.dwHow=DIPH_BYID;range.diph.dwObj=object->dwType;range.lMin=0;range.lMax=65535;
        static_cast<IDirectInputDevice8W*>(data)->SetProperty(DIPROP_RANGE,&range.diph);return DIENUM_CONTINUE;
    }
    void closeDevice(){if(device_){device_->Unacquire();device_->Release();device_=nullptr;}sample_={};}
    static int axisIndex(DWORD offset){for(int i=0;i<8;++i)if(offset==DWORD(i*4))return i;return -1;}
public:
    Config config;
    std::wstring deviceId;
    std::vector<DeviceInfo> devices;
    Mapper mapper;
    Device()=default;
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
    ~Device(){closeDevice();if(input_)input_->Release();}
    bool initialize(HWND owner){owner_=owner;if(!input_&&FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input_),nullptr)))return false;return true;}
    bool scan(){devices.clear();return input_&&SUCCEEDED(input_->EnumDevices(DI8DEVCLASS_GAMECTRL,enumerate,&devices,DIEDFL_ATTACHEDONLY));}
    bool disableIfUnavailable(){
        if(!config.enabled)return false;
        if(!scan()&&input_)return false; // An enumeration failure is not proof of removal.
        if(std::any_of(devices.begin(),devices.end(),[&](const auto& d){return deviceId.empty()||deviceId==d.id;}))return false;
        config.enabled=false;closeDevice();mapper.reset();return true;
    }
    void reconnect(HWND owner){closeDevice();mapper.reset();owner_=owner;retryAt_=0;}
    bool open(){
        if(!input_||!owner_)return false;scan();
        const DeviceInfo* selected=nullptr;for(const auto& d:devices)if(deviceId.empty()||deviceId==d.id){selected=&d;break;}if(!selected)return false;
        if(FAILED(input_->CreateDevice(selected->guid,&device_,nullptr)))return false;
        if(FAILED(device_->SetDataFormat(&c_dfDIJoystick2))||FAILED(device_->SetCooperativeLevel(owner_,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE))){closeDevice();return false;}
        device_->EnumObjects(axisRange,device_,DIDFT_AXIS);
        DIPROPDWORD buffer{};buffer.diph.dwSize=sizeof(buffer);buffer.diph.dwHeaderSize=sizeof(buffer.diph);buffer.diph.dwHow=DIPH_DEVICE;buffer.dwData=128;device_->SetProperty(DIPROP_BUFFERSIZE,&buffer.diph);
        device_->Acquire();return true;
    }
    template<class Consumer> Sample poll(bool active,Consumer consume,bool monitor=false){
        const auto now=milliseconds();
        if(!config.enabled&&!monitor){mapper.reset();consume(State{});return {};}
        if(!active&&!monitor){mapper.reset();sample_={};consume(State{});return {};}
        if(!device_&&now>=retryAt_){retryAt_=now+1000;if(!open())disableIfUnavailable();}
        if(!device_){sample_={};consume(mapper.update(config,sample_,now,false));return sample_;}
        device_->Poll();
        // Drain buffered events before reading the final state. An older snapshot
        // applied after newer axis events would look like a spurious reversal.
        DIDEVICEOBJECTDATA events[128]{};DWORD count=128;
        const auto buffered=device_->GetDeviceData(sizeof(events[0]),events,&count,0);
        DIJOYSTATE2 state{};
        auto result=device_->GetDeviceState(sizeof(state),&state);
        if(FAILED(result)){count=0;device_->Acquire();device_->Poll();result=device_->GetDeviceState(sizeof(state),&state);}
        if(FAILED(result)){closeDevice();disableIfUnavailable();consume(mapper.update(config,sample_,now,false));return sample_;}
        // Retain intermediate button edges (including a press/release between frames).
        if(SUCCEEDED(buffered)&&buffered!=DI_BUFFEROVERFLOW&&sample_.connected){
            for(DWORD i=0;i<count;++i){const auto& e=events[i];if(e.dwOfs>=DIJOFS_BUTTON(0)&&e.dwOfs<=DIJOFS_BUTTON(127))sample_.buttons[e.dwOfs-DIJOFS_BUTTON(0)]=(e.dwData&0x80)!=0;
                else if(const int axis=axisIndex(e.dwOfs);axis>=0)sample_.axes[axis]=static_cast<int>(e.dwData);
                consume(mapper.update(config,sample_,now,active));}
        }
        sample_.connected=true;for(int i=0;i<128;++i)sample_.buttons[i]=(state.rgbButtons[i]&0x80)!=0;
        sample_.axes={state.lX,state.lY,state.lZ,state.lRx,state.lRy,state.lRz,state.rglSlider[0],state.rglSlider[1]};
        consume(mapper.update(config,sample_,now,active));return sample_;
    }
    void load(const std::wstring& ini){
        const auto get=[&](const wchar_t* key,int value){return int(GetPrivateProfileIntW(L"Controller",key,value,ini.c_str()));};
        config.enabled=get(L"Enabled",0)!=0;wchar_t id[128]{};GetPrivateProfileStringW(L"Controller",L"Device",L"",id,128,ini.c_str());deviceId=id;
        for(int i=0;i<7;++i)config.keys[i]=std::clamp(get((L"Key"+std::to_wstring(i+1)).c_str(),i+1),0,128)-1;
        config.scratchCw=std::clamp(get(L"ScratchCW",8),0,128)-1;config.scratchCcw=std::clamp(get(L"ScratchCCW",9),0,128)-1;
        config.menu=std::clamp(get(L"Menu",0),0,128)-1;config.axis=std::clamp(get(L"Axis",1),0,8)-1;config.mode=std::clamp(get(L"AxisMode",1),0,2);
        config.threshold=std::clamp(get(L"Threshold",256),1,8192);config.holdMs=std::clamp(get(L"ScratchHoldMs",80),20,250);mapper.reset();
    }
    bool save(const std::wstring& ini){
        bool okay=true;const auto put=[&](const wchar_t* key,int value){okay=(WritePrivateProfileStringW(L"Controller",key,std::to_wstring(value).c_str(),ini.c_str())!=0)&&okay;};
        okay=WritePrivateProfileStringW(L"Controller",L"Device",deviceId.c_str(),ini.c_str())!=0;
        put(L"Enabled",config.enabled);for(int i=0;i<7;++i)put((L"Key"+std::to_wstring(i+1)).c_str(),config.keys[i]+1);
        put(L"ScratchCW",config.scratchCw+1);put(L"ScratchCCW",config.scratchCcw+1);put(L"Menu",config.menu+1);put(L"Axis",config.axis+1);put(L"AxisMode",config.mode);put(L"Threshold",config.threshold);put(L"ScratchHoldMs",config.holdMs);return okay;
    }
};
}
