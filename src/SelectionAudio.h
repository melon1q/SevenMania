#pragma once
#include "SongPreview.h"
#include "SystemSoundSet.h"
#include <chrono>
#include <cmath>
// UI-owner orchestration; uses the existing decoder/mixer and never touches chart timing.
class SelectionAudio {
 SongPreview music_;AudioEngine decision_;std::filesystem::path musicPath_;bool musicSelected_{},decisionReady_{},decisionActive_{};
 int appliedMusicVolume_{},fadeFromVolume_{},targetMusicVolume_{};std::chrono::steady_clock::time_point musicFadeStarted_{};bool musicVolumeInitialized_{};
public:
 static int musicVolume(int configured,bool preview)noexcept{return preview?std::min(std::clamp(configured,0,100),5):std::clamp(configured,0,100);}
 // Re-callable: a folder change in the settings stops the old BGM and the next tick starts the new one.
 void initialize(const std::filesystem::path& assets,const std::filesystem::path& skinSounds={},const bms::SystemSoundFolders& folders={}){
  configure(bms::resolveSystemSound(bms::SystemSound::Select,folders,skinSounds,assets),bms::resolveSystemSound(bms::SystemSound::Decide,folders,skinSounds,assets));
 }
 void configure(const std::filesystem::path& music,const std::filesystem::path& decision){
  stopMusic();stopDecision();musicPath_.clear();decisionReady_=false;
  std::error_code ec;if(!music.empty()&&std::filesystem::is_regular_file(music,ec))musicPath_=music;
  ec.clear();if(!decision.empty()&&std::filesystem::is_regular_file(decision,ec)){bms::Chart chart;chart.path=decision;chart.samples[L"decide"]=decision;chart.bgm.push_back({0,L"decide"});std::wstring error;decisionReady_=decision_.prepare(chart,error);}
 }
 const std::filesystem::path& musicPath()const noexcept{return musicPath_;}
 bool decisionReady()const noexcept{return decisionReady_;}
 void tickMusic(int master,int configured,bool preview,int buffer){
  if(musicPath_.empty())return;
  const int baseVolume=std::clamp(configured,0,100),target=musicVolume(configured,preview);const auto now=std::chrono::steady_clock::now();
  if(!musicSelected_){bms::Chart chart;chart.path=musicPath_;chart.previewAudio=musicPath_;music_.select(chart);musicSelected_=true;appliedMusicVolume_=fadeFromVolume_=targetMusicVolume_=baseVolume;musicVolumeInitialized_=true;}
  if(!musicVolumeInitialized_){appliedMusicVolume_=fadeFromVolume_=targetMusicVolume_=target;musicFadeStarted_=now;musicVolumeInitialized_=true;}
  else if(target!=targetMusicVolume_){fadeFromVolume_=appliedMusicVolume_;targetMusicVolume_=target;musicFadeStarted_=now;}
  if(appliedMusicVolume_!=targetMusicVolume_){
   const double progress=std::clamp(std::chrono::duration<double,std::milli>(now-musicFadeStarted_).count()/SongPreview::fadeDurationMs,0.0,1.0);
   const double eased=progress*progress*(3.0-2.0*progress);
   appliedMusicVolume_=static_cast<int>(std::lround(fadeFromVolume_+(targetMusicVolume_-fadeFromVolume_)*eased));
  }
  music_.tick(master,appliedMusicVolume_,buffer);
 }
 void stopMusic(){if(musicSelected_){music_.reset();musicSelected_=false;}musicVolumeInitialized_=false;}
 void decide(int master,int effects,int buffer){
  stopMusic();if(!decisionReady_)return;decision_.stop();decision_.setVolumes(master,effects,0,0);decision_.setBufferFrames(buffer);std::wstring error;decisionActive_=decision_.startAt(0,error);
 }
 void stopDecision(){if(decisionActive_){decision_.stop();decisionActive_=false;}}
 void tickDecision(){if(decisionActive_&&(!decision_.isRunning()||decision_.positionSeconds()>=decision_.durationSeconds())){decision_.stop();decisionActive_=false;}}
 void shutdown(){music_.shutdown();decision_.stop();decisionActive_=false;}
};
