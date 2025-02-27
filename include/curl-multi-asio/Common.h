#ifndef CURLMULTIASIO_COMMON_H_
#define CURLMULTIASIO_COMMON_H_

// disable all that extra windows clutter and
// name definitions
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#endif

#include <boost/asio.hpp>
// #include <boost/asio/as_tuple.hpp>
// #include <boost/asio/experimental/channel.hpp>

// curl includes
#include <curl/curl.h>

#ifdef _DEBUG
#define NOEXCEPT_RELEASE
#else
#define NOEXCEPT_RELEASE noexcept
#endif

#endif