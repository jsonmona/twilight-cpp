#ifndef COMMON_BYTE_VECTOR_H_
#define COMMON_BYTE_VECTOR_H_


#include <cstdint>
#include <cstring>
#include <type_traits>

class ByteBuffer {
	uint8_t* ptr;
	size_t nowSize;

public:
	template<class T>
	class View {
		static_assert(std::is_trivial<T>::value, "Only trivial types can be used!");
		ByteBuffer* parent;
		
	public:
		explicit View(ByteBuffer* _parent) : parent(_parent) {}

		size_t size() const {
			return parent->nowSize / sizeof(T);
		}

		T* begin() {
			return reinterpret_cast<T*>(parent->ptr);
		}

		T* end() {
			return reinterpret_cast<T*>(parent->ptr) + size();
		}
	};

public:
	ByteBuffer() : ptr(nullptr), nowSize(0) {}
	ByteBuffer(size_t initialSize) : ptr(nullptr), nowSize(0) { resize(initialSize); }
	ByteBuffer(const ByteBuffer& copy) = delete;
	ByteBuffer(ByteBuffer&& move) : ptr(nullptr), nowSize(0) {
		std::swap(ptr, move.ptr);
		std::swap(nowSize, move.nowSize);
	}

	ByteBuffer& operator=(const ByteBuffer& copy) = delete;
	ByteBuffer& operator=(ByteBuffer&& move) {
		std::swap(ptr, move.ptr);
		std::swap(nowSize, move.nowSize);
		return *this;
	}

	~ByteBuffer() {
		free(ptr);
	}

	ByteBuffer clone() {
		ByteBuffer ret;
		if (size() != 0) {
			ret.resize(size());
			memcpy(ret.data(), data(), size());
		}
	}

	void resize(size_t newSize) {
		if (newSize == 0) {
			free(ptr);
			ptr = nullptr;
			nowSize = 0;
		}
		else if (nowSize != newSize) {
			void* newPtr = realloc(ptr, newSize);
			if (newPtr == nullptr)
				abort();  //FIXME: Use of abort
			ptr = reinterpret_cast<uint8_t*>(newPtr);
			nowSize = newSize;
		}
	}

	// removes content near begin
	void shiftTowardBegin(size_t amount) {
		if(amount != 0)
			memmove(ptr, ptr + amount, nowSize - amount);
	}

	// removes content near end
	void shiftTowardEnd(size_t amount) {
		if(amount != 0)
			memmove(ptr + amount, ptr, nowSize - amount);
	}

	void write(size_t dstOffset, void* src, size_t length) {
		memcpy(ptr + dstOffset, src, length);
	}

	void write(size_t dstOffset, const ByteBuffer& other) {
		memcpy(ptr + dstOffset, other.data(), other.size());
	}

	size_t size() const { return nowSize; }

	uint8_t* data() { return ptr; }
	uint8_t* begin() { return ptr; }
	uint8_t* end() { return ptr + size(); }

	const uint8_t* data() const { return ptr; }
	const uint8_t* begin() const { return ptr; }
	const uint8_t* end() const { return ptr + size(); }

	char* data_char() { return reinterpret_cast<char*>(ptr); }
	char* begin_char() { return reinterpret_cast<char*>(ptr); }
	char* end_char() { return reinterpret_cast<char*>(ptr + size()); }

	const char* data_char() const { return reinterpret_cast<const char*>(ptr); }
	const char* begin_char() const { return reinterpret_cast<const char*>(ptr); }
	const char* end_char() const { return reinterpret_cast<const char*>(ptr + size()); }

	template<class T>
	View<T> view() { return View<T>(this); }
};


#endif