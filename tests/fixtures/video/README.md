`colors.m1v` is an original 16x16 MPEG-1 elementary stream: red, green, blue,
approximately one second each, 30 fps. It contains no audio or song assets.

Generated from `fixtures::writeVideo`'s `timing.avi` using:

    ffmpeg -i timing.avi -an -vcodec mpeg1video -r 30 -q:v 2 -f mpeg1video colors.m1v

FFmpeg is a development-only fixture generator. The game and automated tests
decode this committed fixture without invoking FFmpeg or installing a codec.
