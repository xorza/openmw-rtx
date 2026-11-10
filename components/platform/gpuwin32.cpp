#include "gpu.hpp"

#include <algorithm>

#include <Windows.h>
#include <combaseapi.h>
#include <dxgi.h>

namespace Platform::Gpu
{
    std::vector<Vendor> vendors()
    {
        std::vector<Vendor> found;

        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || factory == nullptr)
            return found;

        for (UINT at = 0;; ++at)
        {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(at, &adapter) == DXGI_ERROR_NOT_FOUND || adapter == nullptr)
                break;

            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)))
                found.push_back(vendorOf(description.VendorId));
            adapter->Release();
        }

        factory->Release();
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

}
