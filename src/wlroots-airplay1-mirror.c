#define _POSIX_C_SOURCE 200112L

#define _XOPEN_SOURCE 600 /* for usleep */
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include <wayland-client-protocol.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include <sys/time.h>

#define STREAM_FRAME_RATE 20

static int opt_output_num = 0;

// Multiple output sockets to stream to multiple devices
int output_sockets[255] = {};
FILE *output_file = NULL;
FILE *output_stdout = NULL;

static struct AVCodecContext *enc_ctx = NULL;
static AVBufferRef *hw_device_ctx = NULL;
static struct SwsContext *sws_ctx = NULL;

static struct wl_shm *shm = NULL;
static struct zwlr_screencopy_manager_v1 *screencopy_manager = NULL;
const char *output_name = NULL;
static struct wl_output *output = NULL;

static struct {
    struct wl_buffer *wl_buffer;
    void *data;
    enum wl_shm_format format;
    int width, height, stride;
    bool y_invert;
    uint64_t start_pts;
    uint64_t pts;
} buffer;
bool buffer_copy_done = false;

static struct wl_buffer *create_shm_buffer(int32_t fmt,
        int width, int height, int stride, void **data_out) {
    int size = stride * height;

    const char shm_name[] = "/scrcpy-capture-wlroots-airplay1-mirror";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if( fd < 0 ) {
        fprintf(stderr, "ERROR: shm_open failed %d\n", fd);
        return NULL;
    }
    shm_unlink(shm_name);

    int err;
    while( (err = ftruncate(fd, size)) == EINTR ) {
        // No-op
    }
    if( err < 0 ) {
        close(fd);
        fprintf(stderr, "ERROR: ftruncate failed\n");
        return NULL;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if( data == MAP_FAILED ) {
        fprintf(stderr, "ERROR: mmap failed: %m\n");
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    close(fd);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
        stride, fmt);
    wl_shm_pool_destroy(pool);

    *data_out = data;
    return buffer;
}

static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format,
        uint32_t width, uint32_t height, uint32_t stride) {
    buffer.format = format;
    buffer.width = width;
    buffer.height = height;
    buffer.stride = stride;

    if( !buffer.wl_buffer ) {
        buffer.wl_buffer =
            create_shm_buffer(format, width, height, stride, &buffer.data);
    }

    if( buffer.wl_buffer == NULL ) {
        fprintf(stderr, "ERROR: failed to create buffer\n");
        exit(EXIT_FAILURE);
    }

    zwlr_screencopy_frame_v1_copy(frame, buffer.wl_buffer);
}

static void frame_handle_flags(void *data,
        struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
    buffer.y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}

static void frame_handle_ready(void *data,
        struct zwlr_screencopy_frame_v1 *frame, uint32_t tv_sec_hi,
        uint32_t tv_sec_lo, uint32_t tv_nsec) {
    buffer.pts = ((((uint64_t)tv_sec_hi) << 32) | tv_sec_lo) * 1000000000 + tv_nsec;
    buffer_copy_done = true;
}

static void frame_handle_failed(void *data,
        struct zwlr_screencopy_frame_v1 *frame) {
    fprintf(stderr, "ERROR: failed to copy frame\n");
    exit(EXIT_FAILURE);
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
};

static void handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    if( strcmp(interface, wl_output_interface.name) == 0 && opt_output_num > 0 ) {
        fprintf(stderr, "INFO: Using output: %s\n", interface);
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        opt_output_num--;
    } else if( strcmp(interface, wl_shm_interface.name) == 0 ) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if( strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0 ) {
        screencopy_manager = wl_registry_bind(registry, name,
            &zwlr_screencopy_manager_v1_interface, 1);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t name) {
    // Who cares?
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx) {
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;

    if( !(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx)) ) {
        fprintf(stderr, "Failed to create VAAPI frame context.\n");
        return -1;
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width     = ctx->width;
    frames_ctx->height    = ctx->height;
    frames_ctx->initial_pool_size = 20;
    if( (err = av_hwframe_ctx_init(hw_frames_ref)) < 0 ) {
        fprintf(stderr, "Failed to initialize VAAPI frame context. "
                "Error code: %s\n",av_err2str(err));
        av_buffer_unref(&hw_frames_ref);
        return err;
    }
    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if( !ctx->hw_frames_ctx )
        err = AVERROR(ENOMEM);

    av_buffer_unref(&hw_frames_ref);
    return err;
}

