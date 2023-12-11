#pragma once
#include <filesystem>
#include <string_view>
#include <memory>
#include <functional>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace ffmpipe
{

using PipePtr = std::shared_ptr<class Pipe>;

/**
 * @brief Run FFmpeg and write to stdin.
 * 
 * Operations are not thread-safe.
 */
class Pipe
{
public:
    using PrintFunc = std::function<void(std::string_view)>;

    ~Pipe();
    Pipe(const Pipe&) = delete;

    /**
     * @brief Create a new pipe and run FFmpeg.
     * 
     * FFmpeg will not read from stdin unless you pass the appropriate argument.
     * 
     * Example:
     *   -c:v rawvideo -f rawvideo -pix_fmt rgb24 -s:v 720x1280 -framerate 60 -i - -y output.mp4
     * This takes 720x1280 RGB frames as input. `-i -` is required to read from stdin. `-y` allows overwriting files.
     * 
     * @param ffmpeg_path Path of the FFmpeg executable.
     * @param ffmpeg_args Arguments to pass to FFmpeg.
     * @param timeout_ms Timeout in milliseconds for reading and writing.
     * @return `nullptr` on failure.
     */
    static std::shared_ptr<Pipe> Create(
        const std::filesystem::path& ffmpeg_path, std::wstring_view ffmpeg_args,
        DWORD timeout_ms = 10'000
    );
    
    /// @brief A default callback to print FFmpeg's stdout.
    /// @see SetPrintFunc
    static void DefaultPrintFunc(std::string_view str);
    /// @brief Set the callback for printing FFmpeg's stdout.
    /// @param fn The callback. Use `nullptr` to print nothing.
    void SetPrintFunc(PrintFunc fn) { m_print_fn = fn; }
    /// @brief Write all data to stdin. Blocking.
    /// @return `false` on failure.
    bool Write(const void* data, size_t length);
    /**
     * @brief Close the stdin handle and wait for program exit. Blocking.
     * @details Don't call during write operations.
     * @param timeout_ms Timeout in milliseconds.
     * @param terminate If true, the child process is terminated after timeout.
     */
    void Close(DWORD timeout_ms = INFINITE, bool terminate = true);
    
private:
    Pipe() {}
    
    /// @brief Read and print the console output of FFmpeg. Non-blocking.
    /// @return The number of bytes read
    size_t ReadOutput();

    PROCESS_INFORMATION m_procinfo = {0};
    HANDLE m_stdin_r = INVALID_HANDLE_VALUE , m_stdin_w = INVALID_HANDLE_VALUE;
    HANDLE m_stdout_r = INVALID_HANDLE_VALUE , m_stdout_w = INVALID_HANDLE_VALUE;
    HANDLE m_event = NULL;
    DWORD m_timeout_ms = 10'000;
    PrintFunc m_print_fn = DefaultPrintFunc;
};

}