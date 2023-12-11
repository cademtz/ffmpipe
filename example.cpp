#include <cstdio>
#include <ffmpipe/ffmpipe.h>
#include <sstream>
#include <cstdint>
#include <cmath>

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define FRAMERATE 60
#define DURATION_SECONDS 5

static void WriteDummyFrames(ffmpipe::PipePtr pipe, uint32_t width, uint32_t height, uint32_t num_frames);

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        printf(
            "ffmpipe <ffmpeg-path> \"<output-args>\"\n"
            "This will output an example video.\n"
            "Output args are appended to the input args, and must include the file name\n"
        );
        return 0;
    }

    std::filesystem::path ffmpeg_path = argv[1];
    std::wstring output_args;

    for (char ch : std::string_view(argv[2]))
        output_args.push_back(ch);

    std::wstringstream ffmpeg_args;
    ffmpeg_args << "-c:v rawvideo -f rawvideo -pix_fmt rgb24 -s:v " << FRAME_WIDTH << 'x' << FRAME_HEIGHT << " -framerate " << FRAMERATE << ' ';
    ffmpeg_args << "-i - ";
    ffmpeg_args << output_args;
    
    ffmpipe::PipePtr pipe = ffmpipe::Pipe::Create(ffmpeg_path, ffmpeg_args.str());
    if (!pipe)
    {
        printf("Failed to create pipe. Win32 error: 0x%X\n", GetLastError());
        return -1;
    }

    WriteDummyFrames(pipe, FRAME_WIDTH, FRAME_HEIGHT, DURATION_SECONDS * FRAMERATE);
    pipe->Close();
    
    DWORD last_error = GetLastError();
    if (last_error != ERROR_SUCCESS)
        printf("Win32 error: 0x%X\n", last_error);
    return 0;
}

void WriteDummyFrames(ffmpipe::PipePtr pipe, uint32_t width, uint32_t height, uint32_t num_frames)
{
    const size_t STRIDE = 3;
    const size_t buffer_size = width * height * STRIDE;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
    
    for (uint32_t frame = 0; frame < num_frames; ++frame)
    {
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                uint8_t* pixel = buffer.get() + (y * width + x) * STRIDE;

                float uv[2] = { (float)x / width, (float)y / height };
                float time = (float)frame / FRAMERATE;
                
                float rgb[3] = {
                    0.5f + 0.5f * std::cosf(time + uv[0]),
                    0.5f + 0.5f * std::cosf(time + uv[1] + 2),
                    0.5f + 0.5f * std::cosf(time + uv[0] + 4),
                };
                
                for (int i = 0; i < 3; ++i)
                    pixel[i] = (uint8_t)(rgb[i] * 255);
            }
        }
        if (!pipe->Write(buffer.get(), buffer_size))
        {
            printf("Failed to write frame\n");
            return;
        }
    }
}