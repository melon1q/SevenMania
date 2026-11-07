#include "Mpeg1Video.h"
#include <array>
#include <cstdio>
#include <cmath>
#include <algorithm>

#pragma warning(push,0)
#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg/pl_mpeg.h"
#pragma warning(pop)

struct Mpeg1Video::Impl {
    plm_video_t* video{};
    ~Impl(){if(video)plm_video_destroy(video);}
};
Mpeg1Video::Mpeg1Video()=default;
Mpeg1Video::~Mpeg1Video()=default;
void Mpeg1Video::reset(){impl_.reset();}
bool Mpeg1Video::open(const std::filesystem::path& path){
    reset();
    // Require an actual MPEG-1 sequence header, not just a .mpg suffix.
    FILE* opened{};if(_wfopen_s(&opened,path.c_str(),L"rb")!=0)return false;
    const std::unique_ptr<FILE,decltype(&fclose)> file(opened,fclose);
    if(!file)return false;
    if(_fseeki64(file.get(),0,SEEK_END)!=0)return false;
    const auto length=_ftelli64(file.get());
    if(length<=0||length>512ll*1024*1024||_fseeki64(file.get(),0,SEEK_SET)!=0)return false;
    std::array<unsigned char,4096> header{};
    const auto count=fread(header.data(),1,header.size(),file.get());
    size_t sequence=count;
    for(size_t i=0;i+8<=count;++i)if(header[i]==0&&header[i+1]==0&&header[i+2]==1){
        if(header[i+3]==0xba)return false; // Program streams stay with the native reader.
        if(header[i+3]==0xb3){sequence=i;break;}
    }
    if(sequence==count)return false;
    const int w=(header[sequence+4]<<4)|(header[sequence+5]>>4);
    const int h=((header[sequence+5]&15)<<8)|header[sequence+6];
    const int rate=header[sequence+7]&15;
    if(w<2||h<2||w>2048||h>2048||(w&1)||(h&1)||rate<1||rate>8)return false;
    for(size_t i=sequence+8;i+5<count;++i)if(header[i]==0&&header[i+1]==0&&header[i+2]==1){
        if(header[i+3]==0)break;
        if(header[i+3]==0xb5&&(header[i+4]>>4)==1)return false; // MPEG-2 sequence extension.
    }
    // The library owns a duplicate FILE handle through its streaming buffer.
    FILE* stream{};if(_wfopen_s(&stream,path.c_str(),L"rb")!=0)return false;
    auto* buffer=plm_buffer_create_with_file(stream,TRUE);
    if(!buffer){fclose(stream);return false;}
    auto next=std::make_unique<Impl>();next->video=plm_video_create_with_buffer(buffer,TRUE);
    if(!next->video){plm_buffer_destroy(buffer);return false;}
    if(plm_video_get_width(next->video)!=w||plm_video_get_height(next->video)!=h)return false;
    const double fps=plm_video_get_framerate(next->video);
    if(!std::isfinite(fps)||fps<=0||fps>120)return false;
    impl_=std::move(next);return true;
}
bool Mpeg1Video::read(std::vector<uint32_t>& pixels,double& time){
    if(!impl_)return false;
    auto* frame=plm_video_decode(impl_->video);if(!frame)return false;
    if(frame->width!=unsigned(width())||frame->height!=unsigned(height()))return false;
    pixels.assign(size_t(width())*height(),0xff000000u);
    plm_frame_to_bgra(frame,reinterpret_cast<uint8_t*>(pixels.data()),width()*4);
    time=frame->time;return std::isfinite(time)&&time>=0;
}
bool Mpeg1Video::rewind(){if(!impl_)return false;plm_video_rewind(impl_->video);return true;}
bool Mpeg1Video::ready()const{return impl_!=nullptr;}
int Mpeg1Video::width()const{return impl_?plm_video_get_width(impl_->video):0;}
int Mpeg1Video::height()const{return impl_?plm_video_get_height(impl_->video):0;}
double Mpeg1Video::frameRate()const{return impl_?plm_video_get_framerate(impl_->video):0;}
