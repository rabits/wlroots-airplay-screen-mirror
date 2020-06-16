# WLRoots Airplay 1.0 Screen Mirror

POC of mirroring the wayland screen output to AirPlay 1.0 compatible device.

## Implementation

Was implemented using multiple debugging tools on live stream on youtube:
* I'mProgrammer (ЯжПрограммист): https://www.youtube.com/channel/UCQYMO66yI5ym0nItFHR-hLQ
  * [Index: Learning AirPlay mirroring protocol](https://youtu.be/p_YjBJRcGM8)
  * Streams playlist: [Stream: AirPlay Protocol Client implementation](https://www.youtube.com/watch?v=AoMQG_1Eb74&list=PLGjsYh2LROL41UHAH5XXPvHJAr3KARlq5)

## Author & support

Author: Rabit <home@rabits.org>

If you like this project, you can support my open-source development by a small Bitcoin donation.

Bitcoin wallet: `3Hs7bXdEQ8Uja7RvsA29woA4Bh5d2Tx2sf`

## How to use

1. Make sure you using wlroots-based window manager
2. Run `./build` - it will compile the binary or show you what you've missed in dependencies
3. Run `./wlroots-airplay1-mirror -h` to get the possible options:
  ```
  Usage: scrcpy-capture [options...]

    -h              Show help message and quit.
    -o <output_num> Set the output number to capture.
    -a <address>    Send stream to airplay 1.0 device with specified address.
    -p <port>       Send stream to airplay 1.0 device with specified port (default 7100).
    -s              Output stream to stdout.
    -f <file_path>  Output stream to the specified file path.
    -c              Include cursors in the capture.
  ```
4. Run `./wlroots-airplay1-mirror ` to stream to AirPlay 1.0 compatible device.
  ```
  $ ./wlroots-airplay1-mirror -c -o 2 -a 192.168.30.243
  INFO: Using output: wl_output
  INFO: Using output: wl_output
  INFO: Writing stream to airplay 1.0 device: 192.168.30.243:7100
  [libx264 @ 0x5588f0491300] using cpu capabilities: MMX2 SSE2Fast SSSE3 SSE4.2 AVX FMA3 BMI2 AVX2
  [libx264 @ 0x5588f0491300] profile Constrained Baseline, level 4.0
  [libx264 @ 0x5588f0491300] 264 - core 152 r2854 e9a5903 - H.264/MPEG-4 AVC codec - Copyleft 2003-
  2017 - http://www.videolan.org/x264.html - options: cabac=0 ref=1 deblock=0:0:0 analyse=0:0 me=di
  a subme=0 psy=1 psy_rd=1.00:0.00 mixed_ref=0 me_range=16 chroma_me=1 trellis=0 8x8dct=0 cqm=0 dea
  dzone=21,11 fast_pskip=1 chroma_qp_offset=0 threads=1 lookahead_threads=1 sliced_threads=0 slices
  =1 nr=0 decimate=1 interlaced=0 bluray_compat=0 constrained_intra=0 bframes=0 weightp=0 keyint=10
  keyint_min=1 scenecut=0 intra_refresh=1 rc=crf mbtree=0 crf=15.0 qcomp=0.60 qpmin=0 qpmax=69 qpst
  ep=4 ip_ratio=1.40 aq=0
  DEBUG: plist reading
  DEBUG: plist read done: len: 273
  DEBUG: Initialized airplay mirroring
  DEBUG: Found sps: 4, size: 24
  0x67, 0x42, 0xc0, 0x28, 0xda, 0x01, 0xe0, 0x08, 0x9f, 0x96, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0x48, 0xf1, 0x83, 0x2a,
  DEBUG: Found pps: 32, size: 5
  0x68, 0xce, 0x02, 0xfc, 0x80,
  --> Frame ts: 1586900784318851640, last_ts: 0, additional delay: -35866
  --> Frame ts: 1586900784413924977, last_ts: 1586900784318851640, additional delay: 18951
  --> Frame ts: 1586900784480081104, last_ts: 1586900784413924977, additional delay: 18750
  --> Frame ts: 1586900784546800255, last_ts: 1586900784480081104, additional delay: 19894
  --> Frame ts: 1586900784613319988, last_ts: 1586900784546800255, additional delay: 18525
  --> Frame ts: 1586900784680055774, last_ts: 1586900784613319988, additional delay: 21326
  --> Frame ts: 1586900784746817137, last_ts: 1586900784680055774, additional delay: 18046
  ...
  ```

## TODO

* Encryption of the stream
* Support for AirPlay 2.0
* Audio embedding
* Locating the available AirPlay devices using mDNS (zeroconf, bonjour)
* Use hardware encoding if available (VAAPI)
* Test on the other AirPlay devices
* Variable FPS option

## Tests

It's a POC, but it could provide ~10-50 msec delay, depends on the framerate (set to 20fps) used.

Tested with WiFi N router and SwayWM 1.4:
* [MiraScreen E5S](https://mirascreen.com/collections/wireless-display/products/e5s-wireless-display)
* [MiraScreen K6](https://mirascreen.com/collections/wireless-display/products/k6-wireless-display)
* [MiraScreen G5 Plus](https://www.amazon.com/gp/product/B08395JKVM/)

## Additional info

The implementation using AirPlay 1.0, supported by old or cheap HDMI-wifi devices. During debugging
of this POC - was prepared the initial implementation of the AirPlay 2.0 streamer (w/o fp-setup).

In `tools` directory you can find some tools that could help with understanding the protocol.

### Useful links:

* WLRoots: https://github.com/swaywm/wlroots
* LibAV examples:
    * https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/muxing.c
    * https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/vaapi_encode.c
    * https://github.com/leandromoreira/ffmpeg-libav-tutorial
* FFmpeg zero-latency stream:
    * https://trac.ffmpeg.org/wiki/StreamingGuide
    * https://web.archive.org/web/20140823055916/http://mewiki.project357.com/wiki/X264_Encoding_Suggestions

* Multimedia open networking doc: https://pdfs.semanticscholar.org/92e0/3612c6d32957f0264d62391207c47e004757.pdf
* Airplay 1.0 unofficial spec: https://nto.github.io/AirPlay.html
* Airplay 1.0 receiver (C++): https://github.com/viettd56/SA/
* Airplay 2.0 analysis: http://www.programmersought.com/article/2084789418/
* Airplay 2.0 discussion: https://github.com/mikebrady/shairport-sync/issues/535
* Airplay 2.0 receiver (Java 11): https://github.com/serezhka/java-airplay-server-examples/
* Shairplay & PlayFair FairPlay implementation lib: https://github.com/juhovh/shairplay/tree/master/src/lib/playfair
* PyATV library: https://github.com/postlund/pyatv
* Raspberry Pi airplay server: https://github.com/FD-/RPiPlay
* APK decompiler: http://www.javadecompilers.com/apk
* Cutter binary reverse-engineering: https://github.com/radareorg/cutter
* LibAV H264 extradata: https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/libx264.c#L931
* H264 AVCC and Annex-B:
    * http://www.programmersought.com/article/3901815022/
    * http://aviadr1.blogspot.com/2010/05/h264-extradata-partially-explained-for.html
    * https://stackoverflow.com/questions/24884827/possible-locations-for-sequence-picture-parameter-sets-for-h-264-stream
* H264 SPS & PPS: https://www.cardinalpeak.com/the-h-264-sequence-parameter-set/

### Useful commands:

* UPNP direct: `sudo nmap -sU -p 1900 --script=upnp-info 192.168.30.243`
* UPNP multicast: `sudo nmap -sU -p 1900 --script=broadcast-upnp-info 239.255.255.250`
* Zeroconf (mdns, bonjour): `sudo nmap -sU -p5353 --script=dns-service-discovery 192.168.30.243`
* TCP dump to terminal: `tcpdump -vv -XX -s 0 -i wlan0`
* to create realtime stream: `ffmpeg -r 30 -f video4linux2 -input_format mjpeg -i /dev/video0 -pix_fmt yuv420p -an -vcodec libx264 -thread_type slice -slices 1 -profile baseline -level 32 -preset superfast -tune zerolatency -intra-refresh 1 -crf 15 -x264-params vbv-maxrate=5000:vbv-bufsize=1:slice-max-size=1500:keyint=60 out_stream.mkv`
* Capture pcap on openwrt router: `ssh -C -p 222 root@192.168.20.1 'tcpdump -U -s0 -w - -i wlan0 host 192.168.30.243' > data.pcap`
