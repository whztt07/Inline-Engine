#pragma once

#include "../GraphicsApi_LL/IDescriptorHeap.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <wrl.h>
#include <d3d12.h>
#include "DisableWin32Macros.h"

namespace inl {
namespace gxapi_dx12 {

using Microsoft::WRL::ComPtr;

class DescriptorHeap : public gxapi::IDescriptorHeap {
public:
	DescriptorHeap(ComPtr<ID3D12DescriptorHeap>& native);
	DescriptorHeap(const DescriptorHeap&) = delete;
	DescriptorHeap& operator=(const DescriptorHeap&) = delete;


	gxapi::DescriptorHandle At(size_t index) const override;

	gxapi::DescriptorHeapDesc GetDesc() const override;

private:
	ComPtr<ID3D12DescriptorHeap> m_native;
};


} // namespace gxapi_dx12
} // namespace inl