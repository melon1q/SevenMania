#pragma once
#include <filesystem>
#include <memory>
#include <vector>
#include <cstdint>

// Optional elementary MPEG-1 reader. Called only by the existing video worker.
class Mpeg1Video {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Mpeg1Video();
    ~Mpeg1Video();
    Mpeg1Video(const Mpeg1Video&)=delete;
    Mpeg1Video& operator=(const Mpeg1Video&)=delete;
    void reset();
    bool open(const std::filesystem::path& path);
    bool read(std::vector<uint32_t>& pixels,double& time);
    bool rewind();
    bool ready()const;
    int width()const;
    int height()const;
    double frameRate()const;
};
