#ifndef CURLMULTIASIO_EASY_H_
#define CURLMULTIASIO_EASY_H_

/// @file
/// cURL Easy Handle
/// 6/21/21 15:26

// curl-multi-asio includes
#include <curl-multi-asio/Common.h>
#include <curl-multi-asio/Detail/Lifetime.h>
#include <curl-multi-asio/Error.h>

// expected includes
#include <expected>

// STL includes
#include <initializer_list>
#include <memory>
#include <ostream>
#include <span>
#include <utility>

/// @brief This concept detects any type, such as std::string,
/// or std::vector<char>, that can accept characters as input
template<typename T>
concept AcceptsCharacters = requires(T a)
{
	{ a.data() } -> std::same_as<char*>;
	{ a.resize(std::declval<size_t>()) };
	{ a.size() } -> std::same_as<size_t>;
};
/// @brief An iterator to std::pair<std::string_view, std::string_view>
template<typename T>
concept DataIterator = requires(T a)
{
	++a;
	{ *a } -> std::convertible_to<const std::pair<std::string_view, std::string_view>&>;
};
/// @brief This concept detects whether or not a type is an ostream.
template<typename T>
concept IsOstream = std::is_base_of_v<std::ostream, T>;

namespace cma
{
	class Multi;

	/// @brief Easy is a wrapper around an easy CURL handle
	class Easy
	{
	public:
		struct Header
		{
			std::string_view key;
			std::string_view value;
		};
		struct DefaultBuffer {};
		struct NullBuffer {};

		/// @brief Creates an easy CURL handle by curl_easy_init.
		Easy() noexcept;
		/// @brief Destroys the easy CURL handle by curl_easy_cleanup
		~Easy() = default;
		/// @brief Duplicates the easy handle
		/// @param other The handle to duplicate from
		Easy(const Easy& other) noexcept;
		/// @brief Diplicates the easy handle
		/// @param other The handle to duplicate from
		/// @return This handle
		Easy& operator=(const Easy& other) noexcept;
		/// @brief Moves the easy handle
		/// @param other The other easy handle
		Easy(Easy&& other) = default;
		/// @brief Moves the easy handle
		/// @param other The other easy handle
		/// @return This handle
		Easy& operator=(Easy&& other) = default;

		/// @return The native handle
		inline CURL* GetNativeHandle() const noexcept { return m_nativeHandle.get(); }

		/// @brief Perform the request
		/// @return The resulting code
		inline error_code Perform() noexcept
		{
			return curl_easy_perform(GetNativeHandle());
		}

		/// @brief Adds a header to the request
		/// @param headerStr The header string
		/// @return The success of the operation
		bool AddHeaderStr(const char* headerStr) noexcept;
		/// @brief Adds a header k/v pair to the request
		/// @param header The header key and value
		/// @return The success of the operation
		bool AddHeader(std::pair<std::string_view, std::string_view> header) noexcept;
		/// @brief Clears the custom headers from the cURL request
		inline void ClearHeaders() noexcept { m_headerList.reset(); }