static enum AVPixelFormat scrcpy_fmt_to_pixfmt(uint32_t fmt) {
    switch (fmt) {
    case WL_SHM_FORMAT_NV12: return AV_PIX_FMT_NV12;
    case WL_SHM_FORMAT_ARGB8888: return AV_PIX_FMT_BGRA;
    case WL_SHM_FORMAT_XRGB8888: return AV_PIX_FMT_BGR0;
    case WL_SHM_FORMAT_ABGR8888: return AV_PIX_FMT_RGBA;
    case WL_SHM_FORMAT_XBGR8888: return AV_PIX_FMT_RGB0;
    case WL_SHM_FORMAT_RGBA8888: return AV_PIX_FMT_ABGR;
    case WL_SHM_FORMAT_RGBX8888: return AV_PIX_FMT_0BGR;
    case WL_SHM_FORMAT_BGRA8888: return AV_PIX_FMT_ARGB;
    case WL_SHM_FORMAT_BGRX8888: return AV_PIX_FMT_0RGB;
    default: return AV_PIX_FMT_NONE;
    };
}

static void writeUInt32LE(uint8_t *buff, uint32_t buff_pos, uint32_t data32) {
    buff[buff_pos] = (uint8_t) data32 & 0xff;
    buff[buff_pos+1] = (uint8_t) (data32 >> 8) & 0xff;
    buff[buff_pos+2] = (uint8_t) (data32 >> 16) & 0xff;
    buff[buff_pos+3] = (uint8_t) (data32 >> 24) & 0xff;
}

static void writeFloat32LE(uint8_t *buff, uint32_t buff_pos, float data32) {
    // TODO: probably will not work well somewhere
    uint8_t *b = (uint8_t *) &buff[buff_pos];
    uint8_t *p = (uint8_t *) &data32;
#if defined (_M_IX86) || (defined (CPU_FAMILY) && (CPU_FAMILY == I80X86))
    b[0] = p[3];
    b[1] = p[2];
    b[2] = p[1];
    b[3] = p[0];
#else
    b[0] = p[0];
    b[1] = p[1];
    b[2] = p[2];
    b[3] = p[3];
#endif
}

static void writeUInt16LE(uint8_t *buff, uint32_t buff_pos, uint16_t data16) {
    buff[buff_pos] = (uint8_t) data16 & 0xff;
    buff[buff_pos+1] = (uint8_t) (data16 >> 8) & 0xff;
}

static void writeUInt16BE(uint8_t *buff, uint32_t buff_pos, uint16_t data16) {
    buff[buff_pos] = (uint8_t) (data16 >> 8) & 0xff;
    buff[buff_pos+1] = (uint8_t) data16 & 0xff;
}

static size_t find0001(const uint8_t *p, size_t left_size) {
    char counter = 0;
    for( size_t i = 0; i < left_size; ++i ) {
        if( p[i] == 0 )
            counter++;
        else if( counter == 3 && p[i] == 1 ) {
            return i+1;
        } else
            counter = 0;
    }
    return -1;
}

