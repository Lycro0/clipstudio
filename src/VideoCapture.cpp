#include "VideoCapture.hpp"

#include <unknwn.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <inspectable.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

struct VideoCapture::Impl {
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	wgd3d::IDirect3DDevice rtDevice{nullptr};

	wgc::GraphicsCaptureItem item{nullptr};
	wgc::Direct3D11CaptureFramePool pool{nullptr};
	wgc::GraphicsCaptureSession session{nullptr};
	wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameRevoker;
	wgc::GraphicsCaptureItem::Closed_revoker closedRevoker;
	winrt::Windows::Graphics::SizeInt32 poolSize{};

	winrt::com_ptr<ID3D11Texture2D> staging;
	int stagingW = 0, stagingH = 0;

	std::mutex frameMutex;
	VideoFrame latest;
	std::atomic<bool> active{false};
	HWND hwnd = nullptr;

	bool EnsureDevice()
	{
		if (device)
			return true;
		D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
					       D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
					       D3D11_SDK_VERSION, device.put(), nullptr, context.put());
		if (FAILED(hr))
			return false;

		if (auto mt = device.try_as<ID3D11Multithread>())
			mt->SetMultithreadProtected(TRUE);

		auto dxgi = device.as<IDXGIDevice>();
		winrt::com_ptr<::IInspectable> insp;
		if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put())))
			return false;
		rtDevice = insp.as<wgd3d::IDirect3DDevice>();
		return true;
	}

	bool Start(bool cursor)
	{
		poolSize = item.Size();
		pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
			rtDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
		frameRevoker = pool.FrameArrived(winrt::auto_revoke, [this](auto &&, auto &&) { OnFrame(); });
		closedRevoker = item.Closed(winrt::auto_revoke, [this](auto &&, auto &&) { active = false; });
		session = pool.CreateCaptureSession(item);
		try {
			session.IsCursorCaptureEnabled(cursor);
		} catch (...) {
		}
		try {
			// Windows 11+: hide the yellow capture border.
			if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
				    L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
				session.IsBorderRequired(false);
		} catch (...) {
		}
		session.StartCapture();
		active = true;
		return true;
	}

	void OnFrame()
	{
		auto frame = pool.TryGetNextFrame();
		if (!frame)
			return;

		auto size = frame.ContentSize();
		if (size.Width != poolSize.Width || size.Height != poolSize.Height) {
			poolSize = size;
			pool.Recreate(rtDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
		}
		if (size.Width <= 0 || size.Height <= 0)
			return;

		auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
		winrt::com_ptr<ID3D11Texture2D> tex;
		if (FAILED(access->GetInterface(IID_PPV_ARGS(tex.put()))))
			return;

		D3D11_TEXTURE2D_DESC desc;
		tex->GetDesc(&desc);
		int w = std::min<int>(size.Width, (int)desc.Width);
		int h = std::min<int>(size.Height, (int)desc.Height);

		if (!staging || stagingW != w || stagingH != h) {
			D3D11_TEXTURE2D_DESC sd = {};
			sd.Width = (UINT)w;
			sd.Height = (UINT)h;
			sd.MipLevels = 1;
			sd.ArraySize = 1;
			sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			sd.SampleDesc.Count = 1;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			staging = nullptr;
			if (FAILED(device->CreateTexture2D(&sd, nullptr, staging.put())))
				return;
			stagingW = w;
			stagingH = h;
		}

		D3D11_BOX box = {0, 0, 0, (UINT)w, (UINT)h, 1};
		context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, tex.get(), 0, &box);

		D3D11_MAPPED_SUBRESOURCE map;
		if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map)))
			return;
		{
			std::lock_guard<std::mutex> lock(frameMutex);
			latest.width = w;
			latest.height = h;
			latest.bgra.resize((size_t)w * h * 4);
			const uint8_t *src = (const uint8_t *)map.pData;
			for (int y = 0; y < h; y++)
				memcpy(latest.bgra.data() + (size_t)y * w * 4, src + (size_t)y * map.RowPitch, (size_t)w * 4);
			latest.serial++;
		}
		context->Unmap(staging.get(), 0);
	}

	void Stop()
	{
		active = false;
		frameRevoker.revoke();
		closedRevoker.revoke();
		if (session) {
			session.Close();
			session = nullptr;
		}
		if (pool) {
			pool.Close();
			pool = nullptr;
		}
		item = nullptr;
		hwnd = nullptr;
	}
};

VideoCapture::VideoCapture() : d(std::make_unique<Impl>()) {}

VideoCapture::~VideoCapture()
{
	Stop();
}

bool VideoCapture::IsSupported()
{
	try {
		return wgc::GraphicsCaptureSession::IsSupported();
	} catch (...) {
		return false;
	}
}

bool VideoCapture::StartWindow(HWND hwnd, bool cursor)
{
	Stop();
	try {
		if (!d->EnsureDevice())
			return false;
		auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
							      winrt::put_abi(d->item)));
		d->hwnd = hwnd;
		return d->Start(cursor);
	} catch (...) {
		d->Stop();
		return false;
	}
}

bool VideoCapture::StartMonitor(HMONITOR monitor, bool cursor)
{
	Stop();
	try {
		if (!d->EnsureDevice())
			return false;
		auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		winrt::check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(),
							       winrt::put_abi(d->item)));
		return d->Start(cursor);
	} catch (...) {
		d->Stop();
		return false;
	}
}

void VideoCapture::Stop()
{
	try {
		d->Stop();
	} catch (...) {
	}
}

bool VideoCapture::GetLatest(VideoFrame &out)
{
	std::lock_guard<std::mutex> lock(d->frameMutex);
	if (d->latest.serial == 0 || d->latest.serial == out.serial)
		return false;
	out.width = d->latest.width;
	out.height = d->latest.height;
	out.bgra = d->latest.bgra;
	out.serial = d->latest.serial;
	return true;
}

HWND VideoCapture::Window() const
{
	return d->hwnd;
}

bool VideoCapture::Active() const
{
	return d->active;
}
