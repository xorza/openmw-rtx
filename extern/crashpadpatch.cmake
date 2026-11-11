# Run in Crashpad's source directory after it is extracted, and again on a source it patched when the patch
# changes, as FetchContent runs a changed patch step without extracting anew: each change is made once,
# and stands where it was made.
#
# **Its socket transport on Linux, where it asks for libcurl.** The transport exists to upload a
# report, and nothing here uploads; libcurl would be a library every build and the AppImage carry
# for no call. The socket transport is what Crashpad builds for Android, with nothing to find. A
# release of Crashpad that words the block differently stops the configure here, by name, rather
# than building with libcurl or without a transport.
set(file util/CMakeLists.txt)
file(READ ${file} text)

set(curl
    [[        if(NOT TARGET CURL::libcurl) # Some other lib might bring libcurl already
            find_package(CURL REQUIRED)
        endif()

        target_link_libraries(crashpad_util PRIVATE CURL::libcurl)
        SET(HTTP_TRANSPORT_IMPL net/http_transport_libcurl.cc)]]
)

set(socket "        SET(HTTP_TRANSPORT_IMPL net/http_transport_socket.cc)")
string(
    FIND "${text}"
    "${curl}"
    at
)
string(
    FIND "${text}"
    "${socket}"
    made
)
if(at EQUAL -1 AND made EQUAL -1)
    message(FATAL_ERROR "crashpadpatch.cmake: ${file} no longer asks for libcurl in the words it replaces")
endif()
string(
    REPLACE "${curl}"
    "${socket}"
    text
    "${text}"
)
file(WRITE ${file} "${text}")

# **A signal stack of 64 KiB, where Crashpad sizes it by `SIGSTKSZ`.** Built against glibc 2.28, the release
# base's, `SIGSTKSZ` is the constant 8 KiB, too small for a signal frame and the handler on a CPU whose
# vector state is wide, and a crash in the game overflowed it into its guard page, so the monitor got no
# report; a newer glibc reads it from the kernel at run time. Each of its four spellings, or the configure
# stops by name.
set(file client/crashpad_client_linux.cc)
file(READ ${file} text)
set(sized "(SIGSTKSZ + page_size - 1)")
set(widened "((SIGSTKSZ > 65536 ? SIGSTKSZ : 65536) + page_size - 1)")
string(
    REGEX MATCHALL "\\(SIGSTKSZ \\+ page_size - 1\\)"
    found
    "${text}"
)
string(
    REGEX MATCHALL "\\(\\(SIGSTKSZ > 65536 \\? SIGSTKSZ : 65536\\) \\+ page_size - 1\\)"
    done
    "${text}"
)
list(LENGTH found count)
list(LENGTH done made)
math(EXPR count "${count} + ${made}")
if(NOT count EQUAL 4)
    message(FATAL_ERROR "crashpadpatch.cmake: ${file} sizes its signal stack ${count} times, not the 4 it replaces")
endif()
string(
    REPLACE "${sized}"
    "${widened}"
    text
    "${text}"
)
file(WRITE ${file} "${text}")
