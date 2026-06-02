# Game Streaming

I use mac and installed ffmpeg with brew. This repo conatins the code for the game. I put the engine into folder nearby.

## Source code

- Game: https://github.com/Flatout73/GameStreaming
- Reciever: https://github.com/Flatout73/GameStreamingReciever

Check branches for each tasks.

## Event dispatcher

I have iplemented event_receiver and EventSender for sending mouse and keyboard events.
Also, there is DrawSceneInspector for drawing scene tree.

## Receiver reports

I have implemented polling for the receiver reports. The receiver sends reports every ~10 s. They include byte rate, packet loss rate, and frame rate. So now we have 2 threads created via Thread:

```swift
        let recvT = Thread { [weak self] in
            self?.receiveLoop()
        }
        recvT.name = "UDPReceiver.ThreadA"
        recvT.start()
        receiveThread = recvT

        let reportT = Thread { [weak self] in
            self?.reportLoop()
        }
        reportT.name = "UDPReceiver.Report"
        reportT.start()
        reportThread = reportT
```

Also, I have added report showing in the game window of the reciever part to check that it works.

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