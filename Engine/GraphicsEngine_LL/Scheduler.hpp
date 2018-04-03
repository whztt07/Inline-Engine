#pragma once

#include "GraphicsNode.hpp"
#include "Pipeline.hpp"
#include "FrameContext.hpp"
#include "ScratchSpacePool.hpp"
#include "CommandListPool.hpp"
#include "MemoryObject.hpp"

#include <GraphicsApi_LL/IFence.hpp>
#include <GraphicsApi_LL/Common.hpp>
#include <BaseLibrary/JobSystem/Task.hpp>
#include <BaseLibrary/JobSystem/ThreadpoolScheduler.hpp>
#include <memory>
#include <cstdint>
#include <vector>
#include <optional>

namespace inl {
namespace gxeng {



class Scheduler {
public:
	Scheduler();

	/// <summary> Currently active pipeline contains the nodes that are executed each frame. </summary>
	/// <remarks> The pipeline cannot be modified outside the scheduler, hence the exclusive access. </remarks>
	void SetPipeline(Pipeline&& pipeline);

	/// <summary> You can read information about currently used pipeline. </summary>
	const Pipeline& GetPipeline() const;

	/// <summary> You can regain ownership of the pipeline and leave the scheduler with an empty pipeline. </summary>
	Pipeline ReleasePipeline();

	/// <summary> Runs the currently bound pipeline nodes using information about the frame. </summary>
	void Execute(FrameContext context) {
		ExecuteParallel(context); // Only dry-run
		ExecuteSerial(context); // Real drawing
	}

	/// <summary> Instructs all pipeline nodes to release their resources related to rendering. </summary>
	/// <remarks> This can be called to free resources before resizing the swapchain.
	///		First, references to the swapchain are dropped, second, GPU memory will be freed
	///		so that old resources won't prevent new ones from being allocated. </remarks>
	void ReleaseResources();
protected:
	struct UsedResource {
		MemoryObject* resource;
		unsigned subresource;
		float firstState;
		bool multipleUse;
	};

	//--------------------------------------------
	// Multi-threaded rendering
	//--------------------------------------------
	void ExecuteParallel(FrameContext context);


	//--------------------------------------------
	// Single threaded rendering
	//--------------------------------------------
	void ExecuteSerial(FrameContext context);

	static std::vector<GraphicsTask*> MakeSchedule(const lemon::ListDigraph& taskGraph,
												   const lemon::ListDigraph::NodeMap<GraphicsTask*>& taskFunctionMap
													/*std::vector<CommandQueue*> queues*/);

	//--------------------------------------------
	// Utilities
	//--------------------------------------------

	// Enqueues a command list into the command queue, and enqueues init and clean tasks
	// for given command list. Also sets up synchronization between init, gpu and clean.
	static void EnqueueCommandList(CommandQueue& commandQueue,
								   CmdListPtr commandList,
								   CmdAllocPtr commandAllocator,
								   std::vector<ScratchSpacePtr> scratchSpaces,
								   std::vector<MemoryObject> usedResources,
								   std::unique_ptr<VolatileViewHeap> volatileHeap,
								   const FrameContext& context);


	// Command lists (gxeng, not gxapi) do not issue resource barriers for the first time SetResourceState is called.
	// Instead, these states are recorded, and must be "patched in", that is, issued before said command list
	// by the scheduler. This function gives the list of barriers to issue.
	template <class UsedResourceIter>
	static std::vector<gxapi::ResourceBarrier> InjectBarriers(UsedResourceIter firstResource, UsedResourceIter lastResource);

	// Goes over the list of resource usages of a command list and updates CPU-side resource state tracking accordingly.
	// Iterator points to UsedResources.
	template <class UsedResourceIter>
	static void UpdateResourceStates(UsedResourceIter firstResource, UsedResourceIter lastResource);

	// Check if two GPU command lists can execute asynchronously.
	// Using the same resources in different states prohibits that.
	template <class UsedResourceIter1, class UsedResourceIter2>
	static bool CanExecuteParallel(UsedResourceIter1 first1, UsedResourceIter1 last1, UsedResourceIter2 first2, UsedResourceIter2 last2);


	//--------------------------------------------
	// Failure handling
	//--------------------------------------------
	static void RenderFailureScreen(FrameContext context);
private:
	Pipeline m_pipeline;
	jobs::ThreadpoolScheduler m_jobScheduler;
private:
	class UploadTask : public GraphicsTask {
	public:
		UploadTask(const std::vector<UploadManager::UploadDescription>* uploads) : m_uploads(uploads) {}
		void Setup(SetupContext& context) override;
		void Execute(RenderContext& context) override;
	private:
		const std::vector<UploadManager::UploadDescription>* m_uploads;
	};
};



template <class UsedResourceIter>
std::vector<gxapi::ResourceBarrier> Scheduler::InjectBarriers(UsedResourceIter firstResource, UsedResourceIter lastResource) {
	std::vector<gxapi::ResourceBarrier> barriers;

	// Collect all necessary barriers.
	for (UsedResourceIter it = firstResource; it != lastResource; ++it) {
		MemoryObject& resource = it->resource;
		unsigned subresource = it->subresource;
		gxapi::eResourceState targetState = it->firstState;

		if (subresource != gxapi::ALL_SUBRESOURCES) {
			gxapi::eResourceState sourceState = resource.ReadState(subresource);
			if (sourceState != targetState) {
				barriers.push_back(gxapi::TransitionBarrier{ resource._GetResourcePtr(), sourceState, targetState, subresource });
			}
		}
		else {
			for (unsigned subresourceIdx = 0; subresourceIdx < resource.GetNumSubresources(); ++subresourceIdx) {
				gxapi::eResourceState sourceState = resource.ReadState(subresourceIdx);
				if (sourceState != targetState) {
					barriers.push_back(gxapi::TransitionBarrier{ resource._GetResourcePtr(), sourceState, targetState, subresourceIdx });
				}
			}
		}
	}

	return barriers;
}


template <class UsedResourceIter1, class UsedResourceIter2>
bool Scheduler::CanExecuteParallel(UsedResourceIter1 first1, UsedResourceIter1 last1, UsedResourceIter2 first2, UsedResourceIter2 last2) {
	UsedResourceIter1 it1 = first1;
	UsedResourceIter2 it2 = first2;

	// Advance the two iterators on the sorted ranges simultaneously.
	while (it1 != last1 && it2 != last2) {
		if (MemoryObject::PtrLess(it1->resource, it2->resource)) {
			++it1;
		}
		else if (MemoryObject::PtrGreater(it1->resource, it2->resource)) {
			++it2;
		}
		else {
			// If the resources are the same, but uses are incompatible, return false.
			if (it1->firstState != it2->firstState
				|| it1->multipleUse
				|| it2->multipleUse)
			{
				return false;
			}
			++it1;
			++it2;
		}
	}

	return true;
}


template <class UsedResourceIter>
void Scheduler::UpdateResourceStates(UsedResourceIter firstResource, UsedResourceIter lastResource) {
	for (auto it = firstResource; it != lastResource; ++it) {
		if (it->subresource == gxapi::ALL_SUBRESOURCES) {
			for (unsigned s = 0; s < it->resource.GetNumSubresources(); ++s) {
				it->resource.RecordState(s, it->lastState);
			}
		}
		else {
			it->resource.RecordState(it->subresource, it->lastState);
		}
	}
}


} // namespace gxeng
} // namespace inl