uint8_t avcc_buff[1024];
static size_t prepareAVCCData() {
    // Read extradata annexb
    uint8_t *sps = NULL;
    uint8_t sps_size = 0;
    uint8_t *pps = NULL;
    uint8_t pps_size = 0;
    char counter = 0;

    size_t pos_sps = find0001(enc_ctx->extradata, enc_ctx->extradata_size);
    sps = &enc_ctx->extradata[pos_sps];
    size_t pos_pps = pos_sps + find0001(&enc_ctx->extradata[pos_sps], enc_ctx->extradata_size-pos_sps);
    pps = &enc_ctx->extradata[pos_pps];
    sps_size = pos_pps - pos_sps - 4;
    fprintf(stderr, "DEBUG: Found sps: %ld, size: %d\n", pos_sps, sps_size);
    for( size_t j = 0; j < sps_size; ++j )
        fprintf(stderr, "0x%02x, ", (unsigned char)sps[j]);
    fprintf(stderr, "\n");
    pps_size = enc_ctx->extradata_size - pos_pps;
    fprintf(stderr, "DEBUG: Found pps: %ld, size: %d\n", pos_pps, pps_size);
    for( size_t j = 0; j < pps_size; ++j )
        fprintf(stderr, "0x%02x, ", (unsigned char)pps[j]);
    fprintf(stderr, "\n");

    pos_pps = find0001(&enc_ctx->extradata[pos_pps], enc_ctx->extradata_size-pos_pps);
    if( -1 != pos_pps ) {
        fprintf(stderr, "ERROR: Found another nalu, it should not be here: %ld\n", pos_pps);
        exit(1);
    }

    // Send codec data in avcc format
    avcc_buff[0] = 0x01;  // version
    avcc_buff[1] = sps[1]; // SPS profile
    avcc_buff[2] = sps[2]; // SPS compatibility
    avcc_buff[3] = sps[3]; // SPS level
    avcc_buff[4] = 0xFC | 3; // reserved (6 bits), NULA length size - 1 (2 bits)
    avcc_buff[5] = 0xE0 | 1; // reserved (3 bits), num of SPS (5 bits)
    writeUInt16BE(avcc_buff, 6, sps_size); // 2 bytes for length of SPS in BE

    // TODO: check buffer overload
    memcpy(&avcc_buff[8], sps, sps_size); // data of SPS

    size_t pps_begin = 8+sps_size;
    avcc_buff[pps_begin] = 0x01;  // num of PPS
    writeUInt16BE(avcc_buff, pps_begin+1, pps_size);  // 2 bytes for length of PPS in BE
    memcpy(&avcc_buff[pps_begin+3], pps, pps_size); // data of PPS

    return pps_begin+3+pps_size;
}

const int HEADER_BUFF_SIZE = 128;
uint8_t header_buff[128];
static void prepareHeader(uint32_t payload_size, uint16_t type) {
    // Clean buffer
    memset(header_buff, 0x00, 128);

    writeUInt32LE(header_buff, 0, payload_size); // 4 bytes Payload size
    writeUInt16LE(header_buff, 4, type); // 2 bytes Payload type
    writeUInt16LE(header_buff, 6, type == 0x02 ? 0x1e : 0x06 ); // 2 bytes Payload option (0x1e on heartbeat)

    if( type == 0x02 ) // HEART_BEAT
        return;

    // Get current seconds and fraction
    struct timeval ts;
    gettimeofday(&ts, NULL);
    writeUInt32LE(header_buff, 8, ts.tv_usec); // 4 bytes NTP Timestamp fraction
    writeUInt32LE(header_buff, 12, ts.tv_sec); // 4 bytes NTP Timestamp seconds

    /*fprintf(stderr, "DEBUG: data size: %u : ", payload_size);
    for( size_t j = 0; j < 16; ++j )
        fprintf(stderr, "%02x ", header_buff[j]);
    fprintf(stderr, "\n");*/

    // Write source screen WxH if type VIDEO_CODEC
    if( type == 0x01 ) {
        writeFloat32LE(header_buff, 16, 1920.0f); // 4 bytes Source screen width
        writeFloat32LE(header_buff, 20, 1080.0f); // 4 bytes Source screen height
    }

    writeFloat32LE(header_buff, 40, 1920.0f); // 4 bytes Source screen width
    writeFloat32LE(header_buff, 44, 1080.0f); // 4 bytes Source screen height

    // 48 byte - float (REAL_SCREEN_WIDTH - SENT_SCREEN_WIDTH)/2
    // Probably to add black boxes and center the picture horizontally
    writeFloat32LE(header_buff, 48, 0.0f);
    // 52 byte - float (REAL_SCREEN_HEIGHT - SENT_SCREEN_HEIGHT)/2
    // Probably to add black boxes and center the picture vertically
    writeFloat32LE(header_buff, 52, 0.0f);

    // Send the supported picture size (could be gotten from "GET /stream.xml HTTP/1.1")
    writeFloat32LE(header_buff, 56, 1920.0f); // 4 bytes Supported screen width
    writeFloat32LE(header_buff, 60, 1080.0f); // 4 bytes Supported screen height
}