		/// @brief Gets info from the easy handle
		/// @tparam T The data type
		/// @param info The info
		/// @param out The output instance
		/// @return The resulting error
		template<typename T>
		inline error_code GetInfo(CURLINFO info, T& out) noexcept
		{
			return curl_easy_getinfo(GetNativeHandle(), info, &out);
		}
		/// @brief Sets the easy handle to not use the default buffer
		/// @return The resulting error
		error_code SetBuffer(DefaultBuffer) noexcept;
		/// @brief Sets the easy handle to not use any buffer
		/// @return The resulting error
		error_code SetBuffer(NullBuffer) noexcept;
		/// @brief Gets info from the easy handle
		/// @tparam T The data type
		/// @param info The info
		/// @return An instance to the type, or the error
		template<typename T>
		inline std::expected<T, error_code> GetInfo(CURLINFO info) noexcept
		{
			T inst;
			if (auto res = GetInfo(info, inst); res)
				return std::unexpected(res);
			// no copy elision here. move it into the expected
			return std::move(inst);
		}
		/// @brief Sets a buffer that either accepts appending strings
		/// or is an ostream. The buffer must stay in scope until the
		/// call to Perform, otherwise the call will result in undefied behavior
		/// @param buffer The buffer
		/// @return The resulting error
		template<typename T>
		error_code SetBuffer(T& buffer) noexcept requires
			AcceptsCharacters<T> || IsOstream<T>
		{
			// set the buffer first in case it fails, to avoid potential
			// calls with a null buffer
			if (const auto err = SetOption(CURLoption::CURLOPT_WRITEDATA,
				&buffer); err)
				return err;
			return SetOption(CURLoption::CURLOPT_WRITEFUNCTION, WriteCb<T>);
		}
		/// @brief Sets an option on the easy handle
		/// @tparam T The value type
		/// @param option The option
		/// @param value The value
		/// @return The resulting error
		template<typename T>
		inline error_code SetOption(CURLoption option, T&& value) noexcept
		{
			// weird GCC bug where forward thinks its return value is ignored
			return curl_easy_setopt(GetNativeHandle(), option, static_cast<T&&>(value));
		}
		/// @brief Sets post data to the data, and sets method to POST. 
		/// Per cURL docs, it also sets the data type in the header to
		/// url-encoded. So if you use this, make sure you set the header
		/// to whatever data type you are sending
		/// @param postData The raw data
		/// @return The resulting error code
		template<typename Str>
		error_code SetPOSTData(Str&& postData) noexcept
		{
			// set the method to POST
			if (const auto res = SetOption(CURLoption::CURLOPT_POST, 1L); res)
				return res;
			// set the POST data
			m_postData = std::forward<Str>(postData);
			// and set the field + size
			if (const auto res = SetOption(CURLoption::CURLOPT_POSTFIELDSIZE,
				m_postData.size()); res)
				return res;
			return SetOption(CURLoption::CURLOPT_POSTFIELDS, m_postData.c_str());
		}
		/// @brief Sets post data to the url-encoded data, and sets
		/// the method to POST
		/// @param urlEncodedData The url-encoded data
		/// @return The resulting error code
		inline error_code SetPOSTData(std::span<std::pair<std::string_view,
			std::string_view>> urlEncodedData) noexcept
		{
			return SetPOSTData(URLEncode(urlEncodedData.begin(), urlEncodedData.end()));
		}
		/// @brief Sets post data to the url-encoded data, and sets
		/// the method to POST
		/// @param urlEncodedData The url-encoded data
		/// @return The resulting error code
		inline error_code SetPOSTData(std::initializer_list<std::pair<
			std::string_view, std::string_view>> urlEncodedData) noexcept
		{
			return SetPOSTData(URLEncode(urlEncodedData.begin(), urlEncodedData.end()));
		}
		/// @brief Erases the POST data and reverts back to the previous
		/// REST method
		/// @return The resulting error code
		inline error_code SetPOSTData(NullBuffer) noexcept
		{
			// just disable post
			return SetOption(CURLoption::CURLOPT_POST, 0L);
		}
		/// @brief Sets the URL to traverse
		/// @param url The URL
		/// @return The resulting error
		inline error_code SetURL(const char* url) noexcept
		{
			return SetOption(CURLoption::CURLOPT_URL, url);
		}
		/// @brief Sets the URL to traverse, with urlencoded parameters
		/// @tparam Str The string type
		/// @param url The URL
		/// @param urlEncodedParams The URLencoded parameters
		/// @return The resulting error
		template<typename Str>
		inline error_code SetURL(Str&& url, std::span<
			std::pair<std::string_view, std::string_view>> urlEncodedParams) noexcept
		{
			return SetURL((std::string(url) + '?' + URLEncode(
				urlEncodedParams.begin(), urlEncodedParams.end())).c_str());
		}
		/// @brief Sets the URL to traverse, with urlencoded parameters
		/// @tparam Str The string type
		/// @param url The URL
		/// @param urlEncodedParams The URLencoded parameters
		/// @return The resulting error
		template<typename Str>
		inline error_code SetURL(Str&& url, std::initializer_list<
			std::pair<std::string_view, std::string_view>> urlEncodedParams) noexcept
		{
			return SetURL((std::string(url) + '?' + URLEncode(
				urlEncodedParams.begin(), urlEncodedParams.end())).c_str());
		}

		/// @return Whether or not the handle is valid
		inline operator bool() const noexcept { return m_nativeHandle != nullptr; }
	private:
		/// @brief URL-encodes key-value pairs
		/// @param begin The starting iterator of the data
		/// @param end The ending iterator of the data
		/// @return The URL-encoded string
		template<DataIterator It>
		static std::string URLEncode(It begin, It end)
		{
			std::string result;
			for (It it = begin; it != end; ++it)
			{
				if (it != begin)
					result += '&';
				result += (*it).first;
				result += '=';
				result += (*it).second;
			}
			return result;
		}

		/// @brief The write callback for ostreams. For a
		/// description of each argument, check cURL docs for
		/// CURLOPT_WRITEFUNCTION
		/// @return The numver of bytes taken care of
		template<IsOstream T>
		static size_t WriteCb(char* ptr, size_t size, size_t nmemb, std::ostream* buffer) noexcept
		{
			buffer->write(ptr, nmemb);
			return nmemb;
		}
		/// @brief The write callback for string buffers. For a 
		/// description of each argument, check cURL docs for
		/// CURLOPT_WRITEFUNCTION
		/// @return The number of bytes taken care of
		template<AcceptsCharacters T>
		static size_t WriteCb(char* ptr, size_t size, size_t nmemb, T* buffer) noexcept
		{
			const size_t oldSize = buffer->size();
			// allocate space for the new data
			buffer->resize(buffer->size() + nmemb);
			std::copy(ptr, ptr + nmemb, &buffer->data()[oldSize]);
			return nmemb;
		}
		/// @brief The write callback for null buffers. For a 
		/// description of each argument, check cURL docs for
		/// CURLOPT_WRITEFUNCTION
		/// @return The number of bytes taken care of
		template<typename T>
		static size_t WriteCb(char* ptr, size_t size, size_t nmemb, T* buffer) requires(std::is_same_v<T, NullBuffer>)
		{
			return nmemb;
		}

#ifdef CMA_MANAGE_CURL
		Detail::Lifetime m_lifeTime;
#endif
		std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> m_nativeHandle;
		std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> m_headerList;
		std::string m_postData;
	};
}

#endif