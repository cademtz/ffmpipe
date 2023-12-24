#include <ffmpipe/ffmpipe.h>
#include <sstream>
#include <iostream>

namespace ffmpipe
{

/**
 * @brief Create the read & write pipes for redirecting stdin/stdout/stderr
 * @details The handle pointers are assigned when the function returns true
 * @param out_read_pipe Receives a named, synchronous pipe for reading
 * @param out_write_pipe Receives an async (overlapped) file for writing
 * @param buffer_size Size of the internal buffer for `out_write_pipe`
 * @param timeout_ms Timeout in milliseconds for the read pipe
 */
static bool CreatePipePair(const char* name, HANDLE* out_read_pipe, HANDLE* out_write_pipe, DWORD buffer_size, DWORD timeout_ms)
{
    SECURITY_ATTRIBUTES security_attrs;
    security_attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attrs.bInheritHandle = TRUE;
    security_attrs.lpSecurityDescriptor = NULL;

    std::string full_name;
    {
        std::stringstream ss;
        ss << R"(\\.\pipe\ffmpipe_)" << GetCurrentProcessId() << '_' << out_read_pipe << '_' << name;
        full_name = ss.str();
    }

    HANDLE read_pipe = CreateNamedPipeA(
        full_name.c_str(),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,
        buffer_size, buffer_size,
        timeout_ms, &security_attrs
    );
    if (read_pipe == INVALID_HANDLE_VALUE)
        return false;

    HANDLE write_pipe = CreateFileA(
        full_name.c_str(),
        GENERIC_WRITE,
        0, // No sharing
        &security_attrs,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL // Template file
	);
    if (write_pipe == INVALID_HANDLE_VALUE)
    {
        CloseHandle(read_pipe);
        return false;
    }
    
    *out_write_pipe = write_pipe;
    *out_read_pipe = read_pipe;
    return true;
}

Pipe::~Pipe()
{
    HANDLE invalid_handles[] = { m_stdin_r, m_stdin_w, m_stdout_r, m_stdout_w };
    HANDLE null_handles[] = { m_event, m_procinfo.hProcess, m_procinfo.hThread };

    for (HANDLE handle : null_handles)
    {
        if (handle)
            CloseHandle(handle);
    }

    for (HANDLE handle : invalid_handles)
    {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
}

std::shared_ptr<Pipe> Pipe::Create(
    const std::filesystem::path& ffmpeg_path, std::wstring_view ffmpeg_args,
    DWORD timeout_ms
) {
    std::shared_ptr<Pipe> stream = std::shared_ptr<Pipe>(new Pipe);
    stream->m_timeout_ms = timeout_ms;

    stream->m_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!stream->m_event)
        return nullptr;

    // Create pipes to redirect stdout, stderr, and stdin

    if (!CreatePipePair("stdout", &stream->m_stdout_r, &stream->m_stdout_w, 4096 * 4096, timeout_ms)
        || !SetHandleInformation(stream->m_stdout_r, HANDLE_FLAG_INHERIT, 0)
    ) {
        return nullptr;
    }

    if (!CreatePipePair("stdin", &stream->m_stdin_r, &stream->m_stdin_w, 4096 * 4096, timeout_ms)
        || !SetHandleInformation(stream->m_stdin_w, HANDLE_FLAG_INHERIT, 0)
    ) {
        return nullptr;
    }

    // Create the child process

    STARTUPINFOW startup_info;
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    startup_info.hStdError = stream->m_stdout_w;
    startup_info.hStdOutput = stream->m_stdout_w;
    startup_info.hStdInput = stream->m_stdin_r;
    startup_info.dwFlags = STARTF_USESTDHANDLES;

    std::wstring cmdline = ffmpeg_path.wstring();
    cmdline += ' ';
    cmdline += ffmpeg_args;

    if (!CreateProcessW(
        NULL,               // application name
        cmdline.data(),     // command line 
        NULL,               // process security attributes 
        NULL,               // primary thread security attributes 
        TRUE,               // handles are inherited 
        CREATE_NO_WINDOW,   // creation flags 
        NULL,               // use parent's environment 
        NULL,               // use parent's current directory 
        &startup_info,      // STARTUPINFO pointer 
        &stream->m_procinfo // receives PROCESS_INFORMATION 
    )) {
        return nullptr;
    }

    return stream;
}

bool Pipe::Write(const void* data, size_t length)
{
    DWORD total_written = 0;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = m_event;

    while (total_written < length)
    {
        bool ok = WriteFile(m_stdin_w, (const uint8_t*)data + total_written, (DWORD)length - total_written, nullptr, &overlapped);
        if (!ok)
        {
            if (GetLastError() != ERROR_IO_PENDING)
                return false;
            SetLastError(ERROR_SUCCESS);
        }
        
        HANDLE wait_objects[2] = { m_event, m_procinfo.hProcess };
        if (WaitForMultipleObjects(2, wait_objects, FALSE, m_timeout_ms) != STATUS_WAIT_0)
            return false; // Failure or timeout
        
        DWORD written = 0;
        if (!GetOverlappedResult(m_stdin_w, &overlapped, &written, FALSE))
            return false;
        
        total_written += written;
        
        ReadOutput();
    }
    return true;
}

void Pipe::Close(DWORD timeout_ms, bool terminate)
{
    CloseHandle(m_stdin_w);
    m_stdin_w = INVALID_HANDLE_VALUE;
    DWORD result = WaitForSingleObject(m_procinfo.hProcess, timeout_ms);
    if (result != STATUS_WAIT_0 && terminate)
        TerminateProcess(m_procinfo.hProcess, -1);
    ReadOutput();
}

void Pipe::DefaultPrintFunc(std::string_view str) {
    std::cout << str;
}

size_t Pipe::ReadOutput()
{
    DWORD available;
    if (!PeekNamedPipe(m_stdout_r, nullptr, 0, nullptr, &available, nullptr))
        return 0;

    char buffer[256];
    DWORD total_read = 0;

    while (total_read < available)
    {
        DWORD read = available - total_read;
        if (read > sizeof(buffer))
            read = sizeof(buffer);
        
        if (!ReadFile(m_stdout_r, buffer, read, &read, nullptr))
            return total_read;
        
        total_read += read;
        if (m_print_fn)
            m_print_fn(std::string_view(buffer, read));
    }

    return total_read;
}

}