static void sendToOutputs(uint8_t *buffer, size_t num_bytes) {
    // TODO: parallelize to increase the framerate
    //struct timespec tm;
    //clock_gettime( CLOCK_REALTIME, &tm );
    //int64_t start = tm.tv_nsec + tm.tv_sec * 1000000000;
    for( uint8_t i = 0; i < 255; i++ ) {
        if( output_sockets[i] == 0 )
            break;
        send(output_sockets[i], buffer, num_bytes, 0);
    }
    if( output_file )
        fwrite(buffer, 1, num_bytes, output_file);
    if( output_stdout )
        fwrite(buffer, 1, num_bytes, output_stdout);
    //clock_gettime( CLOCK_REALTIME, &tm );
    //int64_t end = tm.tv_nsec + tm.tv_sec * 1000000000;
    //fprintf(stderr, "----> send bytes %li delay: %ldms\n", num_bytes, (end - start) / 1000);
}

static void initMirroringConnection() {
    // Read plist
    // TODO: Generate plist dynamically
    char plist_buf[1024] = {0};
    FILE* fh = NULL;
    fh = fopen("stream-mirror.bplist", "rb");
    if( fh == NULL ) {
        fprintf(stderr, "ERROR: unable to open 'stream-mirror.bplist' from current dir\n");
        exit(1);
    }
    fprintf(stderr, "DEBUG: plist reading\n");
    size_t plist_len = fread(plist_buf, 1, 1024, fh);
    fprintf(stderr, "DEBUG: plist read done: len: %ld\n", plist_len);
    fclose(fh);
    fh = NULL;

    // Create buffers for data
    char data_len[32];
    sprintf(data_len, "%ld\r\n\r\n", plist_len);
    char buff[2048] = {0};

    // Generate headers
    const char *header = "POST /stream HTTP/1.1\r\n"
        "User-Agent: wlroots-airplay/1.0.0\r\n"
        "X-Apple-Device-ID: 0x7B:DE:DB:1F:BB:AB\r\n"
        "X-Apple-Client-Name: WLRootsAirplay\r\n"
        "X-Apple-ProtocolVersion: 1\r\n"
        "Content-Type: application/x-apple-binary-plist\r\n"
        "Content-Length: ";

    strcat(buff, header);
    strcat(buff, data_len);

    // Send Headers
    sendToOutputs(buff, strlen(header) + strlen(data_len));
    // Send plist
    sendToOutputs(plist_buf, plist_len);
    fprintf(stderr, "DEBUG: Initialized airplay mirroring\n");
}

static const char usage[] =
    "Usage: scrcpy-capture [options...]\n"
    "\n"
    "  -h                     Show help message and quit.\n"
    "  -v <vaapi_dev>         Use VAAPI device to encode the frames.\n"
    "  -o <output_num>        Set the output number to capture.\n"
    "  -a <addr[:port]>,[...] Send stream to airplay 1.0 device with specified\n"
    "                         address:port list (separated by comma).\n"
    "  -s                     Output stream to stdout.\n"
    "  -f <file_path>         Output stream to the specified file path.\n"
    "  -c                     Include cursors in the capture.\n";

