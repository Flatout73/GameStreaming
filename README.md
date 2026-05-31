# Game Streaming

I use mac and installed ffmpeg with brew. This repo conatins the code for the game. I put the engine into folder nearby.

## Source code

- Game: https://github.com/Flatout73/GameStreaming
- Reciever: https://github.com/Flatout73/GameStreamingReciever


## Streaming 

I used ipv6 to stream the video and hevc (h.265) codec. It streams at address [::1]:50000.
I have created Swift native app using SwiftSDL wrapper as a receiver.

Video:
https://youtu.be/QpVJxrvyZlk

Video with optimizations after Claude:
https://youtu.be/-j3_ECNi0QA

## Codecs

For video encoding I use h.264 and h.265 (and hevc) with different bitrates. It is clearly seen the bitrate affects the quality of the video. The higher the bitrate, the better the quality. Codec hevc is more efficient than h.264 sizewise, but there is no visible difference in quality at high bitrate. However, with low bitrate, hevc has better quality.

Grid Layout Reference:

Top-Left: H.264 High (2 Mbps)
Bottom-Left: HEVC High (2 Mbps)
Top-Right: H.264 Low (100 kbps)
Bottom-Right: HEVC Low (100 kbps)

https://youtu.be/g2bkt_YEWN8