# Game Streaming

I use mac and installed ffmpeg with brew. This repo conatins the code for the game. I put the enging in folder nearby.


## Codecs

For video encoding I use h.264 and h.265 (and hevc) with different bitrates. It is clearly seen the bitrate affects the quality of the video. The higher the bitrate, the better the quality. Codec hevc is more efficient than h.264 sizewise, but no visible difference in quality with high bitrate. However, with low bitrate, hevc has better quality.

Grid Layout Reference:

Top-Left: H.264 High (2 Mbps)
Bottom-Left: HEVC High (2 Mbps)
Top-Right: H.264 Low (100 kbps)
Bottom-Right: HEVC Low (100 kbps)

https://youtu.be/g2bkt_YEWN8


Source code:
https://github.com/Flatout73/GameStreaming