int main(int argc, char *argv[]) {
    bool write_stdout = false;
    bool with_cursor = false;
    bool use_hw = false;
    int enc_pix_format = AV_PIX_FMT_YUV420P;
    int frame_pix_format = AV_PIX_FMT_YUV420P;
    const char *encoder_name = "libx264";
    const char *hw_dev_path = NULL;
    const char *file_path = NULL;
    char *airplay_addresses = NULL;

    int c;

    while( (c = getopt(argc, argv, "hvf:o:a:p:sc")) != -1 ) {
        switch( c ) {
        case 'h':
            printf("%s", usage);
            return EXIT_SUCCESS;
        case 'v':
            encoder_name = "h264_vaapi";
            enc_pix_format = AV_PIX_FMT_VAAPI;
            frame_pix_format = AV_PIX_FMT_NV12;
            use_hw = true;
            hw_dev_path = optarg;
            break;
        case 'f':
            file_path = optarg;
            break;
        case 'o':
            opt_output_num = atoi(optarg);
            break;
        case 'a':
            airplay_addresses = optarg;
            break;
        case 's':
            write_stdout = true;
            break;
        case 'c':
            with_cursor = true;
            break;
        case '?':
            if( isprint(optopt) )
              fprintf(stderr, "ERROR: Unknown option `-%c'.\n", optopt);
            else
              fprintf(stderr, "ERROR: Unknown option character `\\x%x'.\n", optopt);
            return 1;
        default:
            break;
        }
    }

    struct wl_display * display = wl_display_connect(NULL);
    if( display == NULL ) {
        fprintf(stderr, "ERROR: failed to create display: %m\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if( shm == NULL ) {
        fprintf(stderr, "ERROR: compositor is missing wl_shm\n");
        return EXIT_FAILURE;
    }
    if( screencopy_manager == NULL ) {
        fprintf(stderr, "ERROR: compositor doesn't support wlr-screencopy-unstable-v1\n");
        return EXIT_FAILURE;
    }
    if( output == NULL ) {
        fprintf(stderr, "ERROR: no output available\n");
        return EXIT_FAILURE;
    }

    struct zwlr_screencopy_frame_v1 *wl_frame =
        zwlr_screencopy_manager_v1_capture_output(screencopy_manager, with_cursor, output);
    zwlr_screencopy_frame_v1_add_listener(wl_frame, &frame_listener, NULL);

    if( airplay_addresses ) {
        // TODO: Check MDNS on airplay features and determine mirroring support
        char *addr_ptr = strtok(airplay_addresses, ",");
        uint8_t counter = 0;
        while( addr_ptr != NULL ) {
            int port = 7100;
            char *port_ptr = strchr(addr_ptr, ':');
            if( port_ptr != NULL ) {
                port = atoi(&port_ptr[1]);
                port_ptr[0] = '\0';
            }

            fprintf(stderr, "INFO: Writing stream to airplay 1.0 device: %s:%d\n", addr_ptr, port);

            // Create socket
            if( (output_sockets[counter] = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
                fprintf(stderr, "ERROR: Socket creation error\n");
                return -1;
            }
            int yes = 1;
            if( setsockopt(output_sockets[counter], IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1 ) {
                fprintf(stderr, "ERROR: Socket setsockopt TCP_NODELAY error\n");
                return -1;
            }

            struct sockaddr_in serv_addr;
            struct hostent *host;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            // Try to parse IPv4
            if( inet_pton(AF_INET, addr_ptr, &serv_addr.sin_addr) <= 0 ) {
                // Try to get address from DNS
                host = gethostbyname(addr_ptr);
                if( !host ) {
                    fprintf(stderr, "ERROR: Wrong address %s\n", addr_ptr);
                    return -1;
                }
                memcpy(&serv_addr.sin_addr, host->h_addr_list[0], host->h_length);
            }
            // Connect to socket
            if( connect(output_sockets[counter], (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0 ) {
                fprintf(stderr, "ERROR: Connection Failed\n");
                return -1;
            }

            if( port_ptr != NULL )
                port_ptr[0] = ':';
            addr_ptr = strtok(NULL, ",");
            counter++;
        }
        if( counter < 255 )
            output_sockets[counter] = 0;
    } else
        output_sockets[0] = 0;

    if( file_path ) {
        fprintf(stderr, "INFO: Writing stream to file: %s\n", file_path);
        output_file = fopen(file_path, "wb");
    }

    if( write_stdout ) {
        fprintf(stderr, "INFO: Writing stream to stdout\n");
        output_stdout = stdout;
    }

    if( !output_file && !output_stdout && output_sockets[0] == 0 ) {
        fprintf(stderr, "ERROR: No output is specified (check -s, -f, -a)\n");
        exit(1);
    }

    // AVLIB INIT
    int err;
    if( use_hw ) {
        err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, hw_dev_path, NULL, 0);
        if( err < 0 ) {
            fprintf(stderr, "ERROR: Failed to create a VAAPI device. Error code: %s\n", av_err2str(err));
            goto close;
        }
    }

    AVFrame *frame = NULL, *hw_frame = NULL;
    AVPacket *pkt = NULL;
    const AVCodec *encoder = avcodec_find_encoder_by_name(encoder_name);
    if( !encoder ) {
        fprintf(stderr, "ERROR: Codec '%s' not found\n", encoder_name);
        goto close;
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if( !enc_ctx ) {
        fprintf(stderr, "ERROR: Could not allocate video codec context\n");
        goto close;
    }

    pkt = av_packet_alloc();
    if( !pkt )
        goto close;

    while( !buffer_copy_done && wl_display_dispatch(display) != -1 ) {
        // This space is intentionally left blank
    }

    /* put sample parameters */
    enc_ctx->bit_rate = 4096000;
    /* resolution must be a multiple of two */
    enc_ctx->width = buffer.width;
    enc_ctx->height = buffer.height;
    /* frames per second */
    enc_ctx->time_base = (AVRational){1, STREAM_FRAME_RATE};
    enc_ctx->framerate = (AVRational){STREAM_FRAME_RATE, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    enc_ctx->gop_size = 10;
    enc_ctx->pix_fmt = enc_pix_format;

    if( use_hw ) {
        if( (err = set_hwframe_ctx(enc_ctx, hw_device_ctx)) < 0 ) {
            fprintf(stderr, "ERROR: Failed to set hwframe context.\n");
            goto close;
        }
    }

    if( !use_hw && encoder->id == AV_CODEC_ID_H264 ) {
        av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(enc_ctx->priv_data, "profile", "baseline", 0);
        av_opt_set_int(enc_ctx->priv_data, "intra-refresh", 1, 0);
        av_opt_set_int(enc_ctx->priv_data, "crf", 15, 0);
        //av_opt_set(enc_ctx->priv_data, "x264-params", "vbv-maxrate=500000:vbv-bufsize=500:slice-max-size=1500:keyint=60", 0);
        //av_opt_set(enc_ctx->priv_data, "x264opts", "no-mbtree:sliced-threads:sync-lookahead=0", 0);
        enc_ctx->max_b_frames = 0;
        enc_ctx->delay = 0;
        enc_ctx->thread_count = 1;
        enc_ctx->thread_type = FF_THREAD_SLICE;
        enc_ctx->slices = 1;
        enc_ctx->level = 40;
        av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);
    }
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    bool codec_data_refresh = true;

    /* open it */
    err = avcodec_open2(enc_ctx, encoder, NULL);
    if( err < 0 ) {
        fprintf(stderr, "ERROR: Could not open codec: %s\n", av_err2str(err));
        goto close;
    }

/*    if( use_hw ) {
        if( !(hw_frame = av_frame_alloc()) ) {
            err = AVERROR(ENOMEM);
            goto close;
        }
    }*/

    frame = av_frame_alloc();
    if( !frame ) {
        fprintf(stderr, "ERROR: Could not allocate video frame\n");
        goto close;
    }
    frame->format = frame_pix_format;
    frame->width  = enc_ctx->width;
    frame->height = enc_ctx->height;

    err = av_frame_get_buffer(frame, 1);
    if( err < 0 ) {
        fprintf(stderr, "ERROR: Could not allocate the video frame data\n");
        goto close;
    }

    initMirroringConnection();

    struct timespec tm;
    int64_t last_ts = 0;

    do {
        if( ! buffer_copy_done )
            continue;

        clock_gettime( CLOCK_REALTIME, &tm );
        int64_t frame_ts = tm.tv_nsec + tm.tv_sec * 1000000000;

        /* make sure the frame data is writable */
        err = av_frame_make_writable(frame);
        if( err < 0 )
            goto close;

        // Convert from existing format to target one
        sws_ctx = sws_getCachedContext(sws_ctx,
            frame->width, frame->height, scrcpy_fmt_to_pixfmt(buffer.format),
            frame->width, frame->height, frame_pix_format, 0, NULL, NULL, NULL);
        //int *inv_table, srcrange, *table, dstrange, brightness, contrast, saturation;
        //sws_getColorspaceDetails(sws_ctx, &inv_table, &srcrange, &table, &dstrange, &brightness, &contrast, &saturation);
        //sws_setColorspaceDetails(sws_ctx, inv_table, srcrange, table, 1, brightness, contrast, saturation);
        uint8_t * inData[1] = { buffer.data };
        int inLinesize[1] = { buffer.stride };

        // Inverting Y axis if source buffer is inverted
        if( buffer.y_invert ) {
            inData[0] += inLinesize[0]*(frame->height-1);
            inLinesize[0] = -inLinesize[0];
        }

        sws_scale(sws_ctx, (const uint8_t * const *)&inData, inLinesize, 0, frame->height, frame->data, frame->linesize);

        frame->pts = buffer.pts;

        if( !buffer.start_pts )
            buffer.start_pts = frame->pts;

        frame->pts = av_rescale_q(frame->pts - buffer.start_pts, (AVRational){ 1, 1000000000 }, enc_ctx->time_base);

        // ENCODE
        if( use_hw ) {
            if( !(hw_frame = av_frame_alloc()) ) {
                err = AVERROR(ENOMEM);
                goto close;
            }
            if( (err = av_hwframe_get_buffer(enc_ctx->hw_frames_ctx, hw_frame, 0)) < 0 ) {
                fprintf(stderr, "Error code: %s.\n", av_err2str(err));
                goto close;
            }
            if( !hw_frame->hw_frames_ctx ) {
                err = AVERROR(ENOMEM);
                goto close;
            }
            if( (err = av_hwframe_transfer_data(hw_frame, frame, 0)) < 0 ) {
                fprintf(stderr, "Error while transferring frame data to surface."
                        "Error code: %s.\n", av_err2str(err));
                goto close;
            }
            err = avcodec_send_frame(enc_ctx, hw_frame);
        } else
            err = avcodec_send_frame(enc_ctx, frame);

        if( err < 0 ) {
            fprintf(stderr, "ERROR: sending a frame for encoding failed, Error code: %s\n", av_err2str(err));
            goto close;
        }

        // WRITE
        while( err >= 0 ) {
            err = avcodec_receive_packet(enc_ctx, pkt);
            if( err == AVERROR(EAGAIN) || err == AVERROR_EOF )
                break;
            else if( err < 0 ) {
                fprintf(stderr, "ERROR: encoding failed\n");
                goto close;
            }

            fprintf(stderr, "DEBUG: extradata: %d, packet: %d\n", enc_ctx->extradata_size, pkt->size);
            if( codec_data_refresh ) {
                // Send ping
                // TODO: send heart beat every second
                prepareHeader(0, 0x02); // type HEART_BEAT
                sendToOutputs(header_buff, HEADER_BUFF_SIZE);

                // Send VIDEO_CODEC header
                size_t avcc_len = prepareAVCCData();
                prepareHeader(avcc_len, 0x01); // type VIDEO_CODEC
                sendToOutputs(header_buff, HEADER_BUFF_SIZE);

                // Send AVCC data
                sendToOutputs(avcc_buff, avcc_len);

                codec_data_refresh = false;
            }

            // Change nalu start to nalu size
            uint8_t *pd = pkt->data;
            size_t ps = pkt->size;
            size_t pos_data = find0001(pd, ps);
            size_t first_nalu = pos_data - 4; // To cut avcodec comments
            while( -1 != pos_data ) {
                size_t nalu_len;
                //fprintf(stderr, "DEBUG: Found nalu data pos: %ld", pos_data);
                pd = &pd[pos_data];
                ps = ps - pos_data;
                size_t pos_data2 = find0001(pd, ps);
                if( -1 == pos_data2 ) {
                    nalu_len = ps;
                } else {
                    // Minus 4 because find0001 returns end of nalu token
                    nalu_len = pos_data2 - pos_data - 4;
                }
                //fprintf(stderr, ", size: %ld\n", nalu_len);
                // BE size of the nalu buffer
                pd[-1] = (uint8_t) nalu_len & 0xff;
                pd[-2] = (uint8_t) (nalu_len >> 8) & 0xff;
                pd[-3] = (uint8_t) (nalu_len >> 16) & 0xff;
                pd[-4] = (uint8_t) (nalu_len >> 24) & 0xff;
                pos_data = pos_data2;
            }

            prepareHeader(pkt->size - first_nalu, 0x00); // type VIDEO_DATA
            //prepareHeader(pkt->size, 0x00); // type VIDEO_DATA
            sendToOutputs(header_buff, HEADER_BUFF_SIZE);

            // Send packet data
            sendToOutputs(&pkt->data[first_nalu], pkt->size - first_nalu);
            //sendToOutputs(pkt->data, pkt->size);

            av_packet_unref(pkt);
        }
        // WRITE DONE

        buffer_copy_done = false;

        // Sleep for the next frame
        // TODO: Multithreading capture/encoding to improve framerate
        if( frame->pts != AV_NOPTS_VALUE ) {
            clock_gettime( CLOCK_REALTIME, &tm );
            int64_t curr_ts = tm.tv_nsec + tm.tv_sec * 1000000000;

            if( last_ts != AV_NOPTS_VALUE ) {
                // 100000 = 100msec == 0.1 sec = 10f/s
                // 50000 = 50msec == 0.05 sec = 20f/s
                // TODO: add fps option to specify required frames per second
                int64_t delay = 50000 - (curr_ts - frame_ts)/1000;

                fprintf(stderr, "--> Frame ts: %ld, last_ts: %ld, additional delay: %ld\n", frame_ts, last_ts, delay);
                if( delay > 0 && delay < 1000000 )
                    usleep(delay);
            }
            last_ts = frame_ts;
        }

        zwlr_screencopy_frame_v1_destroy(wl_frame);

        wl_frame = zwlr_screencopy_manager_v1_capture_output(screencopy_manager, with_cursor, output);
        zwlr_screencopy_frame_v1_add_listener(wl_frame, &frame_listener, NULL);

        if( use_hw )
            av_frame_free(&hw_frame);
    } while( wl_display_dispatch(display) != -1 );

close:
    if( output_sockets[0] != 0 ) {
        for( uint8_t i = 0; i < 255; i++ ) {
            if( output_sockets[i] == 0 )
                break;
            close(output_sockets[i]);
        }
    }
    if( output_file )
        fclose(output_file);
    if( output_stdout )
        fclose(output_stdout);

    av_frame_free(&frame);
    av_frame_free(&hw_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc_ctx);
    av_buffer_unref(&hw_device_ctx);

    wl_buffer_destroy(buffer.wl_buffer);

    return err;
}
