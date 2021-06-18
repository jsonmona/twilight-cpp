#ifndef DXGI_WRAPPER_H_
#define DXGI_WRAPPER_H_


#include "winheaders.h"

#include <memory>
#include <cassert>
#include <type_traits>


template<class T>
class ComWrapper {
	T* obj;
	
	template<class U>
	friend class ComWrapper;

	// Automatically converted to either T** or void**
	// With additional safeguard for memory leak (only on debug build)
	struct DoublePtrProxy {
		ComWrapper<T>* parent;
#ifndef NDEBUG
		T* prev;
#endif

		DoublePtrProxy(ComWrapper<T>* _parent) : parent(_parent) {
#ifndef NDEBUG
			prev = parent->obj;
#endif
		}
		operator T** () {
			return &parent->obj;
		}
		operator void** () {
			return (void**) &parent->obj;
		}
		~DoublePtrProxy() {
#ifndef NDEBUG
			if (prev != nullptr && prev != parent->obj)
				abort();  // Using abort() since this only triggers in debug builds.
#endif
		}
	};

public:
	~ComWrapper() {
		static_assert(std::is_convertible<T*, IUnknown*>::value, "T must be subclass of IUnknown");
		if (obj != nullptr)
			obj->Release();
	}

	ComWrapper() : obj(nullptr) {}
	explicit ComWrapper(T* ptr) : obj(ptr) {}
	ComWrapper(const ComWrapper& copy) : obj(copy.obj) {
		if(obj != nullptr)
			obj->AddRef();
	}
	ComWrapper(ComWrapper&& move) noexcept : obj(move.obj) {
		move.obj = nullptr;
	}
	ComWrapper<T>& operator=(const ComWrapper<T>& copy) {
		release();
		obj = copy.obj;
		if(obj != nullptr)
			obj->AddRef();
		return *this;
	}
	ComWrapper<T>& operator=(ComWrapper<T>&& move) noexcept {
		release();
		obj = move.obj;
		move.obj = nullptr;
		return *this;
	}
	ComWrapper<T>& operator=(T* rawPtr) {
		release();
		obj = rawPtr;
		return *this;
	}

	void release() {
		if (obj != nullptr) {
			obj->Release();
			obj = nullptr;
		}
	}

	DoublePtrProxy data() {
		return DoublePtrProxy(this);
	}

	T* operator->() const {
		return obj;
	}
	T* ptr() const {
		return obj;
	}
	T* detach() {
		T* ret = obj;
		obj = nullptr;
		return ret;
	}
	GUID guid() const {
		return __uuidof(T);
	}

	bool operator==(IUnknown* x) const {
		return obj == x;
	}
	bool isValid() const {
		return obj != nullptr;
	}
	bool isInvalid() const {
		return !isValid();
	}

	template<class U>
	ComWrapper<U> castTo() const {
		static_assert(std::is_convertible<U*, IUnknown*>::value, "Template argument should be COM type");

		if (obj != nullptr) {
			ComWrapper<U> ret;
			obj->QueryInterface(__uuidof(ret.obj), (void**)&ret.obj);
			return ret;
		}
		else {
			return ComWrapper<U>();
		}
	}
};


using DxgiFactory5 = ComWrapper<IDXGIFactory5>;
using DxgiAdapter1 = ComWrapper<IDXGIAdapter1>;
using DxgiOutput = ComWrapper<IDXGIOutput>;
using DxgiOutput5 = ComWrapper<IDXGIOutput5>;
using DxgiOutputDuplication = ComWrapper<IDXGIOutputDuplication>;
using DxgiResource = ComWrapper<IDXGIResource>;

using D3D11Device = ComWrapper<ID3D11Device>;
using D3D11DeviceContext = ComWrapper<ID3D11DeviceContext>;
using D3D11Texture2D = ComWrapper<ID3D11Texture2D>;
using D3D11VertexShader = ComWrapper<ID3D11VertexShader>;
using D3D11PixelShader = ComWrapper<ID3D11PixelShader>;
using D3D11InputLayout = ComWrapper<ID3D11InputLayout>;
using D3D11Buffer = ComWrapper<ID3D11Buffer>;
using D3D11RenderTargetView = ComWrapper<ID3D11RenderTargetView>;
using D3D11ShaderResourceView = ComWrapper<ID3D11ShaderResourceView>;
using D3D11SamplerState = ComWrapper<ID3D11SamplerState>;

using D3DBlob = ComWrapper<ID3DBlob>;

using MFDxgiDeviceManager = ComWrapper<IMFDXGIDeviceManager>;
using MFTransform = ComWrapper<IMFTransform>;
using MFMediaType = ComWrapper<IMFMediaType>;
using MFAttributes = ComWrapper<IMFAttributes>;
using MFMediaEventGenerator = ComWrapper<IMFMediaEventGenerator>;
using MFMediaEvent = ComWrapper<IMFMediaEvent>;
using MFMediaBuffer = ComWrapper<IMFMediaBuffer>;
using MFSample = ComWrapper<IMFSample>;


#endif