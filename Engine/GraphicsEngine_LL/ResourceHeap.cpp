#include "ResourceHeap.hpp"

#include "GpuBuffer.hpp"
#include "CopyCommandList.hpp"


namespace inl {
namespace gxeng {
namespace impl {


CriticalBufferHeap::CriticalBufferHeap(gxapi::IGraphicsApi * graphicsApi) :
	m_graphicsApi(graphicsApi)
{}


gxapi::IResource* CriticalBufferHeap::Allocate(gxapi::ResourceDesc desc) {
	return m_graphicsApi->CreateCommittedResource(
		gxapi::HeapProperties(gxapi::eHeapType::DEFAULT),
		gxapi::eHeapFlags::NONE,
		desc,
		gxapi::eResourceState::COMMON
	);
}


void CriticalBufferHeap::ReleaseUnderlying(GenericResource* owner) {
	//m_resources.erase(owner);
}


} // namespace impl


BackBufferHeap::BackBufferHeap(gxapi::IGraphicsApi* graphicsApi, gxapi::ISwapChain* swapChain) :
	m_graphicsApi(graphicsApi),
	m_swapChain(swapChain)
{
	const unsigned numBuffers = swapChain->GetDesc().numBuffers;

	{
		gxapi::DescriptorHeapDesc heapDesc;
		heapDesc.isShaderVisible = false;
		heapDesc.numDescriptors = numBuffers;
		heapDesc.type = gxapi::eDesriptorHeapType::RTV;
		m_descriptorHeap.reset(m_graphicsApi->CreateDescriptorHeap(heapDesc));
	}

	m_backBuffers.reserve(numBuffers);
	for (unsigned i = 0; i < numBuffers; i++) {
		gxapi::DescriptorHandle descHandle = m_descriptorHeap->At(i);
		gxapi::IResource* lowLeveBuffer = swapChain->GetBuffer(i);
		m_graphicsApi->CreateRenderTargetView(lowLeveBuffer, descHandle);

		DescriptorReference descRef;
		descRef.m_handle = descHandle;
		descRef.m_deleter = nullptr; // Descriptors needn't be freed until this heap exists.

		// Underlying resource deallocation is managed by the swap chain!
		Texture2D highLevelBuffer(std::move(descRef), lowLeveBuffer, [](gxapi::IResource*){});
		highLevelBuffer._SetResident(true); // I guess...

		m_backBuffers.push_back(std::move(highLevelBuffer));
	}
}


Texture2D& BackBufferHeap::GetBackBuffer(unsigned index) {
	return m_backBuffers.at(index);
}


UploadHeap::UploadHeap(gxapi::IGraphicsApi* graphicsApi) :
	m_graphicsApi(graphicsApi)
{}


void UploadHeap::UploadToResource(gxeng::CopyCommandList& cmdList, LinearBuffer& target, const void* data, size_t size) {
	if (target.GetSize() < size) {
		throw inl::gxapi::InvalidArgument("Target buffer is not large enough for the uploaded data to fit.", "target");
	}

	gxeng::DescriptorReference desc;
	desc.m_deleter = nullptr;
	desc.m_handle.cpuAddress = nullptr;
	desc.m_handle.gpuAddress = nullptr;

	gxapi::IResource* stagedRes = m_graphicsApi->CreateCommittedResource(
		gxapi::HeapProperties(gxapi::eHeapType::UPLOAD),
		gxapi::eHeapFlags::NONE,
		gxapi::ResourceDesc::Buffer(size),
		gxapi::eResourceState::COPY_DEST
	);

	m_stagedResources.push_back(
		GenericResource(
			std::move(desc),
			stagedRes
		)
	);
	GenericResource& staged = m_stagedResources.back();

	staged._SetResident(true);

	gxapi::MemoryRange noReadRange{0, 0};
	void* stagePtr = stagedRes->Map(0, &noReadRange);
	memcpy(stagePtr, data, size);
	// No need to unmap (see https://msdn.microsoft.com/en-us/library/windows/desktop/dn788712(v=vs.85).aspx)

	{
		gxapi::TransitionBarrier stageToSrc;
		stageToSrc.beforeState = gxapi::eResourceState::COPY_DEST;
		stageToSrc.afterState = gxapi::eResourceState::COPY_SOURCE;
		stageToSrc.resource = stagedRes;
		stageToSrc.subResource = 0xffffffff;
		stageToSrc.splitMode = gxapi::eResourceBarrierSplit::NORMAL;

		// Set stage's state to copy source
		cmdList.ResourceBarrier(stageToSrc);
		//Set target state to copy destination
		cmdList.RegisterResourceTransition(SubresourceID(target._GetResourcePtr(), 0xffffffff), gxapi::eResourceState::COPY_DEST);
		//Copy
		cmdList.CopyBuffer(&target, 0, &staged, 0, size);
	}
}


} // namespace gxeng
} // namespace inl
