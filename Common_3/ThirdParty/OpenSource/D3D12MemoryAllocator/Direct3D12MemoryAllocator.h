//
// Copyright (c) 2019-2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../../../Renderer/RendererConfig.h"

/** \mainpage D3D12 Memory Allocator

<b>Version 2.0.0-development</b> (2020-05-25)

Copyright (c) 2019-2020 Advanced Micro Devices, Inc. All rights reserved. \n
License: MIT

Documentation of all members: D3D12MemAlloc.h

\section main_table_of_contents Table of contents

- <b>User guide</b>
	- \subpage quick_start
		- [Project setup](@ref quick_start_project_setup)
		- [Creating resources](@ref quick_start_creating_resources)
		- [Mapping memory](@ref quick_start_mapping_memory)
	- \subpage reserving_memory
- \subpage configuration
  - [Custom CPU memory allocator](@ref custom_memory_allocator)
- \subpage general_considerations
  - [Thread safety](@ref general_considerations_thread_safety)
  - [Future plans](@ref general_considerations_future_plans)
  - [Features not supported](@ref general_considerations_features_not_supported)

\section main_see_also See also

- [Product page on GPUOpen](https://gpuopen.com/gaming-product/d3d12-memory-allocator/)
- [Source repository on GitHub](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)


\page quick_start Quick start

\section quick_start_project_setup Project setup and initialization

This is a small, standalone C++ library. It consists of a pair of 2 files:
"%D3D12MemAlloc.h" header file with public interface and "D3D12MemAlloc.cpp" with
internal implementation. The only external dependencies are WinAPI, Direct3D 12,
and parts of C/C++ standard library (but STL containers, exceptions, or RTTI are
not used).

The library is developed and tested using Microsoft Visual Studio 2019, but it
should work with other compilers as well. It is designed for 64-bit code.

To use the library in your project:

(1.) Copy files `D3D12MemAlloc.cpp`, `%D3D12MemAlloc.h` to your project.

(2.) Make `D3D12MemAlloc.cpp` compiling as part of the project, as C++ code.

(3.) Include library header in each CPP file that needs to use the library.

\code
#include "D3D12MemAlloc.h"
\endcode

(4.) Right after you created `ID3D12Device`, fill D3D12MA::ALLOCATOR_DESC
structure and call function D3D12MA::CreateAllocator to create the main
D3D12MA::Allocator object.

Please note that all symbols of the library are declared inside #D3D12MA namespace.

\code
IDXGIAdapter* adapter = (...)
ID3D12Device* device = (...)

D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
allocatorDesc.pDevice = device;
allocatorDesc.pAdapter = adapter;

D3D12MA::Allocator* allocator;
HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &allocator);
\endcode

(5.) Right before destroying the D3D12 device, destroy the allocator object.

Please note that objects of this library must be destroyed by calling `Release`
method (despite they are not COM interfaces and no reference counting is involved).

\code
allocator->Release();
\endcode


\section quick_start_creating_resources Creating resources

To use the library for creating resources (textures and buffers), call method
D3D12MA::Allocator::CreateResource in the place where you would previously call
`ID3D12Device::CreateCommittedResource`.

The function has similar syntax, but it expects structure D3D12MA::ALLOCATION_DESC
to be passed along with `D3D12_RESOURCE_DESC` and other parameters for created
resource. This structure describes parameters of the desired memory allocation,
including choice of `D3D12_HEAP_TYPE`.

The function also returns a new object of type D3D12MA::Allocation, created along
with usual `ID3D12Resource`. It represents allocated memory and can be queried
for size, offset, `ID3D12Resource`, and `ID3D12Heap` if needed.

\code
D3D12_RESOURCE_DESC resourceDesc = {};
resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
resourceDesc.Alignment = 0;
resourceDesc.Width = 1024;
resourceDesc.Height = 1024;
resourceDesc.DepthOrArraySize = 1;
resourceDesc.MipLevels = 1;
resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
resourceDesc.SampleDesc.Count = 1;
resourceDesc.SampleDesc.Quality = 0;
resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

D3D12MA::ALLOCATION_DESC allocationDesc = {};
allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

D3D12Resource* resource;
D3D12MA::Allocation* allocation;
HRESULT hr = allocator->CreateResource(
	&allocationDesc,
	&resourceDesc,
	D3D12_RESOURCE_STATE_COPY_DEST,
	NULL,
	&allocation,
	IID_PPV_ARGS(&resource));
\endcode

You need to remember both resource and allocation objects and destroy them
separately when no longer needed.

\code
allocation->Release();
resource->Release();
\endcode

The advantage of using the allocator instead of creating committed resource, and
the main purpose of this library, is that it can decide to allocate bigger memory
heap internally using `ID3D12Device::CreateHeap` and place multiple resources in
it, at different offsets, using `ID3D12Device::CreatePlacedResource`. The library
manages its own collection of allocated memory blocks (heaps) and remembers which
parts of them are occupied and which parts are free to be used for new resources.

It is important to remember that resources created as placed don't have their memory
initialized to zeros, but may contain garbage data, so they need to be fully initialized
before usage, e.g. using Clear (`ClearRenderTargetView`), Discard (`DiscardResource`),
or copy (`CopyResource`).

The library also automatically handles resource heap tier.
When `D3D12_FEATURE_DATA_D3D12_OPTIONS::ResourceHeapTier` equals `D3D12_RESOURCE_HEAP_TIER_1`,
resources of 3 types: buffers, textures that are render targets or depth-stencil,
and other textures must be kept in separate heaps. When `D3D12_RESOURCE_HEAP_TIER_2`,
they can be kept together. By using this library, you don't need to handle this
manually.


\section quick_start_mapping_memory Mapping memory

The process of getting regular CPU-side pointer to the memory of a resource in
Direct3D is called "mapping". There are rules and restrictions to this process,
as described in D3D12 documentation of [ID3D12Resource::Map method](https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12resource-map).

Mapping happens on the level of particular resources, not entire memory heaps,
and so it is out of scope of this library. Just as the linked documentation says:

- Returned pointer refers to data of particular subresource, not entire memory heap.
- You can map same resource multiple times. It is ref-counted internally.
- Mapping is thread-safe.
- Unmapping is not required before resource destruction.
- Unmapping may not be required before using written data - some heap types on
  some platforms support resources persistently mapped.

When using this library, you can map and use your resources normally without
considering whether they are created as committed resources or placed resources in one large heap.

Example for buffer created and filled in `UPLOAD` heap type:

\code
const UINT64 bufSize = 65536;
const float* bufData = (...);

D3D12_RESOURCE_DESC resourceDesc = {};
resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
resourceDesc.Alignment = 0;
resourceDesc.Width = bufSize;
resourceDesc.Height = 1;
resourceDesc.DepthOrArraySize = 1;
resourceDesc.MipLevels = 1;
resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
resourceDesc.SampleDesc.Count = 1;
resourceDesc.SampleDesc.Quality = 0;
resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

D3D12MA::ALLOCATION_DESC allocationDesc = {};
allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

D3D12Resource* resource;
D3D12MA::Allocation* allocation;
HRESULT hr = allocator->CreateResource(
	&allocationDesc,
	&resourceDesc,
	D3D12_RESOURCE_STATE_GENERIC_READ,
	NULL,
	&allocation,
	IID_PPV_ARGS(&resource));

void* mappedPtr;
hr = resource->Map(0, NULL, &mappedPtr);

memcpy(mappedPtr, bufData, bufSize);

resource->Unmap(0, NULL);
\endcode


\page reserving_memory Reserving minimum amount of memory

The library automatically allocates and frees memory heaps.
It also applies some hysteresis so that it doesn't allocate and free entire heap
when you repeatedly create and release a single resource.
However, if you want to make sure certain number of bytes is always allocated as heaps in a specific pool,
you can use functions designed for this purpose:

- For default heaps use D3D12MA::Allocator::SetDefaultHeapMinBytes.
- For custom heaps use D3D12MA::Pool::SetMinBytes.

Default is 0. You can change this parameter any time.
Setting it to higher value may cause new heaps to be allocated.
If this allocation fails, the function returns appropriate error code, but the parameter remains set to the new value.
Setting it to lower value may cause some empty heaps to be released.

You can always call D3D12MA::Allocator::SetDefaultHeapMinBytes for 3 sets of heap flags separately:
`D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS`, `D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES`, `D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES`.
When ResourceHeapTier = 2, so that all types of resourced are kept together,
these 3 values as simply summed up to calculate minimum amount of bytes for default pool with certain heap type.
Alternatively, when ResourceHeapTier = 2, you can call this function with
`D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES` = 0. This will set a single value for the default pool and
will override the sum of those three.

Reservation of minimum number of bytes interacts correctly with
D3D12MA::POOL_DESC::MinBlockCount and D3D12MA::POOL_DESC::MaxBlockCount.
For example, free blocks (heaps) of a custom pool will be released only when
their number doesn't fall below `MinBlockCount` and their sum size doesn't fall below `MinBytes`.

Some restrictions apply:

- Setting `MinBytes` doesn't interact with memory budget. The allocator tries
  to create additional heaps when necessary without checking if they will exceed the budget.
- Resources created as committed don't count into the number of bytes compared with `MinBytes` set.
  Only placed resources are considered.


\page configuration Configuration

Please check file `D3D12MemAlloc.cpp` lines between "Configuration Begin" and
"Configuration End" to find macros that you can define to change the behavior of
the library, primarily for debugging purposes.

\section custom_memory_allocator Custom CPU memory allocator

If you use custom allocator for CPU memory rather than default C++ operator `new`
and `delete` or `malloc` and `free` functions, you can make this library using
your allocator as well by filling structure D3D12MA::ALLOCATION_CALLBACKS and
passing it as optional member D3D12MA::ALLOCATOR_DESC::pAllocationCallbacks.
Functions pointed there will be used by the library to make any CPU-side
allocations. Example:

\code
#include <malloc.h>

void* CustomAllocate(size_t Size, size_t Alignment, void* pUserData)
{
	void* memory = _aligned_malloc(Size, Alignment);
	// Your extra bookkeeping here...
	return memory;
}

void CustomFree(void* pMemory, void* pUserData)
{
	// Your extra bookkeeping here...
	_aligned_free(pMemory);
}

(...)

D3D12MA::ALLOCATION_CALLBACKS allocationCallbacks = {};
allocationCallbacks.pAllocate = &CustomAllocate;
allocationCallbacks.pFree = &CustomFree;

D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
allocatorDesc.pDevice = device;
allocatorDesc.pAdapter = adapter;
allocatorDesc.pAllocationCallbacks = &allocationCallbacks;

D3D12MA::Allocator* allocator;
HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &allocator);
\endcode


\page general_considerations General considerations

\section general_considerations_thread_safety Thread safety

- The library has no global state, so separate D3D12MA::Allocator objects can be used independently.
  In typical applications there should be no need to create multiple such objects though - one per `ID3D12Device` is enough.
- All calls to methods of D3D12MA::Allocator class are safe to be made from multiple
  threads simultaneously because they are synchronized internally when needed.
- When the allocator is created with D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED,
  calls to methods of D3D12MA::Allocator class must be made from a single thread or synchronized by the user.
  Using this flag may improve performance.

\section general_considerations_future_plans Future plans

Features planned for future releases:

Near future: feature parity with [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/), including:

- Alternative allocation algorithms: linear allocator, buddy allocator
- Support for priorities using `ID3D12Device1::SetResidencyPriority`

Later:

- Memory defragmentation
- Support for multi-GPU (multi-adapter)

\section general_considerations_features_not_supported Features not supported

Features deliberately excluded from the scope of this library:

- Descriptor allocation. Although also called "heaps", objects that represent
  descriptors are separate part of the D3D12 API from buffers and textures.
- Support for `D3D12_HEAP_TYPE_CUSTOM`. Only the default heap types are supported:
  `UPLOAD`, `DEFAULT`, `READBACK`.
- Support for reserved (tiled) resources. We don't recommend using them.
- Support for `ID3D12Device::Evict` and `MakeResident`. We don't recommend using them.
- Handling CPU memory allocation failures. When dynamically creating small C++
  objects in CPU memory (not the GPU memory), allocation failures are not
  handled gracefully, because that would complicate code significantly and
  is usually not needed in desktop PC applications anyway.
  Success of an allocation is just checked with an assert.
- Code free of any compiler warnings - especially those that would require complicating the code
  just to please the compiler complaining about unused parameters, variables, or expressions being
  constant in Relese configuration, e.g. because they are only used inside an assert.
- This is a C++ library.
  Bindings or ports to any other programming languages are welcomed as external projects and
  are not going to be included into this repository.
*/

// Define this macro to 0 to disable usage of DXGI 1.4 (needed for IDXGIAdapter3 and query for memory budget).
#ifndef D3D12MA_DXGI_1_4
#define D3D12MA_DXGI_1_4 1
#endif

// If using this library on a platform different than Windows PC, you should
// include D3D12-compatible header before this library on your own and define this macro.
#ifndef D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include <d3d12.h>
#include <dxgi.h>
#endif

/*
When defined to value other than 0, the library will try to use
D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT or D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
for created textures when possible, which can save memory because some small textures
may get their alignment 4K and their size a multiply of 4K instead of 64K.

#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 0
	Disables small texture alignment.
#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 1
	Enables conservative algorithm that will use small alignment only for some textures
	that are surely known to support it.
#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 2
	Enables query for small alignment to D3D12 (based on Microsoft sample) which will
	enable small alignment for more textures, but will also generate D3D Debug Layer
	error #721 on call to ID3D12Device::GetResourceAllocationInfo, which you should just
	ignore.
*/
#ifndef D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 1
#endif

/// \cond INTERNAL

#define D3D12MA_CLASS_NO_COPY(className) \
    private: \
        className(const className&) = delete; \
        className(className&&) = delete; \
        className& operator=(const className&) = delete; \
        className& operator=(className&&) = delete;

// To be used with MAKE_HRESULT to define custom error codes.
#define FACILITY_D3D12MA 3542

/// \endcond

namespace D3D12MA
{

	/// \cond INTERNAL
	class AllocatorPimpl;
	class PoolPimpl;
	class NormalBlock;
	class BlockVector;
	class JsonWriter;
	/// \endcond

	class Pool;
	class Allocator;
	struct StatInfo;

	/// Pointer to custom callback function that allocates CPU memory.
	typedef void* (*ALLOCATE_FUNC_PTR)(size_t Size, size_t Alignment, void* pUserData);
	/**
	\brief Pointer to custom callback function that deallocates CPU memory.

	`pMemory = null` should be accepted and ignored.
	*/
	typedef void (*FREE_FUNC_PTR)(void* pMemory, void* pUserData);

	/// Custom callbacks to CPU memory allocation functions.
	struct ALLOCATION_CALLBACKS
	{
		/// %Allocation function.
		ALLOCATE_FUNC_PTR pAllocate;
		/// Dellocation function.
		FREE_FUNC_PTR pFree;
		/// Custom data that will be passed to allocation and deallocation functions as `pUserData` parameter.
		void* pUserData;
	};

	/// \brief Bit flags to be used with ALLOCATION_DESC::Flags.
	typedef enum ALLOCATION_FLAGS
	{
		/// Zero
		ALLOCATION_FLAG_NONE = 0,

		/**
		Set this flag if the allocation should have its own dedicated memory allocation (committed resource with implicit heap).

		Use it for special, big resources, like fullscreen textures used as render targets.
		*/
		ALLOCATION_FLAG_COMMITTED = 0x1,

		/**
		Set this flag to only try to allocate from existing memory heaps and never create new such heap.

		If new allocation cannot be placed in any of the existing heaps, allocation
		fails with `E_OUTOFMEMORY` error.

		You should not use D3D12MA::ALLOCATION_FLAG_COMMITTED and
		D3D12MA::ALLOCATION_FLAG_NEVER_ALLOCATE at the same time. It makes no sense.
		*/
		ALLOCATION_FLAG_NEVER_ALLOCATE = 0x2,

		/** Create allocation only if additional memory required for it, if any, won't exceed
		memory budget. Otherwise return `E_OUTOFMEMORY`.
		*/
		ALLOCATION_FLAG_WITHIN_BUDGET = 0x4,
	} ALLOCATION_FLAGS;

	/// \brief Parameters of created D3D12MA::Allocation object. To be used with Allocator::CreateResource.
	struct ALLOCATION_DESC
	{
		/// Flags.
		ALLOCATION_FLAGS Flags;
		/** \brief The type of memory heap where the new allocation should be placed.

		It must be one of: `D3D12_HEAP_TYPE_DEFAULT`, `D3D12_HEAP_TYPE_UPLOAD`, `D3D12_HEAP_TYPE_READBACK`.

		When D3D12MA::ALLOCATION_DESC::CustomPool != NULL this member is ignored.
		*/
		D3D12_HEAP_TYPE HeapType;
		/** \brief Additional heap flags to be used when allocating memory.

		In most cases it can be 0.

		- If you use D3D12MA::Allocator::CreateResource(), you don't need to care.
		  Necessary flag `D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS`, `D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES`,
		  or `D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` is added automatically.
		- If you use D3D12MA::Allocator::AllocateMemory(), you should specify one of those `ALLOW_ONLY` flags.
		  Except when you validate that D3D12MA::Allocator::GetD3D12Options()`.ResourceHeapTier == D3D12_RESOURCE_HEAP_TIER_1` -
		  then you can leave it 0.
		- You can specify additional flags if needed. Then the memory will always be allocated as
		  separate block using `D3D12Device::CreateCommittedResource` or `CreateHeap`, not as part of an existing larget block.

		When D3D12MA::ALLOCATION_DESC::CustomPool != NULL this member is ignored.
		*/
		D3D12_HEAP_FLAGS ExtraHeapFlags;
		/** \brief Custom pool to place the new resource in. Optional.

		When not NULL, the resource will be created inside specified custom pool.
		It will then never be created as committed.
		*/
		Pool* CustomPool;

		// CONFFX_BEGIN - Multi GPU
		UINT             CreationNodeMask;
		UINT             VisibleNodeMask;
		// CONFFX_END
	};

	/** \brief Represents single memory allocation.

	It may be either implicit memory heap dedicated to a single resource or a
	specific region of a bigger heap plus unique offset.

	To create such object, fill structure D3D12MA::ALLOCATION_DESC and call function
	Allocator::CreateResource.

	The object remembers size and some other information.
	To retrieve this information, use methods of this class.

	The object also remembers `ID3D12Resource` and "owns" a reference to it,
	so it calls `Release()` on the resource when destroyed.
	*/
	class Allocation
	{
	public:
		/** \brief Deletes this object.

		This function must be used instead of destructor, which is private.
		There is no reference counting involved.
		*/
		void Release();

		/** \brief Returns offset in bytes from the start of memory heap.

		If the Allocation represents committed resource with implicit heap, returns 0.
		*/
		UINT64 GetOffset() const;

		/** \brief Returns size in bytes of the resource.

		Works also with committed resources.
		*/
		UINT64 GetSize() const { return m_Size; }

		/** \brief Returns D3D12 resource associated with this object.

		Calling this method doesn't increment resource's reference counter.
		*/
		ID3D12Resource* GetResource() const { return m_Resource; }

		/** \brief Returns memory heap that the resource is created in.

		If the Allocation represents committed resource with implicit heap, returns NULL.
		*/
		ID3D12Heap* GetHeap() const;

		/** \brief Associates a name with the allocation object. This name is for use in debug diagnostics and tools.

		Internal copy of the string is made, so the memory pointed by the argument can be
		changed of freed immediately after this call.

		`Name` can be null.
		*/
		void SetName(LPCWSTR Name);

		/** \brief Returns the name associated with the allocation object.

		Returned string points to an internal copy.

		If no name was associated with the allocation, returns null.
		*/
		LPCWSTR GetName() const { return m_Name; }

		/** \brief Returns `TRUE` if the memory of the allocation was filled with zeros when the allocation was created.

		Returns `TRUE` only if the allocator is sure that the entire memory where the
		allocation was created was filled with zeros at the moment the allocation was made.

		Returns `FALSE` if the memory could potentially contain garbage data.
		If it's a render-target or depth-stencil texture, it then needs proper
		initialization with `ClearRenderTargetView`, `ClearDepthStencilView`, `DiscardResource`,
		or a copy operation, as described on page:
		[ID3D12Device::CreatePlacedResource method - Notes on the required resource initialization](https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource#notes-on-the-required-resource-initialization).
		Please note that rendering a fullscreen triangle or quad to the texture as
		a render target is not a proper way of initialization!

		See also articles:
		["Coming to DirectX 12: More control over memory allocation"](https://devblogs.microsoft.com/directx/coming-to-directx-12-more-control-over-memory-allocation/),
		["Initializing DX12 Textures After Allocation and Aliasing"](https://asawicki.info/news_1724_initializing_dx12_textures_after_allocation_and_aliasing).
		*/
		BOOL WasZeroInitialized() const { return m_PackedData.WasZeroInitialized(); }

	private:
		friend class AllocatorPimpl;
		friend class BlockVector;
		friend class JsonWriter;
		template<typename T> friend void D3D12MA_DELETE(const ALLOCATION_CALLBACKS&, T*);
		template<typename T> friend class PoolAllocator;

		enum Type
		{
			TYPE_COMMITTED,
			TYPE_PLACED,
			TYPE_HEAP,
			TYPE_COUNT
		};

		AllocatorPimpl* m_Allocator;
		UINT64 m_Size;
		ID3D12Resource* m_Resource;
		UINT m_CreationFrameIndex;
		wchar_t* m_Name;

		union
		{
			struct
			{
				D3D12_HEAP_TYPE heapType;
			} m_Committed;

			struct
			{
				UINT64 offset;
				NormalBlock* block;
			} m_Placed;

			struct
			{
				D3D12_HEAP_TYPE heapType;
				ID3D12Heap* heap;
			} m_Heap;
		};

		struct PackedData
		{
		public:
			PackedData() :
				m_Type(0), m_ResourceDimension(0), m_ResourceFlags(0), m_TextureLayout(0), m_WasZeroInitialized(0) { }

			Type GetType() const { return (Type)m_Type; }
			D3D12_RESOURCE_DIMENSION GetResourceDimension() const { return (D3D12_RESOURCE_DIMENSION)m_ResourceDimension; }
			D3D12_RESOURCE_FLAGS GetResourceFlags() const { return (D3D12_RESOURCE_FLAGS)m_ResourceFlags; }
			D3D12_TEXTURE_LAYOUT GetTextureLayout() const { return (D3D12_TEXTURE_LAYOUT)m_TextureLayout; }
			BOOL WasZeroInitialized() const { return (BOOL)m_WasZeroInitialized; }

			void SetType(Type type);
			void SetResourceDimension(D3D12_RESOURCE_DIMENSION resourceDimension);
			void SetResourceFlags(D3D12_RESOURCE_FLAGS resourceFlags);
			void SetTextureLayout(D3D12_TEXTURE_LAYOUT textureLayout);
			void SetWasZeroInitialized(BOOL wasZeroInitialized) { m_WasZeroInitialized = wasZeroInitialized ? 1 : 0; }

		private:
			UINT m_Type : 2;               // enum Type
			UINT m_ResourceDimension : 3;  // enum D3D12_RESOURCE_DIMENSION
			UINT m_ResourceFlags : 24;     // flags D3D12_RESOURCE_FLAGS
			UINT m_TextureLayout : 9;      // enum D3D12_TEXTURE_LAYOUT
			UINT m_WasZeroInitialized : 1; // BOOL
		} m_PackedData;

		Allocation(AllocatorPimpl* allocator, UINT64 size, BOOL wasZeroInitialized);
		~Allocation();
		void InitCommitted(D3D12_HEAP_TYPE heapType);
		void InitPlaced(UINT64 offset, UINT64 alignment, NormalBlock* block);
		void InitHeap(D3D12_HEAP_TYPE heapType, ID3D12Heap* heap);
		void SetResource(ID3D12Resource* resource, const D3D12_RESOURCE_DESC* pResourceDesc);
		void FreeName();

		D3D12MA_CLASS_NO_COPY(Allocation)
	};

	/// \brief Parameters of created D3D12MA::Pool object. To be used with D3D12MA::Allocator::CreatePool.
	struct POOL_DESC
	{
		/** \brief The type of memory heap where allocations of this pool should be placed.

		It must be one of: `D3D12_HEAP_TYPE_DEFAULT`, `D3D12_HEAP_TYPE_UPLOAD`, `D3D12_HEAP_TYPE_READBACK`.
		*/
		D3D12_HEAP_TYPE HeapType;
		/** \brief Heap flags to be used when allocating heaps of this pool.

		It should contain one of these values, depending on type of resources you are going to create in this heap:
		`D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS`,
		`D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES`,
		`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES`.
		Except if ResourceHeapTier = 2, then it may be `D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES` = 0.

		You can specify additional flags if needed.
		*/
		D3D12_HEAP_FLAGS HeapFlags;
		/** \brief Size of a single heap (memory block) to be allocated as part of this pool, in bytes. Optional.

		Specify nonzero to set explicit, constant size of memory blocks used by this pool.
		Leave 0 to use default and let the library manage block sizes automatically.
		Then sizes of particular blocks may vary.
		*/
		UINT64 BlockSize;
		/** \brief Minimum number of heaps (memory blocks) to be always allocated in this pool, even if they stay empty. Optional.

		Set to 0 to have no preallocated blocks and allow the pool be completely empty.
		*/
		UINT MinBlockCount;
		/** \brief Maximum number of heaps (memory blocks) that can be allocated in this pool. Optional.

		Set to 0 to use default, which is `UINT64_MAX`, which means no limit.

		Set to same value as D3D12MA::POOL_DESC::MinBlockCount to have fixed amount of memory allocated
		throughout whole lifetime of this pool.
		*/
		UINT MaxBlockCount;
	};

	/** \brief Custom memory pool

	Represents a separate set of heaps (memory blocks) that can be used to create
	D3D12MA::Allocation-s and resources in it. Usually there is no need to create custom
	pools - creating resources in default pool is sufficient.

	To create custom pool, fill D3D12MA::POOL_DESC and call D3D12MA::Allocator::CreatePool.
	*/
	class Pool
	{
	public:
		/** \brief Deletes pool object, frees D3D12 heaps (memory blocks) managed by it. Allocations and resources must already be released!

		It doesn't delete allocations and resources created in this pool. They must be all
		released before calling this function!
		*/
		void Release();

		/** \brief Returns copy of parameters of the pool.

		These are the same parameters as passed to D3D12MA::Allocator::CreatePool.
		*/
		POOL_DESC GetDesc() const;

		/** \brief Sets the minimum number of bytes that should always be allocated (reserved) in this pool.

		See also: \subpage reserving_memory.
		*/
		HRESULT SetMinBytes(UINT64 minBytes);

		/** \brief Retrieves statistics from the current state of this pool.
		*/
		void CalculateStats(StatInfo* pStats);

		/** \brief Associates a name with the pool. This name is for use in debug diagnostics and tools.

		Internal copy of the string is made, so the memory pointed by the argument can be
		changed of freed immediately after this call.

		`Name` can be NULL.
		*/
		void SetName(LPCWSTR Name);

		/** \brief Returns the name associated with the pool object.

		Returned string points to an internal copy.

		If no name was associated with the allocation, returns NULL.
		*/
		LPCWSTR GetName() const;

	private:
		friend class Allocator;
		friend class AllocatorPimpl;
		template<typename T> friend void D3D12MA_DELETE(const ALLOCATION_CALLBACKS&, T*);

		PoolPimpl* m_Pimpl;

		Pool(Allocator* allocator, const POOL_DESC &desc);
		~Pool();

		D3D12MA_CLASS_NO_COPY(Pool)
	};

	/// \brief Bit flags to be used with ALLOCATOR_DESC::Flags.
	typedef enum ALLOCATOR_FLAGS
	{
		/// Zero
		ALLOCATOR_FLAG_NONE = 0,

		/**
		Allocator and all objects created from it will not be synchronized internally,
		so you must guarantee they are used from only one thread at a time or
		synchronized by you.

		Using this flag may increase performance because internal mutexes are not used.
		*/
		ALLOCATOR_FLAG_SINGLETHREADED = 0x1,

		/**
		Every allocation will have its own memory block.
		To be used for debugging purposes.
	   */
		ALLOCATOR_FLAG_ALWAYS_COMMITTED = 0x2,
	} ALLOCATOR_FLAGS;

	/// \brief Parameters of created Allocator object. To be used with CreateAllocator().
	struct ALLOCATOR_DESC
	{
		/// Flags.
		ALLOCATOR_FLAGS Flags;

		/** Direct3D device object that the allocator should be attached to.

		Allocator is doing `AddRef`/`Release` on this object.
		*/
		ID3D12Device* pDevice;

		/** \brief Preferred size of a single `ID3D12Heap` block to be allocated.

		Set to 0 to use default, which is currently 256 MiB.
		*/
		UINT64 PreferredBlockSize;

		/** \brief Custom CPU memory allocation callbacks. Optional.

		Optional, can be null. When specified, will be used for all CPU-side memory allocations.
		*/
		const ALLOCATION_CALLBACKS* pAllocationCallbacks;

		/** DXGI Adapter object that you use for D3D12 and this allocator.

		Allocator is doing `AddRef`/`Release` on this object.
		*/
		IDXGIAdapter* pAdapter;
	};

	/**
	\brief Number of D3D12 memory heap types supported.
	*/
	const UINT HEAP_TYPE_COUNT = 3;

	/**
	\brief Calculated statistics of memory usage in entire allocator.
	*/
	struct StatInfo
	{
		/// Number of memory blocks (heaps) allocated.
		UINT BlockCount;
		/// Number of D3D12MA::Allocation objects allocated.
		UINT AllocationCount;
		/// Number of free ranges of memory between allocations.
		UINT UnusedRangeCount;
		/// Total number of bytes occupied by all allocations.
		UINT64 UsedBytes;
		/// Total number of bytes occupied by unused ranges.
		UINT64 UnusedBytes;
		UINT64 AllocationSizeMin;
		UINT64 AllocationSizeAvg;
		UINT64 AllocationSizeMax;
		UINT64 UnusedRangeSizeMin;
		UINT64 UnusedRangeSizeAvg;
		UINT64 UnusedRangeSizeMax;
	};

	/**
	\brief General statistics from the current state of the allocator.
	*/
	struct Stats
	{
		/// Total statistics from all heap types.
		StatInfo Total;
		/**
		One StatInfo for each type of heap located at the following indices:
		0 - DEFAULT, 1 - UPLOAD, 2 - READBACK.
		*/
		StatInfo HeapType[HEAP_TYPE_COUNT];
	};

	/** \brief Statistics of current memory usage and available budget, in bytes, for GPU or CPU memory.
	*/
	struct Budget
	{
		/** \brief Sum size of all memory blocks allocated from particular heap type, in bytes.
		*/
		UINT64 BlockBytes;

		/** \brief Sum size of all allocations created in particular heap type, in bytes.

		Always less or equal than `BlockBytes`.
		Difference `BlockBytes - AllocationBytes` is the amount of memory allocated but unused -
		available for new allocations or wasted due to fragmentation.
		*/
		UINT64 AllocationBytes;

		/** \brief Estimated current memory usage of the program, in bytes.

		Fetched from system using `IDXGIAdapter3::QueryVideoMemoryInfo` if enabled.

		It might be different than `BlockBytes` (usually higher) due to additional implicit objects
		also occupying the memory, like swapchain, pipeline state objects, descriptor heaps, command lists, or
		memory blocks allocated outside of this library, if any.
		*/
		UINT64 UsageBytes;

		/** \brief Estimated amount of memory available to the program, in bytes.

		Fetched from system using `IDXGIAdapter3::QueryVideoMemoryInfo` if enabled.

		It might be different (most probably smaller) than memory sizes reported in `DXGI_ADAPTER_DESC` due to factors
		external to the program, like other programs also consuming system resources.
		Difference `BudgetBytes - UsageBytes` is the amount of additional memory that can probably
		be allocated without problems. Exceeding the budget may result in various problems.
		*/
		UINT64 BudgetBytes;
	};

	/**
	\brief Represents main object of this library initialized for particular `ID3D12Device`.

	Fill structure D3D12MA::ALLOCATOR_DESC and call function CreateAllocator() to create it.
	Call method Allocator::Release to destroy it.

	It is recommended to create just one object of this type per `ID3D12Device` object,
	right after Direct3D 12 is initialized and keep it alive until before Direct3D device is destroyed.
	*/
	class Allocator
	{
	public:
		/** \brief Deletes this object.

		This function must be used instead of destructor, which is private.
		There is no reference counting involved.
		*/
		void Release();

		/// Returns cached options retrieved from D3D12 device.
		const D3D12_FEATURE_DATA_D3D12_OPTIONS& GetD3D12Options() const;

		/** \brief Allocates memory and creates a D3D12 resource (buffer or texture). This is the main allocation function.

		The function is similar to `ID3D12Device::CreateCommittedResource`, but it may
		really call `ID3D12Device::CreatePlacedResource` to assign part of a larger,
		existing memory heap to the new resource, which is the main purpose of this
		whole library.

		If `ppvResource` is null, you receive only `ppAllocation` object from this function.
		It holds pointer to `ID3D12Resource` that can be queried using function D3D12::Allocation::GetResource().
		Reference count of the resource object is 1.
		It is automatically destroyed when you destroy the allocation object.

		If 'ppvResource` is not null, you receive pointer to the resource next to allocation object.
		Reference count of the resource object is then 2, so you need to manually `Release` it
		along with the allocation.

		\param pAllocDesc   Parameters of the allocation.
		\param pResourceDesc   Description of created resource.
		\param InitialResourceState   Initial resource state.
		\param pOptimizedClearValue   Optional. Either null or optimized clear value.
		\param[out] ppAllocation   Filled with pointer to new allocation object created.
		\param riidResource   IID of a resource to be created. Must be `__uuidof(ID3D12Resource)`.
		\param[out] ppvResource   Optional. If not null, filled with pointer to new resouce created.
		*/
		HRESULT CreateResource(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			Allocation** ppAllocation,
			REFIID riidResource,
			void** ppvResource);

		/** \brief Allocates memory without creating any resource placed in it.

		This function is similar to `ID3D12Device::CreateHeap`, but it may really assign
		part of a larger, existing heap to the allocation.

		`pAllocDesc->heapFlags` should contain one of these values, depending on type of resources you are going to create in this memory:
		`D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS`,
		`D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES`,
		`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES`.
		Except if you validate that ResourceHeapTier = 2 - then `heapFlags`
		may be `D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES` = 0.
		Additional flags in `heapFlags` are allowed as well.

		`pAllocInfo->SizeInBytes` must be multiply of 64KB.
		`pAllocInfo->Alignment` must be one of the legal values as described in documentation of `D3D12_HEAP_DESC`.

		If you use D3D12MA::ALLOCATION_FLAG_COMMITTED you will get a separate memory block -
		a heap that always has offset 0.
		*/
		HRESULT AllocateMemory(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_ALLOCATION_INFO* pAllocInfo,
			Allocation** ppAllocation);

		/** \brief Creates a new resource in place of an existing allocation. This is useful for memory aliasing.

		\param pAllocation Existing allocation indicating the memory where the new resource should be created.
			It can be created using D3D12MA::Allocator::CreateResource and already have a resource bound to it,
			or can be a raw memory allocated with D3D12MA::Allocator::AllocateMemory.
			It must not be created as committed so that `ID3D12Heap` is available and not implicit.
		\param AllocationLocalOffset Additional offset in bytes to be applied when allocating the resource.
			Local from the start of `pAllocation`, not the beginning of the whole `ID3D12Heap`!
			If the new resource should start from the beginning of the `pAllocation` it should be 0.
		\param pResourceDesc Description of the new resource to be created.
		\param InitialResourceState
		\param pOptimizedClearValue
		\param riidResource
		\param[out] ppvResource Returns pointer to the new resource.
			The resource is not bound with `pAllocation`.
			This pointer must not be null - you must get the resource pointer and `Release` it when no longer needed.

		Memory requirements of the new resource are checked for validation.
		If its size exceeds the end of `pAllocation` or required alignment is not fulfilled
		considering `pAllocation->GetOffset() + AllocationLocalOffset`, the function
		returns `E_INVALIDARG`.
		*/
		HRESULT CreateAliasingResource(
			Allocation* pAllocation,
			UINT64 AllocationLocalOffset,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			REFIID riidResource,
			void** ppvResource);

		/** \brief Creates custom pool.
		*/
		HRESULT CreatePool(
			const POOL_DESC* pPoolDesc,
			Pool** ppPool);

		/** \brief Sets the minimum number of bytes that should always be allocated (reserved) in a specific default pool.

		\param heapType Must be one of: `D3D12_HEAP_TYPE_DEFAULT`, `D3D12_HEAP_TYPE_UPLOAD`, `D3D12_HEAP_TYPE_READBACK`.
		\param heapFlags Must be one of: `D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS`, `D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES`,
			`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES`. If ResourceHeapTier = 2, it can also be `D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES`.
		\param minBytes Minimum number of bytes to keep allocated.

		See also: \subpage reserving_memory.
		*/
		HRESULT SetDefaultHeapMinBytes(
			D3D12_HEAP_TYPE heapType,
			D3D12_HEAP_FLAGS heapFlags,
			UINT64 minBytes);

		/** \brief Sets the index of the current frame.

		This function is used to set the frame index in the allocator when a new game frame begins.
		*/
		void SetCurrentFrameIndex(UINT frameIndex);

		/** \brief Retrieves statistics from the current state of the allocator.
		*/
		void CalculateStats(Stats* pStats);

		/** \brief Retrieves information about current memory budget.

		\param[out] pGpuBudget Optional, can be null.
		\param[out] pCpuBudget Optional, can be null.

		This function is called "get" not "calculate" because it is very fast, suitable to be called
		every frame or every allocation. For more detailed statistics use CalculateStats().

		Note that when using allocator from multiple threads, returned information may immediately
		become outdated.
		*/
		void GetBudget(Budget* pGpuBudget, Budget* pCpuBudget);

		/// Builds and returns statistics as a string in JSON format.
		/** @param[out] ppStatsString Must be freed using Allocator::FreeStatsString.
		@param DetailedMap `TRUE` to include full list of allocations (can make the string quite long), `FALSE` to only return statistics.
		*/
		void BuildStatsString(WCHAR** ppStatsString, BOOL DetailedMap);

		/// Frees memory of a string returned from Allocator::BuildStatsString.
		void FreeStatsString(WCHAR* pStatsString);

	private:
		friend HRESULT CreateAllocator(const ALLOCATOR_DESC*, Allocator**);
		template<typename T> friend void D3D12MA_DELETE(const ALLOCATION_CALLBACKS&, T*);
		friend class Pool;

		Allocator(const ALLOCATION_CALLBACKS& allocationCallbacks, const ALLOCATOR_DESC& desc);
		~Allocator();

		AllocatorPimpl* m_Pimpl;

		D3D12MA_CLASS_NO_COPY(Allocator)
	};

	/** \brief Creates new main Allocator object and returns it through `ppAllocator`.

	You normally only need to call it once and keep a single Allocator object for your `ID3D12Device`.
	*/
	HRESULT CreateAllocator(const ALLOCATOR_DESC* pDesc, Allocator** ppAllocator);

} // namespace D3D12MA

/// \cond INTERNAL
DEFINE_ENUM_FLAG_OPERATORS(D3D12MA::ALLOCATION_FLAGS);
DEFINE_ENUM_FLAG_OPERATORS(D3D12MA::ALLOCATOR_FLAGS);
/// \endcond

#ifdef D3D12MA_IMPLEMENTATION
//
// Copyright (c) 2019-2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include <dxgi.h>
#if D3D12MA_DXGI_1_4
#include <dxgi1_4.h>
#endif
#endif

#include <mutex>
#include <atomic>
#include <algorithm>
#include <utility>
#include <cstdlib>
#include <malloc.h> // for _aligned_malloc, _aligned_free

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Configuration Begin
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifndef D3D12MA_ASSERT
#include <cassert>
#define D3D12MA_ASSERT(cond) assert(cond)
#endif

// Assert that will be called very often, like inside data structures e.g. operator[].
// Making it non-empty can make program slow.
#ifndef D3D12MA_HEAVY_ASSERT
#ifdef _DEBUG
#define D3D12MA_HEAVY_ASSERT(expr)   //D3D12MA_ASSERT(expr)
#else
#define D3D12MA_HEAVY_ASSERT(expr)
#endif
#endif

#ifndef D3D12MA_DEBUG_ALIGNMENT
	/*
	Minimum alignment of all allocations, in bytes.
	Set to more than 1 for debugging purposes only. Must be power of two.
	*/
#define D3D12MA_DEBUG_ALIGNMENT (1)
#endif

#ifndef D3D12MA_DEBUG_MARGIN
	// Minimum margin before and after every allocation, in bytes.
	// Set nonzero for debugging purposes only.
#define D3D12MA_DEBUG_MARGIN (0)
#endif

#ifndef D3D12MA_DEBUG_GLOBAL_MUTEX
	/*
	Set this to 1 for debugging purposes only, to enable single mutex protecting all
	entry calls to the library. Can be useful for debugging multithreading issues.
	*/
#define D3D12MA_DEBUG_GLOBAL_MUTEX (0)
#endif

#ifndef D3D12MA_DEFAULT_BLOCK_SIZE
	/// Default size of a block allocated as single ID3D12Heap.
#define D3D12MA_DEFAULT_BLOCK_SIZE (256ull * 1024 * 1024)
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Configuration End
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


namespace D3D12MA
{

	////////////////////////////////////////////////////////////////////////////////
	// Private globals - CPU memory allocation

	static void* DefaultAllocate(size_t Size, size_t Alignment, void* /*pUserData*/)
	{
		return _aligned_malloc(Size, Alignment);
	}
	static void DefaultFree(void* pMemory, void* /*pUserData*/)
	{
		return _aligned_free(pMemory);
	}

	static void* Malloc(const ALLOCATION_CALLBACKS& allocs, size_t size, size_t alignment)
	{
		void* const result = (*allocs.pAllocate)(size, alignment, allocs.pUserData);
		D3D12MA_ASSERT(result);
		return result;
	}
	static void Free(const ALLOCATION_CALLBACKS& allocs, void* memory)
	{
		(*allocs.pFree)(memory, allocs.pUserData);
	}

	template<typename T>
	static T* Allocate(const ALLOCATION_CALLBACKS& allocs)
	{
		return (T*)Malloc(allocs, sizeof(T), __alignof(T));
	}
	template<typename T>
	static T* AllocateArray(const ALLOCATION_CALLBACKS& allocs, size_t count)
	{
		return (T*)Malloc(allocs, sizeof(T) * count, __alignof(T));
	}

#define D3D12MA_NEW(allocs, type) new(D3D12MA::Allocate<type>(allocs))(type)
#define D3D12MA_NEW_ARRAY(allocs, type, count) new(D3D12MA::AllocateArray<type>((allocs), (count)))(type)

	template<typename T>
	void D3D12MA_DELETE(const ALLOCATION_CALLBACKS& allocs, T* memory)
	{
		if (memory)
		{
			memory->~T();
			Free(allocs, memory);
		}
	}
	template<typename T>
	void D3D12MA_DELETE_ARRAY(const ALLOCATION_CALLBACKS& allocs, T* memory, size_t count)
	{
		if (memory)
		{
			for (size_t i = count; i--; )
			{
				memory[i].~T();
			}
			Free(allocs, memory);
		}
	}

	static void SetupAllocationCallbacks(ALLOCATION_CALLBACKS& outAllocs, const ALLOCATOR_DESC& allocatorDesc)
	{
		if (allocatorDesc.pAllocationCallbacks)
		{
			outAllocs = *allocatorDesc.pAllocationCallbacks;
			D3D12MA_ASSERT(outAllocs.pAllocate != NULL && outAllocs.pFree != NULL);
		}
		else
		{
			outAllocs.pAllocate = &DefaultAllocate;
			outAllocs.pFree = &DefaultFree;
			outAllocs.pUserData = NULL;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private globals - basic facilities

#define SAFE_RELEASE(ptr)   do { if(ptr) { (ptr)->Release(); (ptr) = NULL; } } while(false)

#define D3D12MA_VALIDATE(cond) do { if(!(cond)) { \
        D3D12MA_ASSERT(0 && "Validation failed: " #cond); \
        return false; \
    } } while(false)

	const UINT NEW_BLOCK_SIZE_SHIFT_MAX = 3;

	template<typename T>
	static inline T D3D12MA_MIN(const T& a, const T& b)
	{
		return a <= b ? a : b;
	}
	template<typename T>
	static inline T D3D12MA_MAX(const T& a, const T& b)
	{
		return a <= b ? b : a;
	}

	template<typename T>
	static inline void D3D12MA_SWAP(T& a, T& b)
	{
		T tmp = a; a = b; b = tmp;
	}

#ifndef D3D12MA_MUTEX
	class Mutex
	{
	public:
		void Lock() { m_Mutex.lock(); }
		void Unlock() { m_Mutex.unlock(); }
	private:
		std::mutex m_Mutex;
	};
#define D3D12MA_MUTEX Mutex
#endif

#if !defined(_WIN32) || !defined(WINVER) || WINVER < 0x0600
#error Required at least WinAPI version supporting: client = Windows Vista, server = Windows Server 2008.
#endif

#ifndef D3D12MA_RW_MUTEX
	class RWMutex
	{
	public:
		RWMutex() { InitializeSRWLock(&m_Lock); }
		void LockRead() { AcquireSRWLockShared(&m_Lock); }
		void UnlockRead() { ReleaseSRWLockShared(&m_Lock); }
		void LockWrite() { AcquireSRWLockExclusive(&m_Lock); }
		void UnlockWrite() { ReleaseSRWLockExclusive(&m_Lock); }
	private:
		SRWLOCK m_Lock;
	};
#define D3D12MA_RW_MUTEX RWMutex
#endif

	/*
	If providing your own implementation, you need to implement a subset of std::atomic.
	*/
#ifndef D3D12MA_ATOMIC_UINT32
#define D3D12MA_ATOMIC_UINT32 std::atomic<UINT>
#endif

#ifndef D3D12MA_ATOMIC_UINT64
#define D3D12MA_ATOMIC_UINT64 std::atomic<UINT64>
#endif

	// Aligns given value up to nearest multiply of align value. For example: AlignUp(11, 8) = 16.
	// Use types like UINT, uint64_t as T.
	template <typename T>
	static inline T AlignUp(T val, T align)
	{
		return (val + align - 1) / align * align;
	}
	// Aligns given value down to nearest multiply of align value. For example: AlignUp(11, 8) = 8.
	// Use types like UINT, uint64_t as T.
	template <typename T>
	static inline T AlignDown(T val, T align)
	{
		return val / align * align;
	}

	// Division with mathematical rounding to nearest number.
	template <typename T>
	static inline T RoundDiv(T x, T y)
	{
		return (x + (y / (T)2)) / y;
	}
	template <typename T>
	static inline T DivideRoudingUp(T x, T y)
	{
		return (x + y - 1) / y;
	}

	/*
	Returns true if given number is a power of two.
	T must be unsigned integer number or signed integer but always nonnegative.
	For 0 returns true.
	*/
	template <typename T>
	inline bool IsPow2(T x)
	{
		return (x & (x - 1)) == 0;
	}

	// Returns smallest power of 2 greater or equal to v.
	static inline UINT NextPow2(UINT v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}
	static inline uint64_t NextPow2(uint64_t v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		v++;
		return v;
	}

	// Returns largest power of 2 less or equal to v.
	static inline UINT PrevPow2(UINT v)
	{
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v = v ^ (v >> 1);
		return v;
	}
	static inline uint64_t PrevPow2(uint64_t v)
	{
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		v = v ^ (v >> 1);
		return v;
	}

	static inline bool StrIsEmpty(const char* pStr)
	{
		return pStr == NULL || *pStr == '\0';
	}

	// Helper RAII class to lock a mutex in constructor and unlock it in destructor (at the end of scope).
	struct MutexLock
	{
	public:
		MutexLock(D3D12MA_MUTEX& mutex, bool useMutex = true) :
			m_pMutex(useMutex ? &mutex : NULL)
		{
			if (m_pMutex)
			{
				m_pMutex->Lock();
			}
		}
		~MutexLock()
		{
			if (m_pMutex)
			{
				m_pMutex->Unlock();
			}
		}
	private:
		D3D12MA_MUTEX* m_pMutex;

		D3D12MA_CLASS_NO_COPY(MutexLock)
	};

	// Helper RAII class to lock a RW mutex in constructor and unlock it in destructor (at the end of scope), for reading.
	struct MutexLockRead
	{
	public:
		MutexLockRead(D3D12MA_RW_MUTEX& mutex, bool useMutex) :
			m_pMutex(useMutex ? &mutex : NULL)
		{
			if (m_pMutex)
			{
				m_pMutex->LockRead();
			}
		}
		~MutexLockRead()
		{
			if (m_pMutex)
			{
				m_pMutex->UnlockRead();
			}
		}
	private:
		D3D12MA_RW_MUTEX* m_pMutex;

		D3D12MA_CLASS_NO_COPY(MutexLockRead)
	};

	// Helper RAII class to lock a RW mutex in constructor and unlock it in destructor (at the end of scope), for writing.
	struct MutexLockWrite
	{
	public:
		MutexLockWrite(D3D12MA_RW_MUTEX& mutex, bool useMutex) :
			m_pMutex(useMutex ? &mutex : NULL)
		{
			if (m_pMutex)
			{
				m_pMutex->LockWrite();
			}
		}
		~MutexLockWrite()
		{
			if (m_pMutex)
			{
				m_pMutex->UnlockWrite();
			}
		}
	private:
		D3D12MA_RW_MUTEX* m_pMutex;

		D3D12MA_CLASS_NO_COPY(MutexLockWrite)
	};

#if D3D12MA_DEBUG_GLOBAL_MUTEX
	static D3D12MA_MUTEX g_DebugGlobalMutex;
#define D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK MutexLock debugGlobalMutexLock(g_DebugGlobalMutex, true);
#else
#define D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
#endif

	// Minimum size of a free suballocation to register it in the free suballocation collection.
	static const UINT64 MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER = 16;

	/*
	Performs binary search and returns iterator to first element that is greater or
	equal to `key`, according to comparison `cmp`.

	Cmp should return true if first argument is less than second argument.

	Returned value is the found element, if present in the collection or place where
	new element with value (key) should be inserted.
	*/
	template <typename CmpLess, typename IterT, typename KeyT>
	static IterT BinaryFindFirstNotLess(IterT beg, IterT end, const KeyT &key, const CmpLess& cmp)
	{
		size_t down = 0, up = (end - beg);
		while (down < up)
		{
			const size_t mid = (down + up) / 2;
			if (cmp(*(beg + mid), key))
			{
				down = mid + 1;
			}
			else
			{
				up = mid;
			}
		}
		return beg + down;
	}

	/*
	Performs binary search and returns iterator to an element that is equal to `key`,
	according to comparison `cmp`.

	Cmp should return true if first argument is less than second argument.

	Returned value is the found element, if present in the collection or end if not
	found.
	*/
	template<typename CmpLess, typename IterT, typename KeyT>
	IterT BinaryFindSorted(const IterT& beg, const IterT& end, const KeyT& value, const CmpLess& cmp)
	{
		IterT it = BinaryFindFirstNotLess<CmpLess, IterT, KeyT>(beg, end, value, cmp);
		if (it == end ||
			(!cmp(*it, value) && !cmp(value, *it)))
		{
			return it;
		}
		return end;
	}

	struct PointerLess
	{
		bool operator()(const void* lhs, const void* rhs) const
		{
			return lhs < rhs;
		}
	};

	static UINT HeapTypeToIndex(D3D12_HEAP_TYPE type)
	{
		switch (type)
		{
		case D3D12_HEAP_TYPE_DEFAULT:  return 0;
		case D3D12_HEAP_TYPE_UPLOAD:   return 1;
		case D3D12_HEAP_TYPE_READBACK: return 2;
		default: D3D12MA_ASSERT(0); return UINT_MAX;
		}
	}

	static const WCHAR* const HeapTypeNames[] = {
		L"DEFAULT",
		L"UPLOAD",
		L"READBACK",
	};

	// Stat helper functions

	static void AddStatInfo(StatInfo& dst, const StatInfo& src)
	{
		dst.BlockCount += src.BlockCount;
		dst.AllocationCount += src.AllocationCount;
		dst.UnusedRangeCount += src.UnusedRangeCount;
		dst.UsedBytes += src.UsedBytes;
		dst.UnusedBytes += src.UnusedBytes;
		dst.AllocationSizeMin = D3D12MA_MIN(dst.AllocationSizeMin, src.AllocationSizeMin);
		dst.AllocationSizeMax = D3D12MA_MAX(dst.AllocationSizeMax, src.AllocationSizeMax);
		dst.UnusedRangeSizeMin = D3D12MA_MIN(dst.UnusedRangeSizeMin, src.UnusedRangeSizeMin);
		dst.UnusedRangeSizeMax = D3D12MA_MAX(dst.UnusedRangeSizeMax, src.UnusedRangeSizeMax);
	}

	static void PostProcessStatInfo(StatInfo& statInfo)
	{
		statInfo.AllocationSizeAvg = statInfo.AllocationCount ?
			statInfo.UsedBytes / statInfo.AllocationCount : 0;
		statInfo.UnusedRangeSizeAvg = statInfo.UnusedRangeCount ?
			statInfo.UnusedBytes / statInfo.UnusedRangeCount : 0;
	}

	static UINT64 HeapFlagsToAlignment(D3D12_HEAP_FLAGS flags)
	{
		/*
		Documentation of D3D12_HEAP_DESC structure says:

		- D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT   defined as 64KB.
		- D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT   defined as 4MB. An
		application must decide whether the heap will contain multi-sample
		anti-aliasing (MSAA), in which case, the application must choose [this flag].

		https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/ns-d3d12-d3d12_heap_desc
		*/

		const D3D12_HEAP_FLAGS denyAllTexturesFlags =
			D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
		const bool canContainAnyTextures =
			(flags & denyAllTexturesFlags) != denyAllTexturesFlags;
		return canContainAnyTextures ?
			D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	}

	static bool IsFormatCompressed(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	// Only some formats are supported. For others it returns 0.
	static UINT GetBitsPerPixel(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
			return 64;
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
			return 64;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return 64;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return 32;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
			return 32;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
			return 32;
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			return 32;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return 32;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
			return 16;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
			return 16;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
			return 8;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			return 4;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			return 8;
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			return 8;
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
			return 8;
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
			return 8;
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;
		default:
			return 0;
		}
	}

	// This algorithm is overly conservative.
	static bool CanUseSmallAlignment(const D3D12_RESOURCE_DESC& resourceDesc)
	{
		if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		if ((resourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0)
			return false;
		if (resourceDesc.SampleDesc.Count > 1)
			return false;
		if (resourceDesc.DepthOrArraySize != 1)
			return false;

		UINT sizeX = (UINT)resourceDesc.Width;
		UINT sizeY = resourceDesc.Height;
		UINT bitsPerPixel = GetBitsPerPixel(resourceDesc.Format);
		if (bitsPerPixel == 0)
			return false;

		if (IsFormatCompressed(resourceDesc.Format))
		{
			sizeX = DivideRoudingUp(sizeX / 4, 1u);
			sizeY = DivideRoudingUp(sizeY / 4, 1u);
			bitsPerPixel *= 16;
		}

		UINT tileSizeX = 0, tileSizeY = 0;
		switch (bitsPerPixel)
		{
		case   8: tileSizeX = 64; tileSizeY = 64; break;
		case  16: tileSizeX = 64; tileSizeY = 32; break;
		case  32: tileSizeX = 32; tileSizeY = 32; break;
		case  64: tileSizeX = 32; tileSizeY = 16; break;
		case 128: tileSizeX = 16; tileSizeY = 16; break;
		default: return false;
		}

		const UINT tileCount = DivideRoudingUp(sizeX, tileSizeX) * DivideRoudingUp(sizeY, tileSizeY);
		return tileCount <= 16;
	}

	static D3D12_HEAP_FLAGS GetExtraHeapFlagsToIgnore()
	{
		D3D12_HEAP_FLAGS result =
			D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
		return result;
	}

	static inline bool IsHeapTypeValid(D3D12_HEAP_TYPE type)
	{
		return type == D3D12_HEAP_TYPE_DEFAULT ||
			type == D3D12_HEAP_TYPE_UPLOAD ||
			type == D3D12_HEAP_TYPE_READBACK;
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class Vector

	/*
	Dynamically resizing continuous array. Class with interface similar to std::vector.
	T must be POD because constructors and destructors are not called and memcpy is
	used for these objects.
	*/
	template<typename T>
	class Vector
	{
	public:
		typedef T value_type;

		// allocationCallbacks externally owned, must outlive this object.
		Vector(const ALLOCATION_CALLBACKS& allocationCallbacks) :
			m_AllocationCallbacks(allocationCallbacks),
			m_pArray(NULL),
			m_Count(0),
			m_Capacity(0)
		{
		}

		Vector(size_t count, const ALLOCATION_CALLBACKS& allocationCallbacks) :
			m_AllocationCallbacks(allocationCallbacks),
			m_pArray(count ? AllocateArray<T>(allocationCallbacks, count) : NULL),
			m_Count(count),
			m_Capacity(count)
		{
		}

		Vector(const Vector<T>& src) :
			m_AllocationCallbacks(src.m_AllocationCallbacks),
			m_pArray(src.m_Count ? AllocateArray<T>(src.m_AllocationCallbacks, src.m_Count) : NULL),
			m_Count(src.m_Count),
			m_Capacity(src.m_Count)
		{
			if (m_Count > 0)
			{
				memcpy(m_pArray, src.m_pArray, m_Count * sizeof(T));
			}
		}

		~Vector()
		{
			Free(m_AllocationCallbacks, m_pArray);
		}

		Vector& operator=(const Vector<T>& rhs)
		{
			if (&rhs != this)
			{
				resize(rhs.m_Count);
				if (m_Count != 0)
				{
					memcpy(m_pArray, rhs.m_pArray, m_Count * sizeof(T));
				}
			}
			return *this;
		}

		bool empty() const { return m_Count == 0; }
		size_t size() const { return m_Count; }
		T* data() { return m_pArray; }
		const T* data() const { return m_pArray; }

		T& operator[](size_t index)
		{
			D3D12MA_HEAVY_ASSERT(index < m_Count);
			return m_pArray[index];
		}
		const T& operator[](size_t index) const
		{
			D3D12MA_HEAVY_ASSERT(index < m_Count);
			return m_pArray[index];
		}

		T& front()
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			return m_pArray[0];
		}
		const T& front() const
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			return m_pArray[0];
		}
		T& back()
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			return m_pArray[m_Count - 1];
		}
		const T& back() const
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			return m_pArray[m_Count - 1];
		}

		void reserve(size_t newCapacity, bool freeMemory = false)
		{
			newCapacity = D3D12MA_MAX(newCapacity, m_Count);

			if ((newCapacity < m_Capacity) && !freeMemory)
			{
				newCapacity = m_Capacity;
			}

			if (newCapacity != m_Capacity)
			{
				T* const newArray = newCapacity ? AllocateArray<T>(m_AllocationCallbacks, newCapacity) : NULL;
				if (m_Count != 0)
				{
					memcpy(newArray, m_pArray, m_Count * sizeof(T));
				}
				Free(m_AllocationCallbacks, m_pArray);
				m_Capacity = newCapacity;
				m_pArray = newArray;
			}
		}

		void resize(size_t newCount, bool freeMemory = false)
		{
			size_t newCapacity = m_Capacity;
			if (newCount > m_Capacity)
			{
				newCapacity = D3D12MA_MAX(newCount, D3D12MA_MAX(m_Capacity * 3 / 2, (size_t)8));
			}
			else if (freeMemory)
			{
				newCapacity = newCount;
			}

			if (newCapacity != m_Capacity)
			{
				T* const newArray = newCapacity ? AllocateArray<T>(m_AllocationCallbacks, newCapacity) : NULL;
				const size_t elementsToCopy = D3D12MA_MIN(m_Count, newCount);
				if (elementsToCopy != 0)
				{
					memcpy(newArray, m_pArray, elementsToCopy * sizeof(T));
				}
				Free(m_AllocationCallbacks, m_pArray);
				m_Capacity = newCapacity;
				m_pArray = newArray;
			}

			m_Count = newCount;
		}

		void clear(bool freeMemory = false)
		{
			resize(0, freeMemory);
		}

		void insert(size_t index, const T& src)
		{
			D3D12MA_HEAVY_ASSERT(index <= m_Count);
			const size_t oldCount = size();
			resize(oldCount + 1);
			if (index < oldCount)
			{
				memmove(m_pArray + (index + 1), m_pArray + index, (oldCount - index) * sizeof(T));
			}
			m_pArray[index] = src;
		}

		void remove(size_t index)
		{
			D3D12MA_HEAVY_ASSERT(index < m_Count);
			const size_t oldCount = size();
			if (index < oldCount - 1)
			{
				memmove(m_pArray + index, m_pArray + (index + 1), (oldCount - index - 1) * sizeof(T));
			}
			resize(oldCount - 1);
		}

		void push_back(const T& src)
		{
			const size_t newIndex = size();
			resize(newIndex + 1);
			m_pArray[newIndex] = src;
		}

		void pop_back()
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			resize(size() - 1);
		}

		void push_front(const T& src)
		{
			insert(0, src);
		}

		void pop_front()
		{
			D3D12MA_HEAVY_ASSERT(m_Count > 0);
			remove(0);
		}

		typedef T* iterator;

		iterator begin() { return m_pArray; }
		iterator end() { return m_pArray + m_Count; }

		template<typename CmpLess>
		size_t InsertSorted(const T& value, const CmpLess& cmp)
		{
			const size_t indexToInsert = BinaryFindFirstNotLess<CmpLess, iterator, T>(
				m_pArray,
				m_pArray + m_Count,
				value,
				cmp) - m_pArray;
			insert(indexToInsert, value);
			return indexToInsert;
		}

		template<typename CmpLess>
		bool RemoveSorted(const T& value, const CmpLess& cmp)
		{
			const iterator it = BinaryFindFirstNotLess(
				m_pArray,
				m_pArray + m_Count,
				value,
				cmp);
			if ((it != end()) && !cmp(*it, value) && !cmp(value, *it))
			{
				size_t indexToRemove = it - begin();
				remove(indexToRemove);
				return true;
			}
			return false;
		}

	private:
		const ALLOCATION_CALLBACKS& m_AllocationCallbacks;
		T* m_pArray;
		size_t m_Count;
		size_t m_Capacity;
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class StringBuilder

	class StringBuilder
	{
	public:
		StringBuilder(const ALLOCATION_CALLBACKS& allocationCallbacks) : m_Data(allocationCallbacks) { }

		size_t GetLength() const { return m_Data.size(); }
		LPCWSTR GetData() const { return m_Data.data(); }

		void Add(WCHAR ch) { m_Data.push_back(ch); }
		void Add(LPCWSTR str);
		void AddNewLine() { Add(L'\n'); }
		void AddNumber(UINT num);
		void AddNumber(UINT64 num);

	private:
		Vector<WCHAR> m_Data;
	};

	void StringBuilder::Add(LPCWSTR str)
	{
		const size_t len = wcslen(str);
		if (len > 0)
		{
			const size_t oldCount = m_Data.size();
			m_Data.resize(oldCount + len);
			memcpy(m_Data.data() + oldCount, str, len * sizeof(WCHAR));
		}
	}

	void StringBuilder::AddNumber(UINT num)
	{
		WCHAR buf[11];
		buf[10] = L'\0';
		WCHAR *p = &buf[10];
		do
		{
			*--p = L'0' + (num % 10);
			num /= 10;
		} while (num);
		Add(p);
	}

	void StringBuilder::AddNumber(UINT64 num)
	{
		WCHAR buf[21];
		buf[20] = L'\0';
		WCHAR *p = &buf[20];
		do
		{
			*--p = L'0' + (num % 10);
			num /= 10;
		} while (num);
		Add(p);
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class JsonWriter
	class JsonWriter
	{
	public:
		JsonWriter(const ALLOCATION_CALLBACKS& allocationCallbacks, StringBuilder& stringBuilder);
		~JsonWriter();

		void BeginObject(bool singleLine = false);
		void EndObject();

		void BeginArray(bool singleLine = false);
		void EndArray();

		void WriteString(LPCWSTR pStr);
		void BeginString(LPCWSTR pStr = NULL);
		void ContinueString(LPCWSTR pStr);
		void ContinueString(UINT num);
		void ContinueString(UINT64 num);
		void AddAllocationToObject(const Allocation& alloc);
		// void ContinueString_Pointer(const void* ptr);
		void EndString(LPCWSTR pStr = NULL);

		void WriteNumber(UINT num);
		void WriteNumber(UINT64 num);
		void WriteBool(bool b);
		void WriteNull();

	private:
		static const WCHAR* const INDENT;

		enum CollectionType
		{
			COLLECTION_TYPE_OBJECT,
			COLLECTION_TYPE_ARRAY,
		};
		struct StackItem
		{
			CollectionType type;
			UINT valueCount;
			bool singleLineMode;
		};

		StringBuilder& m_SB;
		Vector<StackItem> m_Stack;
		bool m_InsideString;

		void BeginValue(bool isString);
		void WriteIndent(bool oneLess = false);
	};

	const WCHAR* const JsonWriter::INDENT = L"  ";

	JsonWriter::JsonWriter(const ALLOCATION_CALLBACKS& allocationCallbacks, StringBuilder& stringBuilder) :
		m_SB(stringBuilder),
		m_Stack(allocationCallbacks),
		m_InsideString(false)
	{
	}

	JsonWriter::~JsonWriter()
	{
		D3D12MA_ASSERT(!m_InsideString);
		D3D12MA_ASSERT(m_Stack.empty());
	}

	void JsonWriter::BeginObject(bool singleLine)
	{
		D3D12MA_ASSERT(!m_InsideString);

		BeginValue(false);
		m_SB.Add(L'{');

		StackItem stackItem;
		stackItem.type = COLLECTION_TYPE_OBJECT;
		stackItem.valueCount = 0;
		stackItem.singleLineMode = singleLine;
		m_Stack.push_back(stackItem);
	}

	void JsonWriter::EndObject()
	{
		D3D12MA_ASSERT(!m_InsideString);
		D3D12MA_ASSERT(!m_Stack.empty() && m_Stack.back().type == COLLECTION_TYPE_OBJECT);
		D3D12MA_ASSERT(m_Stack.back().valueCount % 2 == 0);

		WriteIndent(true);
		m_SB.Add(L'}');

		m_Stack.pop_back();
	}

	void JsonWriter::BeginArray(bool singleLine)
	{
		D3D12MA_ASSERT(!m_InsideString);

		BeginValue(false);
		m_SB.Add(L'[');

		StackItem stackItem;
		stackItem.type = COLLECTION_TYPE_ARRAY;
		stackItem.valueCount = 0;
		stackItem.singleLineMode = singleLine;
		m_Stack.push_back(stackItem);
	}

	void JsonWriter::EndArray()
	{
		D3D12MA_ASSERT(!m_InsideString);
		D3D12MA_ASSERT(!m_Stack.empty() && m_Stack.back().type == COLLECTION_TYPE_ARRAY);

		WriteIndent(true);
		m_SB.Add(L']');

		m_Stack.pop_back();
	}

	void JsonWriter::WriteString(LPCWSTR pStr)
	{
		BeginString(pStr);
		EndString();
	}

	void JsonWriter::BeginString(LPCWSTR pStr)
	{
		D3D12MA_ASSERT(!m_InsideString);

		BeginValue(true);
		m_InsideString = true;
		m_SB.Add(L'"');
		if (pStr != NULL)
		{
			ContinueString(pStr);
		}
	}

	void JsonWriter::ContinueString(LPCWSTR pStr)
	{
		D3D12MA_ASSERT(m_InsideString);
		D3D12MA_ASSERT(pStr);

		for (const WCHAR *p = pStr; *p; ++p)
		{
			// the strings we encode are assumed to be in UTF-16LE format, the native
			// windows wide character unicode format. In this encoding unicode code
			// points U+0000 to U+D7FF and U+E000 to U+FFFF are encoded in two bytes,
			// and everything else takes more than two bytes. We will reject any
			// multi wchar character encodings for simplicity.
			UINT val = (UINT)*p;
			D3D12MA_ASSERT(((val <= 0xD7FF) || (0xE000 <= val && val <= 0xFFFF)) &&
				"Character not currently supported.");
			switch (*p)
			{
			case L'"':  m_SB.Add(L'\\'); m_SB.Add(L'"');  break;
			case L'\\': m_SB.Add(L'\\'); m_SB.Add(L'\\'); break;
			case L'/':  m_SB.Add(L'\\'); m_SB.Add(L'/');  break;
			case L'\b': m_SB.Add(L'\\'); m_SB.Add(L'b');  break;
			case L'\f': m_SB.Add(L'\\'); m_SB.Add(L'f');  break;
			case L'\n': m_SB.Add(L'\\'); m_SB.Add(L'n');  break;
			case L'\r': m_SB.Add(L'\\'); m_SB.Add(L'r');  break;
			case L'\t': m_SB.Add(L'\\'); m_SB.Add(L't');  break;
			default:
				// conservatively use encoding \uXXXX for any unicode character
				// requiring more than one byte.
				if (32 <= val && val < 256)
					m_SB.Add(*p);
				else
				{
					m_SB.Add(L'\\');
					m_SB.Add(L'u');
					for (UINT i = 0; i < 4; ++i)
					{
						UINT hexDigit = (val & 0xF000) >> 12;
						val <<= 4;
						if (hexDigit < 10)
							m_SB.Add(L'0' + (WCHAR)hexDigit);
						else
							m_SB.Add(L'A' + (WCHAR)hexDigit);
					}
				}
				break;
			}
		}
	}

	void JsonWriter::ContinueString(UINT num)
	{
		D3D12MA_ASSERT(m_InsideString);
		m_SB.AddNumber(num);
	}

	void JsonWriter::ContinueString(UINT64 num)
	{
		D3D12MA_ASSERT(m_InsideString);
		m_SB.AddNumber(num);
	}

	void JsonWriter::EndString(LPCWSTR pStr)
	{
		D3D12MA_ASSERT(m_InsideString);

		if (pStr)
			ContinueString(pStr);
		m_SB.Add(L'"');
		m_InsideString = false;
	}

	void JsonWriter::WriteNumber(UINT num)
	{
		D3D12MA_ASSERT(!m_InsideString);
		BeginValue(false);
		m_SB.AddNumber(num);
	}

	void JsonWriter::WriteNumber(UINT64 num)
	{
		D3D12MA_ASSERT(!m_InsideString);
		BeginValue(false);
		m_SB.AddNumber(num);
	}

	void JsonWriter::WriteBool(bool b)
	{
		D3D12MA_ASSERT(!m_InsideString);
		BeginValue(false);
		if (b)
			m_SB.Add(L"true");
		else
			m_SB.Add(L"false");
	}

	void JsonWriter::WriteNull()
	{
		D3D12MA_ASSERT(!m_InsideString);
		BeginValue(false);
		m_SB.Add(L"null");
	}

	void JsonWriter::BeginValue(bool isString)
	{
		if (!m_Stack.empty())
		{
			StackItem& currItem = m_Stack.back();
			if (currItem.type == COLLECTION_TYPE_OBJECT && currItem.valueCount % 2 == 0)
			{
				D3D12MA_ASSERT(isString);
			}

			if (currItem.type == COLLECTION_TYPE_OBJECT && currItem.valueCount % 2 == 1)
			{
				m_SB.Add(L':'); m_SB.Add(L' ');
			}
			else if (currItem.valueCount > 0)
			{
				m_SB.Add(L','); m_SB.Add(L' ');
				WriteIndent();
			}
			else
			{
				WriteIndent();
			}
			++currItem.valueCount;
		}
	}

	void JsonWriter::WriteIndent(bool oneLess)
	{
		if (!m_Stack.empty() && !m_Stack.back().singleLineMode)
		{
			m_SB.AddNewLine();

			size_t count = m_Stack.size();
			if (count > 0 && oneLess)
			{
				--count;
			}
			for (size_t i = 0; i < count; ++i)
			{
				m_SB.Add(INDENT);
			}
		}
	}

	void JsonWriter::AddAllocationToObject(const Allocation& alloc)
	{
		WriteString(L"Type");
		switch (alloc.m_PackedData.GetResourceDimension()) {
		case D3D12_RESOURCE_DIMENSION_UNKNOWN:
			WriteString(L"UNKNOWN");
			break;
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			WriteString(L"BUFFER");
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			WriteString(L"TEXTURE1D");
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			WriteString(L"TEXTURE2D");
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			WriteString(L"TEXTURE3D");
			break;
		default: D3D12MA_ASSERT(0); break;
		}
		WriteString(L"Size");
		WriteNumber(alloc.GetSize());
		LPCWSTR name = alloc.GetName();
		if (name != NULL)
		{
			WriteString(L"Name");
			WriteString(name);
		}
		if (alloc.m_PackedData.GetResourceFlags())
		{
			WriteString(L"Flags");
			WriteNumber((UINT)alloc.m_PackedData.GetResourceFlags());
		}
		if (alloc.m_PackedData.GetTextureLayout())
		{
			WriteString(L"Layout");
			WriteNumber((UINT)alloc.m_PackedData.GetTextureLayout());
		}
		if (alloc.m_CreationFrameIndex)
		{
			WriteString(L"CreationFrameIndex");
			WriteNumber(alloc.m_CreationFrameIndex);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class PoolAllocator

	/*
	Allocator for objects of type T using a list of arrays (pools) to speed up
	allocation. Number of elements that can be allocated is not bounded because
	allocator can create multiple blocks.
	T should be POD because constructor and destructor is not called in Alloc or
	Free.
	*/
	template<typename T>
	class PoolAllocator
	{
		D3D12MA_CLASS_NO_COPY(PoolAllocator)
	public:
		// allocationCallbacks externally owned, must outlive this object.
		PoolAllocator(const ALLOCATION_CALLBACKS& allocationCallbacks, UINT firstBlockCapacity);
		~PoolAllocator() { Clear(); }
		void Clear();
		template<typename... Types> T* Alloc(Types... args);
		void Free(T* ptr);

	private:
		union Item
		{
			UINT NextFreeIndex; // UINT32_MAX means end of list.
			alignas(T) char Value[sizeof(T)];
		};

		struct ItemBlock
		{
			Item* pItems;
			UINT Capacity;
			UINT FirstFreeIndex;
		};

		const ALLOCATION_CALLBACKS& m_AllocationCallbacks;
		const UINT m_FirstBlockCapacity;
		Vector<ItemBlock> m_ItemBlocks;

		ItemBlock& CreateNewBlock();
	};

	template<typename T>
	PoolAllocator<T>::PoolAllocator(const ALLOCATION_CALLBACKS& allocationCallbacks, UINT firstBlockCapacity) :
		m_AllocationCallbacks(allocationCallbacks),
		m_FirstBlockCapacity(firstBlockCapacity),
		m_ItemBlocks(allocationCallbacks)
	{
		D3D12MA_ASSERT(m_FirstBlockCapacity > 1);
	}

	template<typename T>
	void PoolAllocator<T>::Clear()
	{
		for (size_t i = m_ItemBlocks.size(); i--; )
		{
			D3D12MA_DELETE_ARRAY(m_AllocationCallbacks, m_ItemBlocks[i].pItems, m_ItemBlocks[i].Capacity);
		}
		m_ItemBlocks.clear(true);
	}

	template<typename T>
	template<typename... Types> T* PoolAllocator<T>::Alloc(Types... args)
	{
		for (size_t i = m_ItemBlocks.size(); i--; )
		{
			ItemBlock& block = m_ItemBlocks[i];
			// This block has some free items: Use first one.
			if (block.FirstFreeIndex != UINT32_MAX)
			{
				Item* const pItem = &block.pItems[block.FirstFreeIndex];
				block.FirstFreeIndex = pItem->NextFreeIndex;
				T* result = (T*)&pItem->Value;
				new(result)T(std::forward<Types>(args)...); // Explicit constructor call.
				return result;
			}
		}

		// No block has free item: Create new one and use it.
		ItemBlock& newBlock = CreateNewBlock();
		Item* const pItem = &newBlock.pItems[0];
		newBlock.FirstFreeIndex = pItem->NextFreeIndex;
		T* result = (T*)pItem->Value;
		new(result)T(std::forward<Types>(args)...); // Explicit constructor call.
		return result;
	}

	template<typename T>
	void PoolAllocator<T>::Free(T* ptr)
	{
		// Search all memory blocks to find ptr.
		for (size_t i = m_ItemBlocks.size(); i--; )
		{
			ItemBlock& block = m_ItemBlocks[i];

			Item* pItemPtr;
			memcpy(&pItemPtr, &ptr, sizeof(pItemPtr));

			// Check if pItemPtr is in address range of this block.
			if ((pItemPtr >= block.pItems) && (pItemPtr < block.pItems + block.Capacity))
			{
				ptr->~T(); // Explicit destructor call.
				const UINT index = static_cast<UINT>(pItemPtr - block.pItems);
				pItemPtr->NextFreeIndex = block.FirstFreeIndex;
				block.FirstFreeIndex = index;
				return;
			}
		}
		D3D12MA_ASSERT(0 && "Pointer doesn't belong to this memory pool.");
	}

	template<typename T>
	typename PoolAllocator<T>::ItemBlock& PoolAllocator<T>::CreateNewBlock()
	{
		const UINT newBlockCapacity = m_ItemBlocks.empty() ?
			m_FirstBlockCapacity : m_ItemBlocks.back().Capacity * 3 / 2;

		const ItemBlock newBlock = {
			D3D12MA_NEW_ARRAY(m_AllocationCallbacks, Item, newBlockCapacity),
			newBlockCapacity,
			0 };

		m_ItemBlocks.push_back(newBlock);

		// Setup singly-linked list of all free items in this block.
		for (UINT i = 0; i < newBlockCapacity - 1; ++i)
		{
			newBlock.pItems[i].NextFreeIndex = i + 1;
		}
		newBlock.pItems[newBlockCapacity - 1].NextFreeIndex = UINT32_MAX;
		return m_ItemBlocks.back();
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class List

	/*
	Doubly linked list, with elements allocated out of PoolAllocator.
	Has custom interface, as well as STL-style interface, including iterator and
	const_iterator.
	*/
	template<typename T>
	class List
	{
		D3D12MA_CLASS_NO_COPY(List)
	public:
		struct Item
		{
			Item* pPrev;
			Item* pNext;
			T Value;
		};

		// allocationCallbacks externally owned, must outlive this object.
		List(const ALLOCATION_CALLBACKS& allocationCallbacks);
		~List();
		void Clear();

		size_t GetCount() const { return m_Count; }
		bool IsEmpty() const { return m_Count == 0; }

		Item* Front() { return m_pFront; }
		const Item* Front() const { return m_pFront; }
		Item* Back() { return m_pBack; }
		const Item* Back() const { return m_pBack; }

		Item* PushBack();
		Item* PushFront();
		Item* PushBack(const T& value);
		Item* PushFront(const T& value);
		void PopBack();
		void PopFront();

		// Item can be null - it means PushBack.
		Item* InsertBefore(Item* pItem);
		// Item can be null - it means PushFront.
		Item* InsertAfter(Item* pItem);

		Item* InsertBefore(Item* pItem, const T& value);
		Item* InsertAfter(Item* pItem, const T& value);

		void Remove(Item* pItem);

		class iterator
		{
		public:
			iterator() :
				m_pList(NULL),
				m_pItem(NULL)
			{
			}

			T& operator*() const
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				return m_pItem->Value;
			}
			T* operator->() const
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				return &m_pItem->Value;
			}

			iterator& operator++()
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				m_pItem = m_pItem->pNext;
				return *this;
			}
			iterator& operator--()
			{
				if (m_pItem != NULL)
				{
					m_pItem = m_pItem->pPrev;
				}
				else
				{
					D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
					m_pItem = m_pList->Back();
				}
				return *this;
			}

			iterator operator++(int)
			{
				iterator result = *this;
				++*this;
				return result;
			}
			iterator operator--(int)
			{
				iterator result = *this;
				--*this;
				return result;
			}

			bool operator==(const iterator& rhs) const
			{
				D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
				return m_pItem == rhs.m_pItem;
			}
			bool operator!=(const iterator& rhs) const
			{
				D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
				return m_pItem != rhs.m_pItem;
			}

		private:
			List<T>* m_pList;
			Item* m_pItem;

			iterator(List<T>* pList, Item* pItem) :
				m_pList(pList),
				m_pItem(pItem)
			{
			}

			friend class List<T>;
		};

		class const_iterator
		{
		public:
			const_iterator() :
				m_pList(NULL),
				m_pItem(NULL)
			{
			}

			const_iterator(const iterator& src) :
				m_pList(src.m_pList),
				m_pItem(src.m_pItem)
			{
			}

			const T& operator*() const
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				return m_pItem->Value;
			}
			const T* operator->() const
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				return &m_pItem->Value;
			}

			const_iterator& operator++()
			{
				D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
				m_pItem = m_pItem->pNext;
				return *this;
			}
			const_iterator& operator--()
			{
				if (m_pItem != NULL)
				{
					m_pItem = m_pItem->pPrev;
				}
				else
				{
					D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
					m_pItem = m_pList->Back();
				}
				return *this;
			}

			const_iterator operator++(int)
			{
				const_iterator result = *this;
				++*this;
				return result;
			}
			const_iterator operator--(int)
			{
				const_iterator result = *this;
				--*this;
				return result;
			}

			bool operator==(const const_iterator& rhs) const
			{
				D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
				return m_pItem == rhs.m_pItem;
			}
			bool operator!=(const const_iterator& rhs) const
			{
				D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
				return m_pItem != rhs.m_pItem;
			}

		private:
			const_iterator(const List<T>* pList, const Item* pItem) :
				m_pList(pList),
				m_pItem(pItem)
			{
			}

			const List<T>* m_pList;
			const Item* m_pItem;

			friend class List<T>;
		};

		bool empty() const { return IsEmpty(); }
		size_t size() const { return GetCount(); }

		iterator begin() { return iterator(this, Front()); }
		iterator end() { return iterator(this, NULL); }

		const_iterator cbegin() const { return const_iterator(this, Front()); }
		const_iterator cend() const { return const_iterator(this, NULL); }

		void clear() { Clear(); }
		void push_back(const T& value) { PushBack(value); }
		void erase(iterator it) { Remove(it.m_pItem); }
		iterator insert(iterator it, const T& value) { return iterator(this, InsertBefore(it.m_pItem, value)); }

	private:
		const ALLOCATION_CALLBACKS& m_AllocationCallbacks;
		PoolAllocator<Item> m_ItemAllocator;
		Item* m_pFront;
		Item* m_pBack;
		size_t m_Count;
	};

	template<typename T>
	List<T>::List(const ALLOCATION_CALLBACKS& allocationCallbacks) :
		m_AllocationCallbacks(allocationCallbacks),
		m_ItemAllocator(allocationCallbacks, 128),
		m_pFront(NULL),
		m_pBack(NULL),
		m_Count(0)
	{
	}

	template<typename T>
	List<T>::~List()
	{
		// Intentionally not calling Clear, because that would be unnecessary
		// computations to return all items to m_ItemAllocator as free.
	}

	template<typename T>
	void List<T>::Clear()
	{
		if (!IsEmpty())
		{
			Item* pItem = m_pBack;
			while (pItem != NULL)
			{
				Item* const pPrevItem = pItem->pPrev;
				m_ItemAllocator.Free(pItem);
				pItem = pPrevItem;
			}
			m_pFront = NULL;
			m_pBack = NULL;
			m_Count = 0;
		}
	}

	template<typename T>
	typename List<T>::Item* List<T>::PushBack()
	{
		Item* const pNewItem = m_ItemAllocator.Alloc();
		pNewItem->pNext = NULL;
		if (IsEmpty())
		{
			pNewItem->pPrev = NULL;
			m_pFront = pNewItem;
			m_pBack = pNewItem;
			m_Count = 1;
		}
		else
		{
			pNewItem->pPrev = m_pBack;
			m_pBack->pNext = pNewItem;
			m_pBack = pNewItem;
			++m_Count;
		}
		return pNewItem;
	}

	template<typename T>
	typename List<T>::Item* List<T>::PushFront()
	{
		Item* const pNewItem = m_ItemAllocator.Alloc();
		pNewItem->pPrev = NULL;
		if (IsEmpty())
		{
			pNewItem->pNext = NULL;
			m_pFront = pNewItem;
			m_pBack = pNewItem;
			m_Count = 1;
		}
		else
		{
			pNewItem->pNext = m_pFront;
			m_pFront->pPrev = pNewItem;
			m_pFront = pNewItem;
			++m_Count;
		}
		return pNewItem;
	}

	template<typename T>
	typename List<T>::Item* List<T>::PushBack(const T& value)
	{
		Item* const pNewItem = PushBack();
		pNewItem->Value = value;
		return pNewItem;
	}

	template<typename T>
	typename List<T>::Item* List<T>::PushFront(const T& value)
	{
		Item* const pNewItem = PushFront();
		pNewItem->Value = value;
		return pNewItem;
	}

	template<typename T>
	void List<T>::PopBack()
	{
		D3D12MA_HEAVY_ASSERT(m_Count > 0);
		Item* const pBackItem = m_pBack;
		Item* const pPrevItem = pBackItem->pPrev;
		if (pPrevItem != NULL)
		{
			pPrevItem->pNext = NULL;
		}
		m_pBack = pPrevItem;
		m_ItemAllocator.Free(pBackItem);
		--m_Count;
	}

	template<typename T>
	void List<T>::PopFront()
	{
		D3D12MA_HEAVY_ASSERT(m_Count > 0);
		Item* const pFrontItem = m_pFront;
		Item* const pNextItem = pFrontItem->pNext;
		if (pNextItem != NULL)
		{
			pNextItem->pPrev = NULL;
		}
		m_pFront = pNextItem;
		m_ItemAllocator.Free(pFrontItem);
		--m_Count;
	}

	template<typename T>
	void List<T>::Remove(Item* pItem)
	{
		D3D12MA_HEAVY_ASSERT(pItem != NULL);
		D3D12MA_HEAVY_ASSERT(m_Count > 0);

		if (pItem->pPrev != NULL)
		{
			pItem->pPrev->pNext = pItem->pNext;
		}
		else
		{
			D3D12MA_HEAVY_ASSERT(m_pFront == pItem);
			m_pFront = pItem->pNext;
		}

		if (pItem->pNext != NULL)
		{
			pItem->pNext->pPrev = pItem->pPrev;
		}
		else
		{
			D3D12MA_HEAVY_ASSERT(m_pBack == pItem);
			m_pBack = pItem->pPrev;
		}

		m_ItemAllocator.Free(pItem);
		--m_Count;
	}

	template<typename T>
	typename List<T>::Item* List<T>::InsertBefore(Item* pItem)
	{
		if (pItem != NULL)
		{
			Item* const prevItem = pItem->pPrev;
			Item* const newItem = m_ItemAllocator.Alloc();
			newItem->pPrev = prevItem;
			newItem->pNext = pItem;
			pItem->pPrev = newItem;
			if (prevItem != NULL)
			{
				prevItem->pNext = newItem;
			}
			else
			{
				D3D12MA_HEAVY_ASSERT(m_pFront == pItem);
				m_pFront = newItem;
			}
			++m_Count;
			return newItem;
		}
		else
		{
			return PushBack();
		}
	}

	template<typename T>
	typename List<T>::Item* List<T>::InsertAfter(Item* pItem)
	{
		if (pItem != NULL)
		{
			Item* const nextItem = pItem->pNext;
			Item* const newItem = m_ItemAllocator.Alloc();
			newItem->pNext = nextItem;
			newItem->pPrev = pItem;
			pItem->pNext = newItem;
			if (nextItem != NULL)
			{
				nextItem->pPrev = newItem;
			}
			else
			{
				D3D12MA_HEAVY_ASSERT(m_pBack == pItem);
				m_pBack = newItem;
			}
			++m_Count;
			return newItem;
		}
		else
			return PushFront();
	}

	template<typename T>
	typename List<T>::Item* List<T>::InsertBefore(Item* pItem, const T& value)
	{
		Item* const newItem = InsertBefore(pItem);
		newItem->Value = value;
		return newItem;
	}

	template<typename T>
	typename List<T>::Item* List<T>::InsertAfter(Item* pItem, const T& value)
	{
		Item* const newItem = InsertAfter(pItem);
		newItem->Value = value;
		return newItem;
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class AllocationObjectAllocator definition

	/*
	Thread-safe wrapper over PoolAllocator free list, for allocation of Allocation objects.
	*/
	class AllocationObjectAllocator
	{
		D3D12MA_CLASS_NO_COPY(AllocationObjectAllocator);
	public:
		AllocationObjectAllocator(const ALLOCATION_CALLBACKS& allocationCallbacks);

		template<typename... Types> Allocation* Allocate(Types... args);
		void Free(Allocation* alloc);

	private:
		D3D12MA_MUTEX m_Mutex;
		PoolAllocator<Allocation> m_Allocator;
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class BlockMetadata and derived classes - declarations

	enum SuballocationType
	{
		SUBALLOCATION_TYPE_FREE = 0,
		SUBALLOCATION_TYPE_ALLOCATION = 1,
	};

	/*
	Represents a region of NormalBlock that is either assigned and returned as
	allocated memory block or free.
	*/
	struct Suballocation
	{
		UINT64 offset;
		UINT64 size;
		Allocation* allocation;
		SuballocationType type;
	};

	// Comparator for offsets.
	struct SuballocationOffsetLess
	{
		bool operator()(const Suballocation& lhs, const Suballocation& rhs) const
		{
			return lhs.offset < rhs.offset;
		}
	};
	struct SuballocationOffsetGreater
	{
		bool operator()(const Suballocation& lhs, const Suballocation& rhs) const
		{
			return lhs.offset > rhs.offset;
		}
	};

	typedef List<Suballocation> SuballocationList;

	struct SuballocationItemSizeLess
	{
		bool operator()(const SuballocationList::iterator lhs, const SuballocationList::iterator rhs) const
		{
			return lhs->size < rhs->size;
		}
		bool operator()(const SuballocationList::iterator lhs, UINT64 rhsSize) const
		{
			return lhs->size < rhsSize;
		}
	};

	/*
	Parameters of planned allocation inside a NormalBlock.
	*/
	struct AllocationRequest
	{
		UINT64 offset;
		UINT64 sumFreeSize; // Sum size of free items that overlap with proposed allocation.
		UINT64 sumItemSize; // Sum size of items to make lost that overlap with proposed allocation.
		SuballocationList::iterator item;
		BOOL zeroInitialized;
	};

	/*
	Keeps track of the range of bytes that are surely initialized with zeros.
	Everything outside of it is considered uninitialized memory that may contain
	garbage data.

	The range is left-inclusive.
	*/
	class ZeroInitializedRange
	{
	public:
		void Reset(UINT64 size)
		{
			D3D12MA_ASSERT(size > 0);
			m_ZeroBeg = 0;
			m_ZeroEnd = size;
		}

		BOOL IsRangeZeroInitialized(UINT64 beg, UINT64 end) const
		{
			D3D12MA_ASSERT(beg < end);
			return m_ZeroBeg <= beg && end <= m_ZeroEnd;
		}

		void MarkRangeAsUsed(UINT64 usedBeg, UINT64 usedEnd)
		{
			D3D12MA_ASSERT(usedBeg < usedEnd);
			// No new bytes marked.
			if (usedEnd <= m_ZeroBeg || m_ZeroEnd <= usedBeg)
			{
				return;
			}
			// All bytes marked.
			if (usedBeg <= m_ZeroBeg && m_ZeroEnd <= usedEnd)
			{
				m_ZeroBeg = m_ZeroEnd = 0;
			}
			// Some bytes marked.
			else
			{
				const UINT64 remainingZeroBefore = usedBeg > m_ZeroBeg ? usedBeg - m_ZeroBeg : 0;
				const UINT64 remainingZeroAfter = usedEnd < m_ZeroEnd ? m_ZeroEnd - usedEnd : 0;
				D3D12MA_ASSERT(remainingZeroBefore > 0 || remainingZeroAfter > 0);
				if (remainingZeroBefore > remainingZeroAfter)
				{
					m_ZeroEnd = usedBeg;
				}
				else
				{
					m_ZeroBeg = usedEnd;
				}
			}
		}

	private:
		UINT64 m_ZeroBeg = 0, m_ZeroEnd = 0;
	};

	/*
	Data structure used for bookkeeping of allocations and unused ranges of memory
	in a single ID3D12Heap memory block.
	*/
	class BlockMetadata
	{
	public:
		BlockMetadata(const ALLOCATION_CALLBACKS* allocationCallbacks);
		virtual ~BlockMetadata() { }
		virtual void Init(UINT64 size) { m_Size = size; }

		// Validates all data structures inside this object. If not valid, returns false.
		virtual bool Validate() const = 0;
		UINT64 GetSize() const { return m_Size; }
		virtual size_t GetAllocationCount() const = 0;
		virtual UINT64 GetSumFreeSize() const = 0;
		virtual UINT64 GetUnusedRangeSizeMax() const = 0;
		// Returns true if this block is empty - contains only single free suballocation.
		virtual bool IsEmpty() const = 0;

		// Tries to find a place for suballocation with given parameters inside this block.
		// If succeeded, fills pAllocationRequest and returns true.
		// If failed, returns false.
		virtual bool CreateAllocationRequest(
			UINT64 allocSize,
			UINT64 allocAlignment,
			AllocationRequest* pAllocationRequest) = 0;

		// Makes actual allocation based on request. Request must already be checked and valid.
		virtual void Alloc(
			const AllocationRequest& request,
			UINT64 allocSize,
			Allocation* Allocation) = 0;

		// Frees suballocation assigned to given memory region.
		virtual void Free(const Allocation* allocation) = 0;
		virtual void FreeAtOffset(UINT64 offset) = 0;

		virtual void CalcAllocationStatInfo(StatInfo& outInfo) const = 0;
		virtual void WriteAllocationInfoToJson(JsonWriter& json) const = 0;

	protected:
		const ALLOCATION_CALLBACKS* GetAllocs() const { return m_pAllocationCallbacks; }

	private:
		UINT64 m_Size;
		const ALLOCATION_CALLBACKS* m_pAllocationCallbacks;

		D3D12MA_CLASS_NO_COPY(BlockMetadata);
	};

	class BlockMetadata_Generic : public BlockMetadata
	{
	public:
		BlockMetadata_Generic(const ALLOCATION_CALLBACKS* allocationCallbacks);
		virtual ~BlockMetadata_Generic();
		virtual void Init(UINT64 size);

		virtual bool Validate() const;
		virtual size_t GetAllocationCount() const { return m_Suballocations.size() - m_FreeCount; }
		virtual UINT64 GetSumFreeSize() const { return m_SumFreeSize; }
		virtual UINT64 GetUnusedRangeSizeMax() const;
		virtual bool IsEmpty() const;

		virtual bool CreateAllocationRequest(
			UINT64 allocSize,
			UINT64 allocAlignment,
			AllocationRequest* pAllocationRequest);

		virtual void Alloc(
			const AllocationRequest& request,
			UINT64 allocSize,
			Allocation* hAllocation);

		virtual void Free(const Allocation* allocation);
		virtual void FreeAtOffset(UINT64 offset);

		virtual void CalcAllocationStatInfo(StatInfo& outInfo) const;
		virtual void WriteAllocationInfoToJson(JsonWriter& json) const;

	private:
		UINT m_FreeCount;
		UINT64 m_SumFreeSize;
		SuballocationList m_Suballocations;
		// Suballocations that are free and have size greater than certain threshold.
		// Sorted by size, ascending.
		Vector<SuballocationList::iterator> m_FreeSuballocationsBySize;
		ZeroInitializedRange m_ZeroInitializedRange;

		bool ValidateFreeSuballocationList() const;

		// Checks if requested suballocation with given parameters can be placed in given pFreeSuballocItem.
		// If yes, fills pOffset and returns true. If no, returns false.
		bool CheckAllocation(
			UINT64 allocSize,
			UINT64 allocAlignment,
			SuballocationList::const_iterator suballocItem,
			UINT64* pOffset,
			UINT64* pSumFreeSize,
			UINT64* pSumItemSize,
			BOOL *pZeroInitialized) const;
		// Given free suballocation, it merges it with following one, which must also be free.
		void MergeFreeWithNext(SuballocationList::iterator item);
		// Releases given suballocation, making it free.
		// Merges it with adjacent free suballocations if applicable.
		// Returns iterator to new free suballocation at this place.
		SuballocationList::iterator FreeSuballocation(SuballocationList::iterator suballocItem);
		// Given free suballocation, it inserts it into sorted list of
		// m_FreeSuballocationsBySize if it's suitable.
		void RegisterFreeSuballocation(SuballocationList::iterator item);
		// Given free suballocation, it removes it from sorted list of
		// m_FreeSuballocationsBySize if it's suitable.
		void UnregisterFreeSuballocation(SuballocationList::iterator item);

		D3D12MA_CLASS_NO_COPY(BlockMetadata_Generic)
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class MemoryBlock definition

	/*
	Represents a single block of device memory (heap).
	Base class for inheritance.
	Thread-safety: This class must be externally synchronized.
	*/
	class MemoryBlock
	{
	public:
		MemoryBlock(
			AllocatorPimpl* allocator,
			D3D12_HEAP_TYPE heapType,
			D3D12_HEAP_FLAGS heapFlags,
			// CONFFX_BEGIN - Linked multi gpu
			UINT creationNodeMask,
			UINT visibleNodeMask,
			// CONFFX_END
			UINT64 size,
			UINT id);
		virtual ~MemoryBlock();
		// Creates the ID3D12Heap.

		D3D12_HEAP_TYPE GetHeapType() const { return m_HeapType; }
		D3D12_HEAP_FLAGS GetHeapFlags() const { return m_HeapFlags; }
		// CONFFX_BEGIN - Linked multi gpu
		UINT GetCreationNodeMask() const { return m_CreationNodeMask; }
		UINT GetVisibleNodeMask() const { return m_VisibleNodeMask; }
		// CONFFX_END
		UINT64 GetSize() const { return m_Size; }
		UINT GetId() const { return m_Id; }
		ID3D12Heap* GetHeap() const { return m_Heap; }

	protected:
		AllocatorPimpl* const m_Allocator;
		const D3D12_HEAP_TYPE m_HeapType;
		const D3D12_HEAP_FLAGS m_HeapFlags;
		// CONFFX_BEGIN - Linked multi gpu
		const UINT m_CreationNodeMask;
		const UINT m_VisibleNodeMask;
		// CONFFX_END
		const UINT64 m_Size;
		const UINT m_Id;

		HRESULT Init();

	private:
		ID3D12Heap* m_Heap = NULL;

		D3D12MA_CLASS_NO_COPY(MemoryBlock)
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class NormalBlock definition

	/*
	Represents a single block of device memory (heap) with all the data about its
	regions (aka suballocations, Allocation), assigned and free.
	Thread-safety: This class must be externally synchronized.
	*/
	class NormalBlock : public MemoryBlock
	{
	public:
		BlockMetadata* m_pMetadata;

		NormalBlock(
			AllocatorPimpl* allocator,
			BlockVector* blockVector,
			D3D12_HEAP_TYPE heapType,
			D3D12_HEAP_FLAGS heapFlags,
			// CONFFX_BEGIN - Linked multi gpu
			UINT creationNodeMask,
			UINT visibleNodeMask,
			// CONFFX_END
			UINT64 size,
			UINT id);
		virtual ~NormalBlock();
		HRESULT Init();

		BlockVector* GetBlockVector() const { return m_BlockVector; }

		// Validates all data structures inside this object. If not valid, returns false.
		bool Validate() const;

	private:
		BlockVector* m_BlockVector;

		D3D12MA_CLASS_NO_COPY(NormalBlock)
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class BlockVector definition

	/*
	Sequence of NormalBlock. Represents memory blocks allocated for a specific
	heap type and possibly resource type (if only Tier 1 is supported).

	Synchronized internally with a mutex.
	*/
	class BlockVector
	{
		D3D12MA_CLASS_NO_COPY(BlockVector)
	public:
		BlockVector(
			AllocatorPimpl* hAllocator,
			D3D12_HEAP_TYPE heapType,
			D3D12_HEAP_FLAGS heapFlags,
			UINT64 preferredBlockSize,
			size_t minBlockCount,
			size_t maxBlockCount,
			bool explicitBlockSize);
		~BlockVector();

		HRESULT CreateMinBlocks();

		UINT GetHeapType() const { return m_HeapType; }
		UINT64 GetPreferredBlockSize() const { return m_PreferredBlockSize; }

		bool IsEmpty();

		HRESULT Allocate(
			UINT64 size,
			UINT64 alignment,
			const ALLOCATION_DESC& allocDesc,
			size_t allocationCount,
			Allocation** pAllocations);

		void Free(
			Allocation* hAllocation);

		HRESULT CreateResource(
			UINT64 size,
			UINT64 alignment,
			const ALLOCATION_DESC& allocDesc,
			const D3D12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			Allocation** ppAllocation,
			REFIID riidResource,
			void** ppvResource);

		HRESULT SetMinBytes(UINT64 minBytes);

		void AddStats(StatInfo& outStats);
		void AddStats(Stats& outStats);

		void WriteBlockInfoToJson(JsonWriter& json);

	private:
		AllocatorPimpl* const m_hAllocator;
		const D3D12_HEAP_TYPE m_HeapType;
		const D3D12_HEAP_FLAGS m_HeapFlags;
		const UINT64 m_PreferredBlockSize;
		const size_t m_MinBlockCount;
		const size_t m_MaxBlockCount;
		const bool m_ExplicitBlockSize;
		UINT64 m_MinBytes;
		/* There can be at most one allocation that is completely empty - a
		hysteresis to avoid pessimistic case of alternating creation and destruction
		of a VkDeviceMemory. */
		bool m_HasEmptyBlock;
		D3D12MA_RW_MUTEX m_Mutex;
		// Incrementally sorted by sumFreeSize, ascending.
		Vector<NormalBlock*> m_Blocks;
		UINT m_NextBlockId;

		UINT64 CalcSumBlockSize() const;
		UINT64 CalcMaxBlockSize() const;

		// Finds and removes given block from vector.
		void Remove(NormalBlock* pBlock);

		// Performs single step in sorting m_Blocks. They may not be fully sorted
		// after this call.
		void IncrementallySortBlocks();

		HRESULT AllocatePage(
			UINT64 size,
			UINT64 alignment,
			const ALLOCATION_DESC& allocDesc,
			Allocation** pAllocation);

		HRESULT AllocateFromBlock(
			NormalBlock* pBlock,
			UINT64 size,
			UINT64 alignment,
			ALLOCATION_FLAGS allocFlags,
			// CONFFX_BEGIN - Linked multi gpu
			const ALLOCATION_DESC& allocDesc,
			// CONFFX_END
			Allocation** pAllocation);

		// CONFFX_BEGIN - Linked multi gpu
		HRESULT CreateBlock(UINT64 blockSize, UINT creationNodeMask, UINT visibleNodeMask, size_t* pNewBlockIndex);
		// CONFFX_END
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class AllocatorPimpl definition

	static const UINT DEFAULT_POOL_MAX_COUNT = 9;

	struct CurrentBudgetData
	{
		D3D12MA_ATOMIC_UINT64 m_BlockBytes[HEAP_TYPE_COUNT];
		D3D12MA_ATOMIC_UINT64 m_AllocationBytes[HEAP_TYPE_COUNT];

		D3D12MA_ATOMIC_UINT32 m_OperationsSinceBudgetFetch;
		D3D12MA_RW_MUTEX m_BudgetMutex;
		UINT64 m_D3D12UsageLocal, m_D3D12UsageNonLocal;
		UINT64 m_D3D12BudgetLocal, m_D3D12BudgetNonLocal;
		UINT64 m_BlockBytesAtBudgetFetch[HEAP_TYPE_COUNT];

		CurrentBudgetData()
		{
			for (UINT i = 0; i < HEAP_TYPE_COUNT; ++i)
			{
				m_BlockBytes[i] = 0;
				m_AllocationBytes[i] = 0;
				m_BlockBytesAtBudgetFetch[i] = 0;
			}

			m_D3D12UsageLocal = 0;
			m_D3D12UsageNonLocal = 0;
			m_D3D12BudgetLocal = 0;
			m_D3D12BudgetNonLocal = 0;
			m_OperationsSinceBudgetFetch = 0;
		}

		void AddAllocation(UINT heapTypeIndex, UINT64 allocationSize)
		{
			m_AllocationBytes[heapTypeIndex] += allocationSize;
			++m_OperationsSinceBudgetFetch;
		}

		void RemoveAllocation(UINT heapTypeIndex, UINT64 allocationSize)
		{
			m_AllocationBytes[heapTypeIndex] -= allocationSize;
			++m_OperationsSinceBudgetFetch;
		}
	};

	class AllocatorPimpl
	{
	public:
		CurrentBudgetData m_Budget;

		AllocatorPimpl(const ALLOCATION_CALLBACKS& allocationCallbacks, const ALLOCATOR_DESC& desc);
		HRESULT Init(const ALLOCATOR_DESC& desc);
		~AllocatorPimpl();

		ID3D12Device* GetDevice() const { return m_Device; }
		// Shortcut for "Allocation Callbacks", because this function is called so often.
		const ALLOCATION_CALLBACKS& GetAllocs() const { return m_AllocationCallbacks; }
		const D3D12_FEATURE_DATA_D3D12_OPTIONS& GetD3D12Options() const { return m_D3D12Options; }
		bool SupportsResourceHeapTier2() const { return m_D3D12Options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2; }
		bool UseMutex() const { return m_UseMutex; }
		AllocationObjectAllocator& GetAllocationObjectAllocator() { return m_AllocationObjectAllocator; }
		bool HeapFlagsFulfillResourceHeapTier(D3D12_HEAP_FLAGS flags) const;

		HRESULT CreateResource(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			Allocation** ppAllocation,
			REFIID riidResource,
			void** ppvResource);

		HRESULT AllocateMemory(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_ALLOCATION_INFO* pAllocInfo,
			Allocation** ppAllocation);

		HRESULT CreateAliasingResource(
			Allocation* pAllocation,
			UINT64 AllocationLocalOffset,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			REFIID riidResource,
			void** ppvResource);

		HRESULT SetDefaultHeapMinBytes(
			D3D12_HEAP_TYPE heapType,
			D3D12_HEAP_FLAGS heapFlags,
			UINT64 minBytes);

		// Unregisters allocation from the collection of dedicated allocations.
		// Allocation object must be deleted externally afterwards.
		void FreeCommittedMemory(Allocation* allocation);
		// Unregisters allocation from the collection of placed allocations.
		// Allocation object must be deleted externally afterwards.
		void FreePlacedMemory(Allocation* allocation);
		// Unregisters allocation from the collection of dedicated allocations and destroys associated heap.
		// Allocation object must be deleted externally afterwards.
		void FreeHeapMemory(Allocation* allocation);

		void SetCurrentFrameIndex(UINT frameIndex);

		UINT GetCurrentFrameIndex() const { return m_CurrentFrameIndex.load(); }

		void CalculateStats(Stats& outStats);

		void GetBudget(Budget* outGpuBudget, Budget* outCpuBudget);
		void GetBudgetForHeapType(Budget& outBudget, D3D12_HEAP_TYPE heapType);

		void BuildStatsString(WCHAR** ppStatsString, BOOL DetailedMap);

		void FreeStatsString(WCHAR* pStatsString);

	private:
		friend class Allocator;
		friend class Pool;

		/*
		Heuristics that decides whether a resource should better be placed in its own,
		dedicated allocation (committed resource rather than placed resource).
		*/
		static bool PrefersCommittedAllocation(const D3D12_RESOURCE_DESC& resourceDesc);

		const bool m_UseMutex;
		const bool m_AlwaysCommitted;
		ID3D12Device* m_Device; // AddRef
		IDXGIAdapter* m_Adapter; // AddRef
#if D3D12MA_DXGI_1_4
		IDXGIAdapter3* m_Adapter3; // AddRef, optional
#endif
		UINT64 m_PreferredBlockSize;
		ALLOCATION_CALLBACKS m_AllocationCallbacks;
		D3D12MA_ATOMIC_UINT32 m_CurrentFrameIndex;
		DXGI_ADAPTER_DESC m_AdapterDesc;
		D3D12_FEATURE_DATA_D3D12_OPTIONS m_D3D12Options;
		AllocationObjectAllocator m_AllocationObjectAllocator;

		typedef Vector<Allocation*> AllocationVectorType;
		AllocationVectorType* m_pCommittedAllocations[HEAP_TYPE_COUNT];
		D3D12MA_RW_MUTEX m_CommittedAllocationsMutex[HEAP_TYPE_COUNT];

		typedef Vector<Pool*> PoolVectorType;
		PoolVectorType* m_pPools[HEAP_TYPE_COUNT];
		D3D12MA_RW_MUTEX m_PoolsMutex[HEAP_TYPE_COUNT];

		// Default pools.
		BlockVector* m_BlockVectors[DEFAULT_POOL_MAX_COUNT];

		// # Used only when ResourceHeapTier = 1
		UINT64 m_DefaultPoolTier1MinBytes[DEFAULT_POOL_MAX_COUNT]; // Default 0
		UINT64 m_DefaultPoolHeapTypeMinBytes[HEAP_TYPE_COUNT]; // Default UINT64_MAX, meaning not set
		D3D12MA_RW_MUTEX m_DefaultPoolMinBytesMutex;

		// Allocates and registers new committed resource with implicit heap, as dedicated allocation.
		// Creates and returns Allocation object.
		HRESULT AllocateCommittedResource(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			Allocation** ppAllocation,
			REFIID riidResource,
			void** ppvResource);

		// Allocates and registers new heap without any resources placed in it, as dedicated allocation.
		// Creates and returns Allocation object.
		HRESULT AllocateHeap(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_ALLOCATION_INFO& allocInfo,
			Allocation** ppAllocation);

		/*
		If SupportsResourceHeapTier2():
			0: D3D12_HEAP_TYPE_DEFAULT
			1: D3D12_HEAP_TYPE_UPLOAD
			2: D3D12_HEAP_TYPE_READBACK
		else:
			0: D3D12_HEAP_TYPE_DEFAULT + buffer
			1: D3D12_HEAP_TYPE_DEFAULT + texture
			2: D3D12_HEAP_TYPE_DEFAULT + texture RT or DS
			3: D3D12_HEAP_TYPE_UPLOAD + buffer
			4: D3D12_HEAP_TYPE_UPLOAD + texture
			5: D3D12_HEAP_TYPE_UPLOAD + texture RT or DS
			6: D3D12_HEAP_TYPE_READBACK + buffer
			7: D3D12_HEAP_TYPE_READBACK + texture
			8: D3D12_HEAP_TYPE_READBACK + texture RT or DS
		*/
		UINT CalcDefaultPoolCount() const;
		UINT CalcDefaultPoolIndex(const ALLOCATION_DESC& allocDesc, const D3D12_RESOURCE_DESC& resourceDesc) const;
		// This one returns UINT32_MAX if nonstandard heap flags are used and index cannot be calculcated.
		static UINT CalcDefaultPoolIndex(D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags, bool supportsResourceHeapTier2);
		UINT CalcDefaultPoolIndex(D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags) const
		{
			return CalcDefaultPoolIndex(heapType, heapFlags, SupportsResourceHeapTier2());
		}
		UINT CalcDefaultPoolIndex(const ALLOCATION_DESC& allocDesc) const
		{
			return CalcDefaultPoolIndex(allocDesc.HeapType, allocDesc.ExtraHeapFlags);
		}
		void CalcDefaultPoolParams(D3D12_HEAP_TYPE& outHeapType, D3D12_HEAP_FLAGS& outHeapFlags, UINT index) const;

		// Registers Allocation object in m_pCommittedAllocations.
		void RegisterCommittedAllocation(Allocation* alloc, D3D12_HEAP_TYPE heapType);
		// Unregisters Allocation object from m_pCommittedAllocations.
		void UnregisterCommittedAllocation(Allocation* alloc, D3D12_HEAP_TYPE heapType);

		// Registers Pool object in m_pPools.
		void RegisterPool(Pool* pool, D3D12_HEAP_TYPE heapType);
		// Unregisters Pool object from m_pPools.
		void UnregisterPool(Pool* pool, D3D12_HEAP_TYPE heapType);

		HRESULT UpdateD3D12Budget();

		D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(D3D12_RESOURCE_DESC& inOutResourceDesc) const;

		// Writes object { } with data of given budget.
		static void WriteBudgetToJson(JsonWriter& json, const Budget& budget);
	};

	////////////////////////////////////////////////////////////////////////////////
	// Private class BlockMetadata implementation

	BlockMetadata::BlockMetadata(const ALLOCATION_CALLBACKS* allocationCallbacks) :
		m_Size(0),
		m_pAllocationCallbacks(allocationCallbacks)
	{
		D3D12MA_ASSERT(allocationCallbacks);
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class BlockMetadata_Generic implementation

	BlockMetadata_Generic::BlockMetadata_Generic(const ALLOCATION_CALLBACKS* allocationCallbacks) :
		BlockMetadata(allocationCallbacks),
		m_FreeCount(0),
		m_SumFreeSize(0),
		m_Suballocations(*allocationCallbacks),
		m_FreeSuballocationsBySize(*allocationCallbacks)
	{
		D3D12MA_ASSERT(allocationCallbacks);
	}

	BlockMetadata_Generic::~BlockMetadata_Generic()
	{
	}

	void BlockMetadata_Generic::Init(UINT64 size)
	{
		BlockMetadata::Init(size);
		m_ZeroInitializedRange.Reset(size);

		m_FreeCount = 1;
		m_SumFreeSize = size;

		Suballocation suballoc = {};
		suballoc.offset = 0;
		suballoc.size = size;
		suballoc.type = SUBALLOCATION_TYPE_FREE;
		suballoc.allocation = NULL;

		D3D12MA_ASSERT(size > MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER);
		m_Suballocations.push_back(suballoc);
		SuballocationList::iterator suballocItem = m_Suballocations.end();
		--suballocItem;
		m_FreeSuballocationsBySize.push_back(suballocItem);
	}

	bool BlockMetadata_Generic::Validate() const
	{
		D3D12MA_VALIDATE(!m_Suballocations.empty());

		// Expected offset of new suballocation as calculated from previous ones.
		UINT64 calculatedOffset = 0;
		// Expected number of free suballocations as calculated from traversing their list.
		UINT calculatedFreeCount = 0;
		// Expected sum size of free suballocations as calculated from traversing their list.
		UINT64 calculatedSumFreeSize = 0;
		// Expected number of free suballocations that should be registered in
		// m_FreeSuballocationsBySize calculated from traversing their list.
		size_t freeSuballocationsToRegister = 0;
		// True if previous visited suballocation was free.
		bool prevFree = false;

		for (SuballocationList::const_iterator suballocItem = m_Suballocations.cbegin();
			suballocItem != m_Suballocations.cend();
			++suballocItem)
		{
			const Suballocation& subAlloc = *suballocItem;

			// Actual offset of this suballocation doesn't match expected one.
			D3D12MA_VALIDATE(subAlloc.offset == calculatedOffset);

			const bool currFree = (subAlloc.type == SUBALLOCATION_TYPE_FREE);
			// Two adjacent free suballocations are invalid. They should be merged.
			D3D12MA_VALIDATE(!prevFree || !currFree);

			D3D12MA_VALIDATE(currFree == (subAlloc.allocation == NULL));

			if (currFree)
			{
				calculatedSumFreeSize += subAlloc.size;
				++calculatedFreeCount;
				if (subAlloc.size >= MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER)
				{
					++freeSuballocationsToRegister;
				}

				// Margin required between allocations - every free space must be at least that large.
				D3D12MA_VALIDATE(subAlloc.size >= D3D12MA_DEBUG_MARGIN);
			}
			else
			{
				D3D12MA_VALIDATE(subAlloc.allocation->GetOffset() == subAlloc.offset);
				D3D12MA_VALIDATE(subAlloc.allocation->GetSize() == subAlloc.size);

				// Margin required between allocations - previous allocation must be free.
				D3D12MA_VALIDATE(D3D12MA_DEBUG_MARGIN == 0 || prevFree);
			}

			calculatedOffset += subAlloc.size;
			prevFree = currFree;
		}

		// Number of free suballocations registered in m_FreeSuballocationsBySize doesn't
		// match expected one.
		D3D12MA_VALIDATE(m_FreeSuballocationsBySize.size() == freeSuballocationsToRegister);

		UINT64 lastSize = 0;
		for (size_t i = 0; i < m_FreeSuballocationsBySize.size(); ++i)
		{
			SuballocationList::iterator suballocItem = m_FreeSuballocationsBySize[i];

			// Only free suballocations can be registered in m_FreeSuballocationsBySize.
			D3D12MA_VALIDATE(suballocItem->type == SUBALLOCATION_TYPE_FREE);
			// They must be sorted by size ascending.
			D3D12MA_VALIDATE(suballocItem->size >= lastSize);

			lastSize = suballocItem->size;
		}

		// Check if totals match calculacted values.
		D3D12MA_VALIDATE(ValidateFreeSuballocationList());
		D3D12MA_VALIDATE(calculatedOffset == GetSize());
		D3D12MA_VALIDATE(calculatedSumFreeSize == m_SumFreeSize);
		D3D12MA_VALIDATE(calculatedFreeCount == m_FreeCount);

		return true;
	}

	UINT64 BlockMetadata_Generic::GetUnusedRangeSizeMax() const
	{
		if (!m_FreeSuballocationsBySize.empty())
		{
			return m_FreeSuballocationsBySize.back()->size;
		}
		else
		{
			return 0;
		}
	}

	bool BlockMetadata_Generic::IsEmpty() const
	{
		return (m_Suballocations.size() == 1) && (m_FreeCount == 1);
	}

	bool BlockMetadata_Generic::CreateAllocationRequest(
		UINT64 allocSize,
		UINT64 allocAlignment,
		AllocationRequest* pAllocationRequest)
	{
		D3D12MA_ASSERT(allocSize > 0);
		D3D12MA_ASSERT(pAllocationRequest != NULL);
		D3D12MA_HEAVY_ASSERT(Validate());

		// There is not enough total free space in this block to fullfill the request: Early return.
		if (m_SumFreeSize < allocSize + 2 * D3D12MA_DEBUG_MARGIN)
		{
			return false;
		}

		// New algorithm, efficiently searching freeSuballocationsBySize.
		const size_t freeSuballocCount = m_FreeSuballocationsBySize.size();
		if (freeSuballocCount > 0)
		{
			// Find first free suballocation with size not less than allocSize + 2 * D3D12MA_DEBUG_MARGIN.
			SuballocationList::iterator* const it = BinaryFindFirstNotLess(
				m_FreeSuballocationsBySize.data(),
				m_FreeSuballocationsBySize.data() + freeSuballocCount,
				allocSize + 2 * D3D12MA_DEBUG_MARGIN,
				SuballocationItemSizeLess());
			size_t index = it - m_FreeSuballocationsBySize.data();
			for (; index < freeSuballocCount; ++index)
			{
				if (CheckAllocation(
					allocSize,
					allocAlignment,
					m_FreeSuballocationsBySize[index],
					&pAllocationRequest->offset,
					&pAllocationRequest->sumFreeSize,
					&pAllocationRequest->sumItemSize,
					&pAllocationRequest->zeroInitialized))
				{
					pAllocationRequest->item = m_FreeSuballocationsBySize[index];
					return true;
				}
			}
		}

		return false;
	}

	void BlockMetadata_Generic::Alloc(
		const AllocationRequest& request,
		UINT64 allocSize,
		Allocation* allocation)
	{
		D3D12MA_ASSERT(request.item != m_Suballocations.end());
		Suballocation& suballoc = *request.item;
		// Given suballocation is a free block.
		D3D12MA_ASSERT(suballoc.type == SUBALLOCATION_TYPE_FREE);
		// Given offset is inside this suballocation.
		D3D12MA_ASSERT(request.offset >= suballoc.offset);
		const UINT64 paddingBegin = request.offset - suballoc.offset;
		D3D12MA_ASSERT(suballoc.size >= paddingBegin + allocSize);
		const UINT64 paddingEnd = suballoc.size - paddingBegin - allocSize;

		// Unregister this free suballocation from m_FreeSuballocationsBySize and update
		// it to become used.
		UnregisterFreeSuballocation(request.item);

		suballoc.offset = request.offset;
		suballoc.size = allocSize;
		suballoc.type = SUBALLOCATION_TYPE_ALLOCATION;
		suballoc.allocation = allocation;

		// If there are any free bytes remaining at the end, insert new free suballocation after current one.
		if (paddingEnd)
		{
			Suballocation paddingSuballoc = {};
			paddingSuballoc.offset = request.offset + allocSize;
			paddingSuballoc.size = paddingEnd;
			paddingSuballoc.type = SUBALLOCATION_TYPE_FREE;
			SuballocationList::iterator next = request.item;
			++next;
			const SuballocationList::iterator paddingEndItem =
				m_Suballocations.insert(next, paddingSuballoc);
			RegisterFreeSuballocation(paddingEndItem);
		}

		// If there are any free bytes remaining at the beginning, insert new free suballocation before current one.
		if (paddingBegin)
		{
			Suballocation paddingSuballoc = {};
			paddingSuballoc.offset = request.offset - paddingBegin;
			paddingSuballoc.size = paddingBegin;
			paddingSuballoc.type = SUBALLOCATION_TYPE_FREE;
			const SuballocationList::iterator paddingBeginItem =
				m_Suballocations.insert(request.item, paddingSuballoc);
			RegisterFreeSuballocation(paddingBeginItem);
		}

		// Update totals.
		m_FreeCount = m_FreeCount - 1;
		if (paddingBegin > 0)
		{
			++m_FreeCount;
		}
		if (paddingEnd > 0)
		{
			++m_FreeCount;
		}
		m_SumFreeSize -= allocSize;

		m_ZeroInitializedRange.MarkRangeAsUsed(request.offset, request.offset + allocSize);
	}

	void BlockMetadata_Generic::Free(const Allocation* allocation)
	{
		for (SuballocationList::iterator suballocItem = m_Suballocations.begin();
			suballocItem != m_Suballocations.end();
			++suballocItem)
		{
			Suballocation& suballoc = *suballocItem;
			if (suballoc.allocation == allocation)
			{
				FreeSuballocation(suballocItem);
				D3D12MA_HEAVY_ASSERT(Validate());
				return;
			}
		}
		D3D12MA_ASSERT(0 && "Not found!");
	}

	void BlockMetadata_Generic::FreeAtOffset(UINT64 offset)
	{
		for (SuballocationList::iterator suballocItem = m_Suballocations.begin();
			suballocItem != m_Suballocations.end();
			++suballocItem)
		{
			Suballocation& suballoc = *suballocItem;
			if (suballoc.offset == offset)
			{
				FreeSuballocation(suballocItem);
				return;
			}
		}
		D3D12MA_ASSERT(0 && "Not found!");
	}

	bool BlockMetadata_Generic::ValidateFreeSuballocationList() const
	{
		UINT64 lastSize = 0;
		for (size_t i = 0, count = m_FreeSuballocationsBySize.size(); i < count; ++i)
		{
			const SuballocationList::iterator it = m_FreeSuballocationsBySize[i];

			D3D12MA_VALIDATE(it->type == SUBALLOCATION_TYPE_FREE);
			D3D12MA_VALIDATE(it->size >= MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER);
			D3D12MA_VALIDATE(it->size >= lastSize);
			lastSize = it->size;
		}
		return true;
	}

	bool BlockMetadata_Generic::CheckAllocation(
		UINT64 allocSize,
		UINT64 allocAlignment,
		SuballocationList::const_iterator suballocItem,
		UINT64* pOffset,
		UINT64* pSumFreeSize,
		UINT64* pSumItemSize,
		BOOL *pZeroInitialized) const
	{
		D3D12MA_ASSERT(allocSize > 0);
		D3D12MA_ASSERT(suballocItem != m_Suballocations.cend());
		D3D12MA_ASSERT(pOffset != NULL && pZeroInitialized != NULL);

		*pSumFreeSize = 0;
		*pSumItemSize = 0;
		*pZeroInitialized = FALSE;

		const Suballocation& suballoc = *suballocItem;
		D3D12MA_ASSERT(suballoc.type == SUBALLOCATION_TYPE_FREE);

		*pSumFreeSize = suballoc.size;

		// Size of this suballocation is too small for this request: Early return.
		if (suballoc.size < allocSize)
		{
			return false;
		}

		// Start from offset equal to beginning of this suballocation.
		*pOffset = suballoc.offset;

		// Apply D3D12MA_DEBUG_MARGIN at the beginning.
		if (D3D12MA_DEBUG_MARGIN > 0)
		{
			*pOffset += D3D12MA_DEBUG_MARGIN;
		}

		// Apply alignment.
		*pOffset = AlignUp(*pOffset, allocAlignment);

		// Calculate padding at the beginning based on current offset.
		const UINT64 paddingBegin = *pOffset - suballoc.offset;

		// Calculate required margin at the end.
		const UINT64 requiredEndMargin = D3D12MA_DEBUG_MARGIN;

		// Fail if requested size plus margin before and after is bigger than size of this suballocation.
		if (paddingBegin + allocSize + requiredEndMargin > suballoc.size)
		{
			return false;
		}

		// All tests passed: Success. pOffset is already filled.
		*pZeroInitialized = m_ZeroInitializedRange.IsRangeZeroInitialized(*pOffset, *pOffset + allocSize);
		return true;
	}

	void BlockMetadata_Generic::MergeFreeWithNext(SuballocationList::iterator item)
	{
		D3D12MA_ASSERT(item != m_Suballocations.end());
		D3D12MA_ASSERT(item->type == SUBALLOCATION_TYPE_FREE);

		SuballocationList::iterator nextItem = item;
		++nextItem;
		D3D12MA_ASSERT(nextItem != m_Suballocations.end());
		D3D12MA_ASSERT(nextItem->type == SUBALLOCATION_TYPE_FREE);

		item->size += nextItem->size;
		--m_FreeCount;
		m_Suballocations.erase(nextItem);
	}

	SuballocationList::iterator BlockMetadata_Generic::FreeSuballocation(SuballocationList::iterator suballocItem)
	{
		// Change this suballocation to be marked as free.
		Suballocation& suballoc = *suballocItem;
		suballoc.type = SUBALLOCATION_TYPE_FREE;
		suballoc.allocation = NULL;

		// Update totals.
		++m_FreeCount;
		m_SumFreeSize += suballoc.size;

		// Merge with previous and/or next suballocation if it's also free.
		bool mergeWithNext = false;
		bool mergeWithPrev = false;

		SuballocationList::iterator nextItem = suballocItem;
		++nextItem;
		if ((nextItem != m_Suballocations.end()) && (nextItem->type == SUBALLOCATION_TYPE_FREE))
		{
			mergeWithNext = true;
		}

		SuballocationList::iterator prevItem = suballocItem;
		if (suballocItem != m_Suballocations.begin())
		{
			--prevItem;
			if (prevItem->type == SUBALLOCATION_TYPE_FREE)
			{
				mergeWithPrev = true;
			}
		}

		if (mergeWithNext)
		{
			UnregisterFreeSuballocation(nextItem);
			MergeFreeWithNext(suballocItem);
		}

		if (mergeWithPrev)
		{
			UnregisterFreeSuballocation(prevItem);
			MergeFreeWithNext(prevItem);
			RegisterFreeSuballocation(prevItem);
			return prevItem;
		}
		else
		{
			RegisterFreeSuballocation(suballocItem);
			return suballocItem;
		}
	}

	void BlockMetadata_Generic::RegisterFreeSuballocation(SuballocationList::iterator item)
	{
		D3D12MA_ASSERT(item->type == SUBALLOCATION_TYPE_FREE);
		D3D12MA_ASSERT(item->size > 0);

		// You may want to enable this validation at the beginning or at the end of
		// this function, depending on what do you want to check.
		D3D12MA_HEAVY_ASSERT(ValidateFreeSuballocationList());

		if (item->size >= MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER)
		{
			if (m_FreeSuballocationsBySize.empty())
			{
				m_FreeSuballocationsBySize.push_back(item);
			}
			else
			{
				m_FreeSuballocationsBySize.InsertSorted(item, SuballocationItemSizeLess());
			}
		}

		//D3D12MA_HEAVY_ASSERT(ValidateFreeSuballocationList());
	}


	void BlockMetadata_Generic::UnregisterFreeSuballocation(SuballocationList::iterator item)
	{
		D3D12MA_ASSERT(item->type == SUBALLOCATION_TYPE_FREE);
		D3D12MA_ASSERT(item->size > 0);

		// You may want to enable this validation at the beginning or at the end of
		// this function, depending on what do you want to check.
		D3D12MA_HEAVY_ASSERT(ValidateFreeSuballocationList());

		if (item->size >= MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER)
		{
			SuballocationList::iterator* const it = BinaryFindFirstNotLess(
				m_FreeSuballocationsBySize.data(),
				m_FreeSuballocationsBySize.data() + m_FreeSuballocationsBySize.size(),
				item,
				SuballocationItemSizeLess());
			for (size_t index = it - m_FreeSuballocationsBySize.data();
				index < m_FreeSuballocationsBySize.size();
				++index)
			{
				if (m_FreeSuballocationsBySize[index] == item)
				{
					m_FreeSuballocationsBySize.remove(index);
					return;
				}
				D3D12MA_ASSERT((m_FreeSuballocationsBySize[index]->size == item->size) && "Not found.");
			}
			D3D12MA_ASSERT(0 && "Not found.");
		}

		//D3D12MA_HEAVY_ASSERT(ValidateFreeSuballocationList());
	}

	void BlockMetadata_Generic::CalcAllocationStatInfo(StatInfo& outInfo) const
	{
		outInfo.BlockCount = 1;

		const UINT rangeCount = (UINT)m_Suballocations.size();
		outInfo.AllocationCount = rangeCount - m_FreeCount;
		outInfo.UnusedRangeCount = m_FreeCount;

		outInfo.UsedBytes = GetSize() - m_SumFreeSize;
		outInfo.UnusedBytes = m_SumFreeSize;

		outInfo.AllocationSizeMin = UINT64_MAX;
		outInfo.AllocationSizeMax = 0;
		outInfo.UnusedRangeSizeMin = UINT64_MAX;
		outInfo.UnusedRangeSizeMax = 0;

		for (SuballocationList::const_iterator suballocItem = m_Suballocations.cbegin();
			suballocItem != m_Suballocations.cend();
			++suballocItem)
		{
			const Suballocation& suballoc = *suballocItem;
			if (suballoc.type == SUBALLOCATION_TYPE_FREE)
			{
				outInfo.UnusedRangeSizeMin = D3D12MA_MIN(suballoc.size, outInfo.UnusedRangeSizeMin);
				outInfo.UnusedRangeSizeMax = D3D12MA_MAX(suballoc.size, outInfo.UnusedRangeSizeMax);
			}
			else
			{
				outInfo.AllocationSizeMin = D3D12MA_MIN(suballoc.size, outInfo.AllocationSizeMin);
				outInfo.AllocationSizeMax = D3D12MA_MAX(suballoc.size, outInfo.AllocationSizeMax);
			}
		}
	}

	void BlockMetadata_Generic::WriteAllocationInfoToJson(JsonWriter& json) const
	{
		json.BeginObject();
		json.WriteString(L"TotalBytes");
		json.WriteNumber(GetSize());
		json.WriteString(L"UnusuedBytes");
		json.WriteNumber(GetSumFreeSize());
		json.WriteString(L"Allocations");
		json.WriteNumber(GetAllocationCount());
		json.WriteString(L"UnusedRanges");
		json.WriteNumber(m_FreeCount);
		json.WriteString(L"Suballocations");
		json.BeginArray();
		for (SuballocationList::const_iterator suballocItem = m_Suballocations.cbegin();
			suballocItem != m_Suballocations.cend();
			++suballocItem)
		{
			const Suballocation& suballoc = *suballocItem;
			json.BeginObject(true);
			json.WriteString(L"Offset");
			json.WriteNumber(suballoc.offset);
			if (suballoc.type == SUBALLOCATION_TYPE_FREE)
			{
				json.WriteString(L"Type");
				json.WriteString(L"FREE");
				json.WriteString(L"Size");
				json.WriteNumber(suballoc.size);
			}
			else
			{
				const Allocation* const alloc = suballoc.allocation;
				D3D12MA_ASSERT(alloc);
				json.AddAllocationToObject(*alloc);
			}
			json.EndObject();
		}
		json.EndArray();
		json.EndObject();
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class NormalBlock implementation

	NormalBlock::NormalBlock(
		AllocatorPimpl* allocator,
		BlockVector* blockVector,
		D3D12_HEAP_TYPE heapType,
		D3D12_HEAP_FLAGS heapFlags,
		// CONFFX_BEGIN - Linked multi gpu
		UINT creationNodeMask,
		UINT visibleNodeMask,
		// CONFFX_END
		UINT64 size,
		UINT id) :
		// CONFFX_BEGIN - Linked multi gpu
		MemoryBlock(allocator, heapType, heapFlags, creationNodeMask, visibleNodeMask, size, id),
		// CONFFX_END
		m_pMetadata(NULL),
		m_BlockVector(blockVector)
	{
	}

	NormalBlock::~NormalBlock()
	{
		if (m_pMetadata != NULL)
		{
			// THIS IS THE MOST IMPORTANT ASSERT IN THE ENTIRE LIBRARY!
			// Hitting it means you have some memory leak - unreleased Allocation objects.
			D3D12MA_ASSERT(m_pMetadata->IsEmpty() && "Some allocations were not freed before destruction of this memory block!");

			D3D12MA_DELETE(m_Allocator->GetAllocs(), m_pMetadata);
		}
	}

	HRESULT NormalBlock::Init()
	{
		HRESULT hr = MemoryBlock::Init();
		if (FAILED(hr))
		{
			return hr;
		}

		m_pMetadata = D3D12MA_NEW(m_Allocator->GetAllocs(), BlockMetadata_Generic)(&m_Allocator->GetAllocs());
		m_pMetadata->Init(m_Size);

		return hr;
	}

	bool NormalBlock::Validate() const
	{
		D3D12MA_VALIDATE(GetHeap() &&
			m_pMetadata &&
			m_pMetadata->GetSize() != 0 &&
			m_pMetadata->GetSize() == GetSize());
		return m_pMetadata->Validate();
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class MemoryBlock definition

	MemoryBlock::MemoryBlock(
		AllocatorPimpl* allocator,
		D3D12_HEAP_TYPE heapType,
		D3D12_HEAP_FLAGS heapFlags,
		// CONFFX_BEGIN - Linked multi gpu
		UINT creationNodeMask,
		UINT visibleNodeMask,
		// CONFFX_END
		UINT64 size,
		UINT id) :
		m_Allocator(allocator),
		m_HeapType(heapType),
		m_HeapFlags(heapFlags),
		// CONFFX_BEGIN - Linked multi gpu
		m_CreationNodeMask(creationNodeMask),
		m_VisibleNodeMask(visibleNodeMask),
		// CONFFX_END
		m_Size(size),
		m_Id(id)
	{
	}

	MemoryBlock::~MemoryBlock()
	{
		if (m_Heap)
		{
			m_Allocator->m_Budget.m_BlockBytes[HeapTypeToIndex(m_HeapType)] -= m_Size;
			m_Heap->Release();
		}
	}

	HRESULT MemoryBlock::Init()
	{
		D3D12MA_ASSERT(m_Heap == NULL && m_Size > 0);

		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = m_Size;
		heapDesc.Properties.Type = m_HeapType;
		// CONFFX_BEGIN - Linked multi gpu
		heapDesc.Properties.CreationNodeMask = m_CreationNodeMask;
		heapDesc.Properties.VisibleNodeMask = m_VisibleNodeMask;
		// CONFFX_END
		heapDesc.Alignment = HeapFlagsToAlignment(m_HeapFlags);
		heapDesc.Flags = m_HeapFlags;

		HRESULT hr = m_Allocator->GetDevice()->CreateHeap(&heapDesc, __uuidof(*m_Heap), (void**)&m_Heap);
		if (SUCCEEDED(hr))
		{
			m_Allocator->m_Budget.m_BlockBytes[HeapTypeToIndex(m_HeapType)] += m_Size;
		}
		return hr;
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class BlockVector implementation

	BlockVector::BlockVector(
		AllocatorPimpl* hAllocator,
		D3D12_HEAP_TYPE heapType,
		D3D12_HEAP_FLAGS heapFlags,
		UINT64 preferredBlockSize,
		size_t minBlockCount,
		size_t maxBlockCount,
		bool explicitBlockSize) :
		m_hAllocator(hAllocator),
		m_HeapType(heapType),
		m_HeapFlags(heapFlags),
		m_PreferredBlockSize(preferredBlockSize),
		m_MinBlockCount(minBlockCount),
		m_MaxBlockCount(maxBlockCount),
		m_ExplicitBlockSize(explicitBlockSize),
		m_MinBytes(0),
		m_HasEmptyBlock(false),
		m_Blocks(hAllocator->GetAllocs()),
		m_NextBlockId(0)
	{
	}

	BlockVector::~BlockVector()
	{
		for (size_t i = m_Blocks.size(); i--; )
		{
			D3D12MA_DELETE(m_hAllocator->GetAllocs(), m_Blocks[i]);
		}
	}

	HRESULT BlockVector::CreateMinBlocks()
	{
		for (size_t i = 0; i < m_MinBlockCount; ++i)
		{
			// CONFFX_BEGIN - Linked multi gpu
			// Create blocks for default node (node 0 or node mask 1)
			HRESULT hr = CreateBlock(m_PreferredBlockSize, 1, 1, NULL);
			// CONFFX_END
			if (FAILED(hr))
			{
				return hr;
			}
		}
		return S_OK;
	}

	bool BlockVector::IsEmpty()
	{
		MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());
		return m_Blocks.empty();
	}

	HRESULT BlockVector::Allocate(
		UINT64 size,
		UINT64 alignment,
		const ALLOCATION_DESC& allocDesc,
		size_t allocationCount,
		Allocation** pAllocations)
	{
		size_t allocIndex;
		HRESULT hr = S_OK;

		{
			MutexLockWrite lock(m_Mutex, m_hAllocator->UseMutex());
			for (allocIndex = 0; allocIndex < allocationCount; ++allocIndex)
			{
				hr = AllocatePage(
					size,
					alignment,
					allocDesc,
					pAllocations + allocIndex);
				if (FAILED(hr))
				{
					break;
				}
			}
		}

		if (FAILED(hr))
		{
			// Free all already created allocations.
			while (allocIndex--)
			{
				Free(pAllocations[allocIndex]);
			}
			ZeroMemory(pAllocations, sizeof(Allocation*) * allocationCount);
		}

		return hr;
	}

	HRESULT BlockVector::AllocatePage(
		UINT64 size,
		UINT64 alignment,
		const ALLOCATION_DESC& allocDesc,
		Allocation** pAllocation)
	{
		// Early reject: requested allocation size is larger that maximum block size for this block vector.
		if (size + 2 * D3D12MA_DEBUG_MARGIN > m_PreferredBlockSize)
		{
			return E_OUTOFMEMORY;
		}

		UINT64 freeMemory;
		{
			Budget budget = {};
			m_hAllocator->GetBudgetForHeapType(budget, m_HeapType);
			freeMemory = (budget.UsageBytes < budget.BudgetBytes) ? (budget.BudgetBytes - budget.UsageBytes) : 0;
		}

		const bool canCreateNewBlock =
			((allocDesc.Flags & ALLOCATION_FLAG_NEVER_ALLOCATE) == 0) &&
			(m_Blocks.size() < m_MaxBlockCount) &&
			// Even if we don't have to stay within budget with this allocation, when the
			// budget would be exceeded, we don't want to allocate new blocks, but always
			// create resources as committed.
			freeMemory >= size;

		// 1. Search existing allocations
		{
			// Forward order in m_Blocks - prefer blocks with smallest amount of free space.
			for (size_t blockIndex = 0; blockIndex < m_Blocks.size(); ++blockIndex)
			{
				NormalBlock* const pCurrBlock = m_Blocks[blockIndex];
				D3D12MA_ASSERT(pCurrBlock);
				HRESULT hr = AllocateFromBlock(
					pCurrBlock,
					size,
					alignment,
					allocDesc.Flags,
					// CONFFX_BEGIN - Linked multi gpu
					allocDesc,
					// CONFFX_END
					pAllocation);
				if (SUCCEEDED(hr))
				{
					return hr;
				}
			}
		}

		// 2. Try to create new block.
		if (canCreateNewBlock)
		{
			// Calculate optimal size for new block.
			UINT64 newBlockSize = m_PreferredBlockSize;
			UINT newBlockSizeShift = 0;

			if (!m_ExplicitBlockSize)
			{
				// Allocate 1/8, 1/4, 1/2 as first blocks.
				const UINT64 maxExistingBlockSize = CalcMaxBlockSize();
				for (UINT i = 0; i < NEW_BLOCK_SIZE_SHIFT_MAX; ++i)
				{
					const UINT64 smallerNewBlockSize = newBlockSize / 2;
					if (smallerNewBlockSize > maxExistingBlockSize && smallerNewBlockSize >= size * 2)
					{
						newBlockSize = smallerNewBlockSize;
						++newBlockSizeShift;
					}
					else
					{
						break;
					}
				}
			}

			size_t newBlockIndex = 0;
			HRESULT hr = newBlockSize <= freeMemory ?
				// CONFFX_BEGIN - Linked multi gpu
				CreateBlock(newBlockSize, allocDesc.CreationNodeMask, allocDesc.VisibleNodeMask, &newBlockIndex) : E_OUTOFMEMORY;
			// CONFFX_END
			// Allocation of this size failed? Try 1/2, 1/4, 1/8 of m_PreferredBlockSize.
			if (!m_ExplicitBlockSize)
			{
				while (FAILED(hr) && newBlockSizeShift < NEW_BLOCK_SIZE_SHIFT_MAX)
				{
					const UINT64 smallerNewBlockSize = newBlockSize / 2;
					if (smallerNewBlockSize >= size)
					{
						newBlockSize = smallerNewBlockSize;
						++newBlockSizeShift;
						hr = newBlockSize <= freeMemory ?
							// CONFFX_BEGIN - Linked multi gpu
							CreateBlock(newBlockSize, allocDesc.CreationNodeMask, allocDesc.VisibleNodeMask, &newBlockIndex) : E_OUTOFMEMORY;
							// CONFFX_END
					}
					else
					{
						break;
					}
				}
			}

			if (SUCCEEDED(hr))
			{
				NormalBlock* const pBlock = m_Blocks[newBlockIndex];
				D3D12MA_ASSERT(pBlock->m_pMetadata->GetSize() >= size);

				hr = AllocateFromBlock(
					pBlock,
					size,
					alignment,
					allocDesc.Flags,
					// CONFFX_BEGIN - Linked multi gpu
					allocDesc,
					// CONFFX_END
					pAllocation);
				if (SUCCEEDED(hr))
				{
					return hr;
				}
				else
				{
					// Allocation from new block failed, possibly due to D3D12MA_DEBUG_MARGIN or alignment.
					return E_OUTOFMEMORY;
				}
			}
		}

		return E_OUTOFMEMORY;
	}

	void BlockVector::Free(Allocation* hAllocation)
	{
		NormalBlock* pBlockToDelete = NULL;

		bool budgetExceeded = false;
		{
			Budget budget = {};
			m_hAllocator->GetBudgetForHeapType(budget, m_HeapType);
			budgetExceeded = budget.UsageBytes >= budget.BudgetBytes;
		}

		// Scope for lock.
		{
			MutexLockWrite lock(m_Mutex, m_hAllocator->UseMutex());

			NormalBlock* pBlock = hAllocation->m_Placed.block;

			pBlock->m_pMetadata->Free(hAllocation);
			D3D12MA_HEAVY_ASSERT(pBlock->Validate());

			const size_t blockCount = m_Blocks.size();
			const UINT64 sumBlockSize = CalcSumBlockSize();
			// pBlock became empty after this deallocation.
			if (pBlock->m_pMetadata->IsEmpty())
			{
				// Already has empty Allocation. We don't want to have two, so delete this one.
				if ((m_HasEmptyBlock || budgetExceeded) &&
					blockCount > m_MinBlockCount &&
					sumBlockSize - pBlock->m_pMetadata->GetSize() >= m_MinBytes)
				{
					pBlockToDelete = pBlock;
					Remove(pBlock);
				}
				// We now have first empty block.
				else
				{
					m_HasEmptyBlock = true;
				}
			}
			// pBlock didn't become empty, but we have another empty block - find and free that one.
			// (This is optional, heuristics.)
			else if (m_HasEmptyBlock && blockCount > m_MinBlockCount)
			{
				NormalBlock* pLastBlock = m_Blocks.back();
				if (pLastBlock->m_pMetadata->IsEmpty() &&
					sumBlockSize - pLastBlock->m_pMetadata->GetSize() >= m_MinBytes)
				{
					pBlockToDelete = pLastBlock;
					m_Blocks.pop_back();
					m_HasEmptyBlock = false;
				}
			}

			IncrementallySortBlocks();
		}

		// Destruction of a free Allocation. Deferred until this point, outside of mutex
		// lock, for performance reason.
		if (pBlockToDelete != NULL)
		{
			D3D12MA_DELETE(m_hAllocator->GetAllocs(), pBlockToDelete);
		}
	}

	HRESULT BlockVector::CreateResource(
		UINT64 size,
		UINT64 alignment,
		const ALLOCATION_DESC& allocDesc,
		const D3D12_RESOURCE_DESC& resourceDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		Allocation** ppAllocation,
		REFIID riidResource,
		void** ppvResource)
	{
		HRESULT hr = Allocate(size, alignment, allocDesc, 1, ppAllocation);
		if (SUCCEEDED(hr))
		{
			ID3D12Resource* res = NULL;
			hr = m_hAllocator->GetDevice()->CreatePlacedResource(
				(*ppAllocation)->m_Placed.block->GetHeap(),
				(*ppAllocation)->GetOffset(),
				&resourceDesc,
				InitialResourceState,
				pOptimizedClearValue,
				riidResource,
				(void**)&res);
			if (SUCCEEDED(hr))
			{
				(*ppAllocation)->SetResource(res, &resourceDesc);
				if (ppvResource != NULL)
				{
					res->AddRef();
					*ppvResource = res;
				}
			}
			else
			{
				SAFE_RELEASE(*ppAllocation);
			}
		}
		return hr;
	}

	UINT64 BlockVector::CalcSumBlockSize() const
	{
		UINT64 result = 0;
		for (size_t i = m_Blocks.size(); i--; )
		{
			result += m_Blocks[i]->m_pMetadata->GetSize();
		}
		return result;
	}

	UINT64 BlockVector::CalcMaxBlockSize() const
	{
		UINT64 result = 0;
		for (size_t i = m_Blocks.size(); i--; )
		{
			result = D3D12MA_MAX(result, m_Blocks[i]->m_pMetadata->GetSize());
			if (result >= m_PreferredBlockSize)
			{
				break;
			}
		}
		return result;
	}

	void BlockVector::Remove(NormalBlock* pBlock)
	{
		for (size_t blockIndex = 0; blockIndex < m_Blocks.size(); ++blockIndex)
		{
			if (m_Blocks[blockIndex] == pBlock)
			{
				m_Blocks.remove(blockIndex);
				return;
			}
		}
		D3D12MA_ASSERT(0);
	}

	void BlockVector::IncrementallySortBlocks()
	{
		// Bubble sort only until first swap.
		for (size_t i = 1; i < m_Blocks.size(); ++i)
		{
			if (m_Blocks[i - 1]->m_pMetadata->GetSumFreeSize() > m_Blocks[i]->m_pMetadata->GetSumFreeSize())
			{
				D3D12MA_SWAP(m_Blocks[i - 1], m_Blocks[i]);
				return;
			}
		}
	}

	HRESULT BlockVector::AllocateFromBlock(
		NormalBlock* pBlock,
		UINT64 size,
		UINT64 alignment,
		ALLOCATION_FLAGS allocFlags,
		// CONFFX_BEGIN - Linked multi gpu
		const ALLOCATION_DESC& allocDesc,
		// CONFFX_END
		Allocation** pAllocation)
	{
		// CONFFX_BEGIN - Linked multi gpu
		// Check if node masks are compatible
		// #NOTE: Visible node mask only has to be a subset of the block's visible node mask so maybe we can think
		// of changing
		// (pBlock->GetVisibleNodeMask() != allocDesc.VisibleNodeMask) to (allocDesc.VisibleNodeMask & pBlock->GetVisibleNodeMask())
		if (pBlock->GetCreationNodeMask() != allocDesc.CreationNodeMask || pBlock->GetVisibleNodeMask() != allocDesc.VisibleNodeMask)
			return E_OUTOFMEMORY;
		// CONFFX_END

		AllocationRequest currRequest = {};
		if (pBlock->m_pMetadata->CreateAllocationRequest(
			size,
			alignment,
			&currRequest))
		{
			// We no longer have an empty Allocation.
			if (pBlock->m_pMetadata->IsEmpty())
			{
				m_HasEmptyBlock = false;
			}

			*pAllocation = m_hAllocator->GetAllocationObjectAllocator().Allocate(m_hAllocator, size, currRequest.zeroInitialized);
			pBlock->m_pMetadata->Alloc(currRequest, size, *pAllocation);
			(*pAllocation)->InitPlaced(currRequest.offset, alignment, pBlock);
			D3D12MA_HEAVY_ASSERT(pBlock->Validate());
			m_hAllocator->m_Budget.AddAllocation(HeapTypeToIndex(m_HeapType), size);
			return S_OK;
		}
		return E_OUTOFMEMORY;
	}

	// CONFFX_BEGIN - Linked multi gpu
	HRESULT BlockVector::CreateBlock(UINT64 blockSize, UINT creationNodeMask, UINT visibleNodeMask, size_t* pNewBlockIndex)
	// CONFFX_END
	{
		NormalBlock* const pBlock = D3D12MA_NEW(m_hAllocator->GetAllocs(), NormalBlock)(
			m_hAllocator,
			this,
			m_HeapType,
			m_HeapFlags,
			// CONFFX_BEGIN - Linked multi gpu
			creationNodeMask,
			visibleNodeMask,
			// CONFFX_END
			blockSize,
			m_NextBlockId++);
		HRESULT hr = pBlock->Init();
		if (FAILED(hr))
		{
			D3D12MA_DELETE(m_hAllocator->GetAllocs(), pBlock);
			return hr;
		}

		m_Blocks.push_back(pBlock);
		if (pNewBlockIndex != NULL)
		{
			*pNewBlockIndex = m_Blocks.size() - 1;
		}

		return hr;
	}

	HRESULT BlockVector::SetMinBytes(UINT64 minBytes)
	{
		MutexLockWrite lock(m_Mutex, m_hAllocator->UseMutex());

		if (minBytes == m_MinBytes)
		{
			return S_OK;
		}

		HRESULT hr = S_OK;
		UINT64 sumBlockSize = CalcSumBlockSize();
		size_t blockCount = m_Blocks.size();

		// New minBytes is smaller - may be able to free some blocks.
		if (minBytes < m_MinBytes)
		{
			m_HasEmptyBlock = false; // Will recalculate this value from scratch.
			for (size_t blockIndex = blockCount; blockIndex--; )
			{
				NormalBlock* const block = m_Blocks[blockIndex];
				const UINT64 size = block->m_pMetadata->GetSize();
				const bool isEmpty = block->m_pMetadata->IsEmpty();
				if (isEmpty &&
					sumBlockSize - size >= minBytes &&
					blockCount - 1 >= m_MinBlockCount)
				{
					D3D12MA_DELETE(m_hAllocator->GetAllocs(), block);
					m_Blocks.remove(blockIndex);
					sumBlockSize -= size;
					--blockCount;
				}
				else
				{
					if (isEmpty)
					{
						m_HasEmptyBlock = true;
					}
				}
			}
		}
		// New minBytes is larger - may need to allocate some blocks.
		else
		{
			const UINT64 minBlockSize = m_PreferredBlockSize >> NEW_BLOCK_SIZE_SHIFT_MAX;
			while (SUCCEEDED(hr) && sumBlockSize < minBytes)
			{
				if (blockCount < m_MaxBlockCount)
				{
					UINT64 newBlockSize = m_PreferredBlockSize;
					if (!m_ExplicitBlockSize)
					{
						if (sumBlockSize + newBlockSize > minBytes)
						{
							newBlockSize = minBytes - sumBlockSize;
						}
						// Next one would be the last block to create and its size would be smaller than
						// the smallest block size we want to use here, so make this one smaller.
						else if (blockCount + 1 < m_MaxBlockCount &&
							sumBlockSize + newBlockSize + minBlockSize > minBytes)
						{
							newBlockSize -= minBlockSize + sumBlockSize + m_PreferredBlockSize - minBytes;
						}
					}

					hr = CreateBlock(newBlockSize, 1, 1, NULL);
					if (SUCCEEDED(hr))
					{
						m_HasEmptyBlock = true;
						sumBlockSize += newBlockSize;
						++blockCount;
					}
				}
				else
				{
					hr = E_INVALIDARG;
				}
			}
		}

		m_MinBytes = minBytes;
		return hr;
	}

	void BlockVector::AddStats(StatInfo& outStats)
	{
		MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

		for (size_t i = 0; i < m_Blocks.size(); ++i)
		{
			const NormalBlock* const pBlock = m_Blocks[i];
			D3D12MA_ASSERT(pBlock);
			D3D12MA_HEAVY_ASSERT(pBlock->Validate());
			StatInfo blockStatInfo;
			pBlock->m_pMetadata->CalcAllocationStatInfo(blockStatInfo);
			AddStatInfo(outStats, blockStatInfo);
		}
	}

	void BlockVector::AddStats(Stats& outStats)
	{
		const UINT heapTypeIndex = HeapTypeToIndex(m_HeapType);
		StatInfo* const pStatInfo = &outStats.HeapType[heapTypeIndex];

		MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

		for (size_t i = 0; i < m_Blocks.size(); ++i)
		{
			const NormalBlock* const pBlock = m_Blocks[i];
			D3D12MA_ASSERT(pBlock);
			D3D12MA_HEAVY_ASSERT(pBlock->Validate());
			StatInfo blockStatInfo;
			pBlock->m_pMetadata->CalcAllocationStatInfo(blockStatInfo);
			AddStatInfo(outStats.Total, blockStatInfo);
			AddStatInfo(*pStatInfo, blockStatInfo);
		}
	}

	void BlockVector::WriteBlockInfoToJson(JsonWriter& json)
	{
		MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

		json.BeginObject();

		for (size_t i = 0, count = m_Blocks.size(); i < count; ++i)
		{
			const NormalBlock* const pBlock = m_Blocks[i];
			D3D12MA_ASSERT(pBlock);
			D3D12MA_HEAVY_ASSERT(pBlock->Validate());
			json.BeginString();
			json.ContinueString(pBlock->GetId());
			json.EndString();

			pBlock->m_pMetadata->WriteAllocationInfoToJson(json);
		}

		json.EndObject();
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class PoolPimpl

	class PoolPimpl
	{
	public:
		PoolPimpl(AllocatorPimpl* allocator, const POOL_DESC& desc);
		HRESULT Init();
		~PoolPimpl();

		AllocatorPimpl* GetAllocator() const { return m_Allocator; }
		const POOL_DESC& GetDesc() const { return m_Desc; }
		BlockVector* GetBlockVector() { return m_BlockVector; }

		HRESULT SetMinBytes(UINT64 minBytes) { return m_BlockVector->SetMinBytes(minBytes); }

		void CalculateStats(StatInfo& outStats);

		void SetName(LPCWSTR Name);
		LPCWSTR GetName() const { return m_Name; }

	private:
		friend class Allocator;

		AllocatorPimpl* m_Allocator; // Externally owned object.
		POOL_DESC m_Desc;
		BlockVector* m_BlockVector; // Owned object.
		wchar_t* m_Name;

		void FreeName();
	};

	PoolPimpl::PoolPimpl(AllocatorPimpl* allocator, const POOL_DESC& desc) :
		m_Allocator(allocator),
		m_Desc(desc),
		m_BlockVector(NULL),
		m_Name(NULL)
	{
		const bool explicitBlockSize = desc.BlockSize != 0;
		const UINT64 preferredBlockSize = explicitBlockSize ? desc.BlockSize : D3D12MA_DEFAULT_BLOCK_SIZE;

		D3D12_HEAP_FLAGS heapFlags = desc.HeapFlags;

		UINT maxBlockCount = desc.MaxBlockCount != 0 ? desc.MaxBlockCount : UINT_MAX;

		m_BlockVector = D3D12MA_NEW(allocator->GetAllocs(), BlockVector)(
			allocator, desc.HeapType, heapFlags,
			preferredBlockSize,
			desc.MinBlockCount, maxBlockCount,
			explicitBlockSize);
	}

	HRESULT PoolPimpl::Init()
	{
		return m_BlockVector->CreateMinBlocks();
	}

	PoolPimpl::~PoolPimpl()
	{
		FreeName();
		D3D12MA_DELETE(m_Allocator->GetAllocs(), m_BlockVector);
	}

	void PoolPimpl::CalculateStats(StatInfo& outStats)
	{
		ZeroMemory(&outStats, sizeof(outStats));
		outStats.AllocationSizeMin = UINT64_MAX;
		outStats.UnusedRangeSizeMin = UINT64_MAX;

		m_BlockVector->AddStats(outStats);

		PostProcessStatInfo(outStats);
	}

	void PoolPimpl::SetName(LPCWSTR Name)
	{
		FreeName();

		if (Name)
		{
			const size_t nameCharCount = wcslen(Name) + 1;
			m_Name = D3D12MA_NEW_ARRAY(m_Allocator->GetAllocs(), WCHAR, nameCharCount);
			memcpy(m_Name, Name, nameCharCount * sizeof(WCHAR));
		}
	}

	void PoolPimpl::FreeName()
	{
		if (m_Name)
		{
			const size_t nameCharCount = wcslen(m_Name) + 1;
			D3D12MA_DELETE_ARRAY(m_Allocator->GetAllocs(), m_Name, nameCharCount);
			m_Name = NULL;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Public class Pool implementation

	void Pool::Release()
	{
		if (this == NULL)
		{
			return;
		}

		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

			D3D12MA_DELETE(m_Pimpl->GetAllocator()->GetAllocs(), this);
	}

	POOL_DESC Pool::GetDesc() const
	{
		return m_Pimpl->GetDesc();
	}

	HRESULT Pool::SetMinBytes(UINT64 minBytes)
	{
		return m_Pimpl->SetMinBytes(minBytes);
	}

	void Pool::CalculateStats(StatInfo* pStats)
	{
		D3D12MA_ASSERT(pStats);
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->CalculateStats(*pStats);
	}

	void Pool::SetName(LPCWSTR Name)
	{
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->SetName(Name);
	}

	LPCWSTR Pool::GetName() const
	{
		return m_Pimpl->GetName();
	}

	Pool::Pool(Allocator* allocator, const POOL_DESC &desc) :
		m_Pimpl(D3D12MA_NEW(allocator->m_Pimpl->GetAllocs(), PoolPimpl)(allocator->m_Pimpl, desc))
	{
	}

	Pool::~Pool()
	{
		m_Pimpl->GetAllocator()->UnregisterPool(this, m_Pimpl->GetDesc().HeapType);

		D3D12MA_DELETE(m_Pimpl->GetAllocator()->GetAllocs(), m_Pimpl);
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class AllocatorPimpl implementation

	AllocatorPimpl::AllocatorPimpl(const ALLOCATION_CALLBACKS& allocationCallbacks, const ALLOCATOR_DESC& desc) :
		m_UseMutex((desc.Flags & ALLOCATOR_FLAG_SINGLETHREADED) == 0),
		m_AlwaysCommitted((desc.Flags & ALLOCATOR_FLAG_ALWAYS_COMMITTED) != 0),
		m_Device(desc.pDevice),
		m_Adapter(desc.pAdapter),
#if D3D12MA_DXGI_1_4
		m_Adapter3(NULL),
#endif
		m_PreferredBlockSize(desc.PreferredBlockSize != 0 ? desc.PreferredBlockSize : D3D12MA_DEFAULT_BLOCK_SIZE),
		m_AllocationCallbacks(allocationCallbacks),
		m_CurrentFrameIndex(0),
		// Below this line don't use allocationCallbacks but m_AllocationCallbacks!!!
		m_AllocationObjectAllocator(m_AllocationCallbacks)
	{
		// desc.pAllocationCallbacks intentionally ignored here, preprocessed by CreateAllocator.
		ZeroMemory(&m_D3D12Options, sizeof(m_D3D12Options));

		ZeroMemory(m_pCommittedAllocations, sizeof(m_pCommittedAllocations));
		ZeroMemory(m_pPools, sizeof(m_pPools));
		ZeroMemory(m_BlockVectors, sizeof(m_BlockVectors));
		ZeroMemory(m_DefaultPoolTier1MinBytes, sizeof(m_DefaultPoolTier1MinBytes));

		for (UINT i = 0; i < HEAP_TYPE_COUNT; ++i)
		{
			m_DefaultPoolHeapTypeMinBytes[i] = UINT64_MAX;
		}

		for (UINT heapTypeIndex = 0; heapTypeIndex < HEAP_TYPE_COUNT; ++heapTypeIndex)
		{
			m_pCommittedAllocations[heapTypeIndex] = D3D12MA_NEW(GetAllocs(), AllocationVectorType)(GetAllocs());
			m_pPools[heapTypeIndex] = D3D12MA_NEW(GetAllocs(), PoolVectorType)(GetAllocs());
		}

		m_Device->AddRef();
		m_Adapter->AddRef();
	}

	HRESULT AllocatorPimpl::Init(const ALLOCATOR_DESC& desc)
	{
#if D3D12MA_DXGI_1_4
		desc.pAdapter->QueryInterface<IDXGIAdapter3>(&m_Adapter3);
#endif

		HRESULT hr = m_Adapter->GetDesc(&m_AdapterDesc);
		if (FAILED(hr))
		{
			return hr;
		}

		hr = m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &m_D3D12Options, sizeof(m_D3D12Options));
		if (FAILED(hr))
		{
			return hr;
		}

		const UINT defaultPoolCount = CalcDefaultPoolCount();
		for (UINT i = 0; i < defaultPoolCount; ++i)
		{
			D3D12_HEAP_TYPE heapType;
			D3D12_HEAP_FLAGS heapFlags;
			CalcDefaultPoolParams(heapType, heapFlags, i);

			m_BlockVectors[i] = D3D12MA_NEW(GetAllocs(), BlockVector)(
				this, // hAllocator
				heapType, // heapType
				heapFlags, // heapFlags
				m_PreferredBlockSize,
				0, // minBlockCount
				SIZE_MAX, // maxBlockCount
				false); // explicitBlockSize
			// No need to call m_pBlockVectors[i]->CreateMinBlocks here, becase minBlockCount is 0.
		}

#if D3D12MA_DXGI_1_4
		if (m_Adapter3)
		{
			UpdateD3D12Budget();
		}
#endif

		return S_OK;
	}

	AllocatorPimpl::~AllocatorPimpl()
	{
#if D3D12MA_DXGI_1_4
		SAFE_RELEASE(m_Adapter3);
#endif
		SAFE_RELEASE(m_Adapter);
		SAFE_RELEASE(m_Device);

		for (UINT i = DEFAULT_POOL_MAX_COUNT; i--; )
		{
			D3D12MA_DELETE(GetAllocs(), m_BlockVectors[i]);
		}

		for (UINT i = HEAP_TYPE_COUNT; i--; )
		{
			if (m_pPools[i] && !m_pPools[i]->empty())
			{
				D3D12MA_ASSERT(0 && "Unfreed pools found!");
			}

			D3D12MA_DELETE(GetAllocs(), m_pPools[i]);
		}

		for (UINT i = HEAP_TYPE_COUNT; i--; )
		{
			if (m_pCommittedAllocations[i] && !m_pCommittedAllocations[i]->empty())
			{
				D3D12MA_ASSERT(0 && "Unfreed committed allocations found!");
			}

			D3D12MA_DELETE(GetAllocs(), m_pCommittedAllocations[i]);
		}
	}

	bool AllocatorPimpl::HeapFlagsFulfillResourceHeapTier(D3D12_HEAP_FLAGS flags) const
	{
		if (SupportsResourceHeapTier2())
		{
			return true;
		}
		else
		{
			const bool allowBuffers = (flags & D3D12_HEAP_FLAG_DENY_BUFFERS) == 0;
			const bool allowRtDsTextures = (flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) == 0;
			const bool allowNonRtDsTextures = (flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES) == 0;
			const uint8_t allowedGroupCount = (allowBuffers ? 1 : 0) + (allowRtDsTextures ? 1 : 0) + (allowNonRtDsTextures ? 1 : 0);
			return allowedGroupCount == 1;
		}
	}

	HRESULT AllocatorPimpl::CreateResource(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_DESC* pResourceDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		Allocation** ppAllocation,
		REFIID riidResource,
		void** ppvResource)
	{
		*ppAllocation = NULL;
		if (ppvResource)
		{
			*ppvResource = NULL;
		}

		if (pAllocDesc->CustomPool == NULL)
		{
			if (!IsHeapTypeValid(pAllocDesc->HeapType))
			{
				return E_INVALIDARG;
			}
		}

		ALLOCATION_DESC finalAllocDesc = *pAllocDesc;

		// CONFFX_BEGIN - Linked multi gpu
		// Can have only one bit set in creation node mask
		D3D12MA_ASSERT(IsPow2(finalAllocDesc.CreationNodeMask));
		// CONFFX_END

		D3D12_RESOURCE_DESC finalResourceDesc = *pResourceDesc;
		D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = GetResourceAllocationInfo(finalResourceDesc);
		resAllocInfo.Alignment = D3D12MA_MAX<UINT64>(resAllocInfo.Alignment, D3D12MA_DEBUG_ALIGNMENT);

		// CONFFX_BEGIN - Xbox alignment fix
#if defined(XBOX)
		resAllocInfo.Alignment = pResourceDesc->SampleDesc.Count > 1 ?
			D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
#endif
		// CONFFX_END

		D3D12MA_ASSERT(IsPow2(resAllocInfo.Alignment));
		D3D12MA_ASSERT(resAllocInfo.SizeInBytes > 0);

		if (pAllocDesc->CustomPool != NULL)
		{
			if ((finalAllocDesc.Flags & ALLOCATION_FLAG_COMMITTED) != 0)
			{
				return E_INVALIDARG;
			}

			BlockVector* blockVector = pAllocDesc->CustomPool->m_Pimpl->GetBlockVector();
			D3D12MA_ASSERT(blockVector);
			return blockVector->CreateResource(
				resAllocInfo.SizeInBytes,
				resAllocInfo.Alignment,
				finalAllocDesc,
				finalResourceDesc,
				InitialResourceState,
				pOptimizedClearValue,
				ppAllocation,
				riidResource,
				ppvResource);
		}
		else
		{
			const UINT defaultPoolIndex = CalcDefaultPoolIndex(*pAllocDesc, finalResourceDesc);
			const bool requireCommittedMemory = defaultPoolIndex == UINT32_MAX;
			if (requireCommittedMemory)
			{
				return AllocateCommittedResource(
					&finalAllocDesc,
					&finalResourceDesc,
					resAllocInfo,
					InitialResourceState,
					pOptimizedClearValue,
					ppAllocation,
					riidResource,
					ppvResource);
			}

			BlockVector* const blockVector = m_BlockVectors[defaultPoolIndex];
			D3D12MA_ASSERT(blockVector);

			const UINT64 preferredBlockSize = blockVector->GetPreferredBlockSize();
			bool preferCommittedMemory =
				m_AlwaysCommitted ||
				PrefersCommittedAllocation(finalResourceDesc) ||
				// Heuristics: Allocate committed memory if requested size if greater than half of preferred block size.
				resAllocInfo.SizeInBytes > preferredBlockSize / 2;
			if (preferCommittedMemory &&
				(finalAllocDesc.Flags & ALLOCATION_FLAG_NEVER_ALLOCATE) == 0)
			{
				finalAllocDesc.Flags |= ALLOCATION_FLAG_COMMITTED;
			}

			if ((finalAllocDesc.Flags & ALLOCATION_FLAG_COMMITTED) != 0)
			{
				return AllocateCommittedResource(
					&finalAllocDesc,
					&finalResourceDesc,
					resAllocInfo,
					InitialResourceState,
					pOptimizedClearValue,
					ppAllocation,
					riidResource,
					ppvResource);
			}
			else
			{
				HRESULT hr = blockVector->CreateResource(
					resAllocInfo.SizeInBytes,
					resAllocInfo.Alignment,
					finalAllocDesc,
					finalResourceDesc,
					InitialResourceState,
					pOptimizedClearValue,
					ppAllocation,
					riidResource,
					ppvResource);
				if (SUCCEEDED(hr))
				{
					return hr;
				}

				return AllocateCommittedResource(
					&finalAllocDesc,
					&finalResourceDesc,
					resAllocInfo,
					InitialResourceState,
					pOptimizedClearValue,
					ppAllocation,
					riidResource,
					ppvResource);
			}
		}
	}

	HRESULT AllocatorPimpl::AllocateMemory(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_ALLOCATION_INFO* pAllocInfo,
		Allocation** ppAllocation)
	{
		*ppAllocation = NULL;

		if (pAllocDesc->CustomPool != NULL)
		{
			BlockVector* const blockVector = pAllocDesc->CustomPool->m_Pimpl->GetBlockVector();
			D3D12MA_ASSERT(blockVector);
			return blockVector->Allocate(
				pAllocInfo->SizeInBytes,
				pAllocInfo->Alignment,
				*pAllocDesc,
				1,
				(Allocation**)ppAllocation);
		}
		else
		{
			if (!IsHeapTypeValid(pAllocDesc->HeapType))
			{
				return E_INVALIDARG;
			}

			ALLOCATION_DESC finalAllocDesc = *pAllocDesc;

			const UINT defaultPoolIndex = CalcDefaultPoolIndex(*pAllocDesc);
			bool requireCommittedMemory = (defaultPoolIndex == UINT32_MAX);
			if (requireCommittedMemory)
			{
				return AllocateHeap(&finalAllocDesc, *pAllocInfo, ppAllocation);
			}

			BlockVector* blockVector = m_BlockVectors[defaultPoolIndex];
			D3D12MA_ASSERT(blockVector);

			const UINT64 preferredBlockSize = blockVector->GetPreferredBlockSize();
			const bool preferCommittedMemory =
				m_AlwaysCommitted ||
				// Heuristics: Allocate committed memory if requested size if greater than half of preferred block size.
				pAllocInfo->SizeInBytes > preferredBlockSize / 2;
			if (preferCommittedMemory &&
				(finalAllocDesc.Flags & ALLOCATION_FLAG_NEVER_ALLOCATE) == 0)
			{
				finalAllocDesc.Flags |= ALLOCATION_FLAG_COMMITTED;
			}

			if ((finalAllocDesc.Flags & ALLOCATION_FLAG_COMMITTED) != 0)
			{
				return AllocateHeap(&finalAllocDesc, *pAllocInfo, ppAllocation);
			}
			else
			{
				HRESULT hr = blockVector->Allocate(
					pAllocInfo->SizeInBytes,
					pAllocInfo->Alignment,
					finalAllocDesc,
					1,
					(Allocation**)ppAllocation);
				if (SUCCEEDED(hr))
				{
					return hr;
				}

				return AllocateHeap(&finalAllocDesc, *pAllocInfo, ppAllocation);
			}
		}
	}

	HRESULT AllocatorPimpl::CreateAliasingResource(
		Allocation* pAllocation,
		UINT64 AllocationLocalOffset,
		const D3D12_RESOURCE_DESC* pResourceDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		REFIID riidResource,
		void** ppvResource)
	{
		*ppvResource = NULL;

		D3D12_RESOURCE_DESC resourceDesc2 = *pResourceDesc;
		D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = GetResourceAllocationInfo(resourceDesc2);
		resAllocInfo.Alignment = D3D12MA_MAX<UINT64>(resAllocInfo.Alignment, D3D12MA_DEBUG_ALIGNMENT);
		D3D12MA_ASSERT(IsPow2(resAllocInfo.Alignment));
		D3D12MA_ASSERT(resAllocInfo.SizeInBytes > 0);

		ID3D12Heap* const existingHeap = pAllocation->GetHeap();
		const UINT64 existingOffset = pAllocation->GetOffset();
		const UINT64 existingSize = pAllocation->GetSize();
		const UINT64 newOffset = existingOffset + AllocationLocalOffset;

		if (existingHeap == NULL ||
			AllocationLocalOffset + resAllocInfo.SizeInBytes > existingSize ||
			newOffset % resAllocInfo.Alignment != 0)
		{
			return E_INVALIDARG;
		}

		return m_Device->CreatePlacedResource(
			existingHeap,
			newOffset,
			&resourceDesc2,
			InitialResourceState,
			pOptimizedClearValue,
			riidResource,
			ppvResource);
	}

	HRESULT AllocatorPimpl::SetDefaultHeapMinBytes(
		D3D12_HEAP_TYPE heapType,
		D3D12_HEAP_FLAGS heapFlags,
		UINT64 minBytes)
	{
		if (!IsHeapTypeValid(heapType))
		{
			D3D12MA_ASSERT(0 && "Allocator::SetDefaultHeapMinBytes: Invalid heapType passed.");
			return E_INVALIDARG;
		}

		if (SupportsResourceHeapTier2())
		{
			if (heapFlags != D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES &&
				heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS &&
				heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES &&
				heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES)
			{
				D3D12MA_ASSERT(0 && "Allocator::SetDefaultHeapMinBytes: Invalid heapFlags passed.");
				return E_INVALIDARG;
			}

			UINT64 newMinBytes = UINT64_MAX;

			{
				MutexLockWrite lock(m_DefaultPoolMinBytesMutex, m_UseMutex);

				if (heapFlags == D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES)
				{
					m_DefaultPoolHeapTypeMinBytes[HeapTypeToIndex(heapType)] = minBytes;
					newMinBytes = minBytes;
				}
				else
				{
					const UINT defaultPoolTier1Index = CalcDefaultPoolIndex(heapType, heapFlags, false);
					m_DefaultPoolTier1MinBytes[defaultPoolTier1Index] = minBytes;

					newMinBytes = m_DefaultPoolHeapTypeMinBytes[HeapTypeToIndex(heapType)];
					if (newMinBytes == UINT64_MAX)
					{
						newMinBytes = m_DefaultPoolTier1MinBytes[CalcDefaultPoolIndex(heapType, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, false)] +
							m_DefaultPoolTier1MinBytes[CalcDefaultPoolIndex(heapType, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, false)] +
							m_DefaultPoolTier1MinBytes[CalcDefaultPoolIndex(heapType, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, false)];
					}
				}
			}

			const UINT defaultPoolIndex = CalcDefaultPoolIndex(heapType, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES);
			return m_BlockVectors[defaultPoolIndex]->SetMinBytes(newMinBytes);
		}
		else
		{
			if (heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS &&
				heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES &&
				heapFlags != D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES)
			{
				D3D12MA_ASSERT(0 && "Allocator::SetDefaultHeapMinBytes: Invalid heapFlags passed.");
				return E_INVALIDARG;
			}

			const UINT defaultPoolIndex = CalcDefaultPoolIndex(heapType, heapFlags);
			return m_BlockVectors[defaultPoolIndex]->SetMinBytes(minBytes);
		}
	}

	bool AllocatorPimpl::PrefersCommittedAllocation(const D3D12_RESOURCE_DESC& resourceDesc)
	{
		// Intentional. It may change in the future.
		return false;
	}

	HRESULT AllocatorPimpl::AllocateCommittedResource(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_DESC* pResourceDesc,
		const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		Allocation** ppAllocation,
		REFIID riidResource,
		void** ppvResource)
	{
		if ((pAllocDesc->Flags & ALLOCATION_FLAG_NEVER_ALLOCATE) != 0)
		{
			return E_OUTOFMEMORY;
		}

		if ((pAllocDesc->Flags & ALLOCATION_FLAG_WITHIN_BUDGET) != 0)
		{
			Budget budget = {};
			GetBudgetForHeapType(budget, pAllocDesc->HeapType);
			if (budget.UsageBytes + resAllocInfo.SizeInBytes > budget.BudgetBytes)
			{
				return E_OUTOFMEMORY;
			}
		}

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = pAllocDesc->HeapType;
		// CONFFX_BEGIN - Multi GPU
		heapProps.CreationNodeMask = pAllocDesc->CreationNodeMask;
		heapProps.VisibleNodeMask = pAllocDesc->VisibleNodeMask;
		// CONFFX_END

		D3D12_HEAP_FLAGS heapFlags = pAllocDesc->ExtraHeapFlags;

		ID3D12Resource* res = NULL;
		HRESULT hr = m_Device->CreateCommittedResource(
			&heapProps, heapFlags, pResourceDesc, InitialResourceState,
			pOptimizedClearValue, riidResource, (void**)&res);
		if (SUCCEEDED(hr))
		{
			const BOOL wasZeroInitialized = TRUE;
			Allocation* alloc = m_AllocationObjectAllocator.Allocate(this, resAllocInfo.SizeInBytes, wasZeroInitialized);
			alloc->InitCommitted(pAllocDesc->HeapType);
			alloc->SetResource(res, pResourceDesc);

			*ppAllocation = alloc;
			if (ppvResource != NULL)
			{
				res->AddRef();
				*ppvResource = res;
			}

			RegisterCommittedAllocation(*ppAllocation, pAllocDesc->HeapType);

			const UINT heapTypeIndex = HeapTypeToIndex(pAllocDesc->HeapType);
			m_Budget.AddAllocation(heapTypeIndex, resAllocInfo.SizeInBytes);
			m_Budget.m_BlockBytes[heapTypeIndex] += resAllocInfo.SizeInBytes;
		}
		return hr;
	}

	HRESULT AllocatorPimpl::AllocateHeap(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_ALLOCATION_INFO& allocInfo,
		Allocation** ppAllocation)
	{
		*ppAllocation = nullptr;

		if ((pAllocDesc->Flags & ALLOCATION_FLAG_NEVER_ALLOCATE) != 0)
		{
			return E_OUTOFMEMORY;
		}

		if ((pAllocDesc->Flags & ALLOCATION_FLAG_WITHIN_BUDGET) != 0)
		{
			Budget budget = {};
			GetBudgetForHeapType(budget, pAllocDesc->HeapType);
			if (budget.UsageBytes + allocInfo.SizeInBytes > budget.BudgetBytes)
			{
				return E_OUTOFMEMORY;
			}
		}

		D3D12_HEAP_FLAGS heapFlags = pAllocDesc->ExtraHeapFlags;

		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = allocInfo.SizeInBytes;
		heapDesc.Properties.Type = pAllocDesc->HeapType;
		// CONFFX_BEGIN - Linked multi gpu
		heapDesc.Properties.CreationNodeMask = pAllocDesc->CreationNodeMask;
		heapDesc.Properties.VisibleNodeMask = pAllocDesc->VisibleNodeMask;
		// CONFFX_END
		heapDesc.Alignment = allocInfo.Alignment;
		heapDesc.Flags = heapFlags;

		ID3D12Heap* heap = nullptr;
		HRESULT hr = m_Device->CreateHeap(&heapDesc, __uuidof(*heap), (void**)&heap);
		if (SUCCEEDED(hr))
		{
			const BOOL wasZeroInitialized = TRUE;
			(*ppAllocation) = m_AllocationObjectAllocator.Allocate(this, allocInfo.SizeInBytes, wasZeroInitialized);
			(*ppAllocation)->InitHeap(pAllocDesc->HeapType, heap);
			RegisterCommittedAllocation(*ppAllocation, pAllocDesc->HeapType);

			const UINT heapTypeIndex = HeapTypeToIndex(pAllocDesc->HeapType);
			m_Budget.AddAllocation(heapTypeIndex, allocInfo.SizeInBytes);
			m_Budget.m_BlockBytes[heapTypeIndex] += allocInfo.SizeInBytes;
		}
		return hr;
	}

	UINT AllocatorPimpl::CalcDefaultPoolCount() const
	{
		if (SupportsResourceHeapTier2())
		{
			return 3;
		}
		else
		{
			return 9;
		}
	}

	UINT AllocatorPimpl::CalcDefaultPoolIndex(const ALLOCATION_DESC& allocDesc, const D3D12_RESOURCE_DESC& resourceDesc) const
	{
		const D3D12_HEAP_FLAGS extraHeapFlags = allocDesc.ExtraHeapFlags & ~GetExtraHeapFlagsToIgnore();
		if (extraHeapFlags != 0)
		{
			return UINT32_MAX;
		}

		UINT poolIndex = UINT_MAX;
		switch (allocDesc.HeapType)
		{
		case D3D12_HEAP_TYPE_DEFAULT:  poolIndex = 0; break;
		case D3D12_HEAP_TYPE_UPLOAD:   poolIndex = 1; break;
		case D3D12_HEAP_TYPE_READBACK: poolIndex = 2; break;
		default: D3D12MA_ASSERT(0);
		}

		if (!SupportsResourceHeapTier2())
		{
			poolIndex *= 3;
			if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				++poolIndex;
				const bool isRenderTargetOrDepthStencil =
					(resourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0;
				if (isRenderTargetOrDepthStencil)
				{
					++poolIndex;
				}
			}
		}

		return poolIndex;
	}

	UINT AllocatorPimpl::CalcDefaultPoolIndex(D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags, bool supportsResourceHeapTier2)
	{
		const D3D12_HEAP_FLAGS extraHeapFlags = heapFlags & ~GetExtraHeapFlagsToIgnore();
		if (extraHeapFlags != 0)
		{
			return UINT32_MAX;
		}

		UINT poolIndex = UINT_MAX;
		switch (heapType)
		{
		case D3D12_HEAP_TYPE_DEFAULT:  poolIndex = 0; break;
		case D3D12_HEAP_TYPE_UPLOAD:   poolIndex = 1; break;
		case D3D12_HEAP_TYPE_READBACK: poolIndex = 2; break;
		default: D3D12MA_ASSERT(0);
		}

		if (!supportsResourceHeapTier2)
		{
			poolIndex *= 3;

			const bool allowBuffers = (heapFlags & D3D12_HEAP_FLAG_DENY_BUFFERS) == 0;
			const bool allowRtDsTextures = (heapFlags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) == 0;
			const bool allowNonRtDsTextures = (heapFlags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES) == 0;

			const uint8_t allowedGroupCount = (allowBuffers ? 1 : 0) + (allowRtDsTextures ? 1 : 0) + (allowNonRtDsTextures ? 1 : 0);
			if (allowedGroupCount != 1)
			{
				return UINT32_MAX;
			}

			if (!allowBuffers)
			{
				++poolIndex;
				if (allowRtDsTextures)
				{
					++poolIndex;
				}
			}
		}

		return poolIndex;
	}

	void AllocatorPimpl::CalcDefaultPoolParams(D3D12_HEAP_TYPE& outHeapType, D3D12_HEAP_FLAGS& outHeapFlags, UINT index) const
	{
		outHeapType = D3D12_HEAP_TYPE_DEFAULT;
		outHeapFlags = D3D12_HEAP_FLAG_NONE;

		if (!SupportsResourceHeapTier2())
		{
			switch (index % 3)
			{
			case 0:
				outHeapFlags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
				break;
			case 1:
				outHeapFlags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
				break;
			case 2:
				outHeapFlags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
				break;
			}

			index /= 3;
		}

		switch (index)
		{
		case 0:
			outHeapType = D3D12_HEAP_TYPE_DEFAULT;
			break;
		case 1:
			outHeapType = D3D12_HEAP_TYPE_UPLOAD;
			break;
		case 2:
			outHeapType = D3D12_HEAP_TYPE_READBACK;
			break;
		default:
			D3D12MA_ASSERT(0);
		}
	}

	void AllocatorPimpl::RegisterCommittedAllocation(Allocation* alloc, D3D12_HEAP_TYPE heapType)
	{
		const UINT heapTypeIndex = HeapTypeToIndex(heapType);

		MutexLockWrite lock(m_CommittedAllocationsMutex[heapTypeIndex], m_UseMutex);
		AllocationVectorType* const committedAllocations = m_pCommittedAllocations[heapTypeIndex];
		D3D12MA_ASSERT(committedAllocations);
		committedAllocations->InsertSorted(alloc, PointerLess());
	}

	void AllocatorPimpl::UnregisterCommittedAllocation(Allocation* alloc, D3D12_HEAP_TYPE heapType)
	{
		const UINT heapTypeIndex = HeapTypeToIndex(heapType);

		MutexLockWrite lock(m_CommittedAllocationsMutex[heapTypeIndex], m_UseMutex);
		AllocationVectorType* const committedAllocations = m_pCommittedAllocations[heapTypeIndex];
		D3D12MA_ASSERT(committedAllocations);
		bool success = committedAllocations->RemoveSorted(alloc, PointerLess());
		D3D12MA_ASSERT(success);
	}

	void AllocatorPimpl::RegisterPool(Pool* pool, D3D12_HEAP_TYPE heapType)
	{
		const UINT heapTypeIndex = HeapTypeToIndex(heapType);

		MutexLockWrite lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
		PoolVectorType* const pools = m_pPools[heapTypeIndex];
		D3D12MA_ASSERT(pools);
		pools->InsertSorted(pool, PointerLess());
	}

	void AllocatorPimpl::UnregisterPool(Pool* pool, D3D12_HEAP_TYPE heapType)
	{
		const UINT heapTypeIndex = HeapTypeToIndex(heapType);

		MutexLockWrite lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
		PoolVectorType* const pools = m_pPools[heapTypeIndex];
		D3D12MA_ASSERT(pools);
		bool success = pools->RemoveSorted(pool, PointerLess());
		D3D12MA_ASSERT(success);
	}

	void AllocatorPimpl::FreeCommittedMemory(Allocation* allocation)
	{
		D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_COMMITTED);
		UnregisterCommittedAllocation(allocation, allocation->m_Committed.heapType);

		const UINT64 allocationSize = allocation->GetSize();
		const UINT heapTypeIndex = HeapTypeToIndex(allocation->m_Committed.heapType);
		m_Budget.RemoveAllocation(heapTypeIndex, allocationSize);
		m_Budget.m_BlockBytes[heapTypeIndex] -= allocationSize;
	}

	void AllocatorPimpl::FreePlacedMemory(Allocation* allocation)
	{
		D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_PLACED);

		NormalBlock* const block = allocation->m_Placed.block;
		D3D12MA_ASSERT(block);
		BlockVector* const blockVector = block->GetBlockVector();
		D3D12MA_ASSERT(blockVector);
		m_Budget.RemoveAllocation(HeapTypeToIndex(block->GetHeapType()), allocation->GetSize());
		blockVector->Free(allocation);
	}

	void AllocatorPimpl::FreeHeapMemory(Allocation* allocation)
	{
		D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_HEAP);
		UnregisterCommittedAllocation(allocation, allocation->m_Heap.heapType);
		SAFE_RELEASE(allocation->m_Heap.heap);

		const UINT heapTypeIndex = HeapTypeToIndex(allocation->m_Heap.heapType);
		const UINT64 allocationSize = allocation->GetSize();
		m_Budget.m_BlockBytes[heapTypeIndex] -= allocationSize;
		m_Budget.RemoveAllocation(heapTypeIndex, allocationSize);
	}

	void AllocatorPimpl::SetCurrentFrameIndex(UINT frameIndex)
	{
		m_CurrentFrameIndex.store(frameIndex);

#if D3D12MA_DXGI_1_4
		if (m_Adapter3)
		{
			UpdateD3D12Budget();
		}
#endif
	}

	void AllocatorPimpl::CalculateStats(Stats& outStats)
	{
		// Init stats
		ZeroMemory(&outStats, sizeof(outStats));
		outStats.Total.AllocationSizeMin = UINT64_MAX;
		outStats.Total.UnusedRangeSizeMin = UINT64_MAX;
		for (size_t i = 0; i < HEAP_TYPE_COUNT; i++)
		{
			outStats.HeapType[i].AllocationSizeMin = UINT64_MAX;
			outStats.HeapType[i].UnusedRangeSizeMin = UINT64_MAX;
		}

		// Process deafult pools.
		for (size_t i = 0; i < HEAP_TYPE_COUNT; ++i)
		{
			BlockVector* const pBlockVector = m_BlockVectors[i];
			D3D12MA_ASSERT(pBlockVector);
			pBlockVector->AddStats(outStats);
		}

		// Process custom pools
		for (size_t heapTypeIndex = 0; heapTypeIndex < HEAP_TYPE_COUNT; ++heapTypeIndex)
		{
			MutexLockRead lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
			const PoolVectorType* const poolVector = m_pPools[heapTypeIndex];
			D3D12MA_ASSERT(poolVector);
			for (size_t poolIndex = 0, count = poolVector->size(); poolIndex < count; ++poolIndex)
			{
				Pool* pool = (*poolVector)[poolIndex];
				pool->m_Pimpl->GetBlockVector()->AddStats(outStats);
			}
		}

		// Process committed allocations.
		for (size_t heapTypeIndex = 0; heapTypeIndex < HEAP_TYPE_COUNT; ++heapTypeIndex)
		{
			StatInfo& heapStatInfo = outStats.HeapType[heapTypeIndex];
			MutexLockRead lock(m_CommittedAllocationsMutex[heapTypeIndex], m_UseMutex);
			const AllocationVectorType* const allocationVector = m_pCommittedAllocations[heapTypeIndex];
			D3D12MA_ASSERT(allocationVector);
			for (size_t allocIndex = 0, count = allocationVector->size(); allocIndex < count; ++allocIndex)
			{
				UINT64 size = (*allocationVector)[allocIndex]->GetSize();
				StatInfo statInfo = {};
				statInfo.BlockCount = 1;
				statInfo.AllocationCount = 1;
				statInfo.UnusedRangeCount = 0;
				statInfo.UsedBytes = size;
				statInfo.UnusedBytes = 0;
				statInfo.AllocationSizeMin = size;
				statInfo.AllocationSizeMax = size;
				statInfo.UnusedRangeSizeMin = UINT64_MAX;
				statInfo.UnusedRangeSizeMax = 0;
				AddStatInfo(outStats.Total, statInfo);
				AddStatInfo(heapStatInfo, statInfo);
			}
		}

		// Post process
		PostProcessStatInfo(outStats.Total);
		for (size_t i = 0; i < HEAP_TYPE_COUNT; ++i)
			PostProcessStatInfo(outStats.HeapType[i]);
	}

	void AllocatorPimpl::GetBudget(Budget* outGpuBudget, Budget* outCpuBudget)
	{
		if (outGpuBudget)
		{
			// Taking DEFAULT.
			outGpuBudget->BlockBytes = m_Budget.m_BlockBytes[0];
			outGpuBudget->AllocationBytes = m_Budget.m_AllocationBytes[0];
		}
		if (outCpuBudget)
		{
			// Taking UPLOAD + READBACK.
			outCpuBudget->BlockBytes = m_Budget.m_BlockBytes[1] + m_Budget.m_BlockBytes[2];
			outCpuBudget->AllocationBytes = m_Budget.m_AllocationBytes[1] + m_Budget.m_AllocationBytes[2];
		}

#if D3D12MA_DXGI_1_4
		if (m_Adapter3)
		{
			if (m_Budget.m_OperationsSinceBudgetFetch < 30)
			{
				MutexLockRead lockRead(m_Budget.m_BudgetMutex, m_UseMutex);
				if (outGpuBudget)
				{

					if (m_Budget.m_D3D12UsageLocal + outGpuBudget->BlockBytes > m_Budget.m_BlockBytesAtBudgetFetch[0])
					{
						outGpuBudget->UsageBytes = m_Budget.m_D3D12UsageLocal +
							outGpuBudget->BlockBytes - m_Budget.m_BlockBytesAtBudgetFetch[0];
					}
					else
					{
						outGpuBudget->UsageBytes = 0;
					}
					outGpuBudget->BudgetBytes = m_Budget.m_D3D12BudgetLocal;
				}
				if (outCpuBudget)
				{
					if (m_Budget.m_D3D12UsageNonLocal + outCpuBudget->BlockBytes > m_Budget.m_BlockBytesAtBudgetFetch[1] + m_Budget.m_BlockBytesAtBudgetFetch[2])
					{
						outCpuBudget->UsageBytes = m_Budget.m_D3D12UsageNonLocal +
							outCpuBudget->BlockBytes - (m_Budget.m_BlockBytesAtBudgetFetch[1] + m_Budget.m_BlockBytesAtBudgetFetch[2]);
					}
					else
					{
						outCpuBudget->UsageBytes = 0;
					}
					outCpuBudget->BudgetBytes = m_Budget.m_D3D12BudgetNonLocal;
				}
			}
			else
			{
				UpdateD3D12Budget(); // Outside of mutex lock
				GetBudget(outGpuBudget, outCpuBudget); // Recursion
			}
		}
		else
#endif
		{
			if (outGpuBudget)
			{
				const UINT64 gpuMemorySize = m_AdapterDesc.DedicatedVideoMemory + m_AdapterDesc.DedicatedSystemMemory; // TODO: Is this right?
				outGpuBudget->UsageBytes = outGpuBudget->BlockBytes;
				outGpuBudget->BudgetBytes = gpuMemorySize * 8 / 10; // 80% heuristics.
			}
			if (outCpuBudget)
			{
				const UINT64 cpuMemorySize = m_AdapterDesc.SharedSystemMemory; // TODO: Is this right?
				outCpuBudget->UsageBytes = outCpuBudget->BlockBytes;
				outCpuBudget->BudgetBytes = cpuMemorySize * 8 / 10; // 80% heuristics.
			}
		}
	}

	void AllocatorPimpl::GetBudgetForHeapType(Budget& outBudget, D3D12_HEAP_TYPE heapType)
	{
		switch (heapType)
		{
		case D3D12_HEAP_TYPE_DEFAULT:
			GetBudget(&outBudget, NULL);
			break;
		case D3D12_HEAP_TYPE_UPLOAD:
		case D3D12_HEAP_TYPE_READBACK:
			GetBudget(NULL, &outBudget);
			break;
		default: D3D12MA_ASSERT(0);
		}
	}

	static void AddStatInfoToJson(JsonWriter& json, const StatInfo& statInfo)
	{
		json.BeginObject();
		json.WriteString(L"Blocks");
		json.WriteNumber(statInfo.BlockCount);
		json.WriteString(L"Allocations");
		json.WriteNumber(statInfo.AllocationCount);
		json.WriteString(L"UnusedRanges");
		json.WriteNumber(statInfo.UnusedRangeCount);
		json.WriteString(L"UsedBytes");
		json.WriteNumber(statInfo.UsedBytes);
		json.WriteString(L"UnusedBytes");
		json.WriteNumber(statInfo.UnusedBytes);

		json.WriteString(L"AllocationSize");
		json.BeginObject(true);
		json.WriteString(L"Min");
		json.WriteNumber(statInfo.AllocationSizeMin);
		json.WriteString(L"Avg");
		json.WriteNumber(statInfo.AllocationSizeAvg);
		json.WriteString(L"Max");
		json.WriteNumber(statInfo.AllocationSizeMax);
		json.EndObject();

		json.WriteString(L"UnusedRangeSize");
		json.BeginObject(true);
		json.WriteString(L"Min");
		json.WriteNumber(statInfo.UnusedRangeSizeMin);
		json.WriteString(L"Avg");
		json.WriteNumber(statInfo.UnusedRangeSizeAvg);
		json.WriteString(L"Max");
		json.WriteNumber(statInfo.UnusedRangeSizeMax);
		json.EndObject();

		json.EndObject();
	}

	void AllocatorPimpl::BuildStatsString(WCHAR** ppStatsString, BOOL DetailedMap)
	{
		StringBuilder sb(GetAllocs());
		{
			JsonWriter json(GetAllocs(), sb);

			Budget gpuBudget = {}, cpuBudget = {};
			GetBudget(&gpuBudget, &cpuBudget);

			Stats stats;
			CalculateStats(stats);

			json.BeginObject();

			json.WriteString(L"Total");
			AddStatInfoToJson(json, stats.Total);
			for (size_t heapType = 0; heapType < HEAP_TYPE_COUNT; ++heapType)
			{
				json.WriteString(HeapTypeNames[heapType]);
				AddStatInfoToJson(json, stats.HeapType[heapType]);
			}

			json.WriteString(L"Budget");
			json.BeginObject();
			{
				json.WriteString(L"GPU");
				WriteBudgetToJson(json, gpuBudget);
				json.WriteString(L"CPU");
				WriteBudgetToJson(json, cpuBudget);
			}
			json.EndObject();

			if (DetailedMap)
			{
				json.WriteString(L"DetailedMap");
				json.BeginObject();

				json.WriteString(L"DefaultPools");
				json.BeginObject();

				if (SupportsResourceHeapTier2())
				{
					for (size_t heapType = 0; heapType < HEAP_TYPE_COUNT; ++heapType)
					{
						json.WriteString(HeapTypeNames[heapType]);
						json.BeginObject();

						json.WriteString(L"Blocks");

						BlockVector* blockVector = m_BlockVectors[heapType];
						D3D12MA_ASSERT(blockVector);
						blockVector->WriteBlockInfoToJson(json);

						json.EndObject(); // heap name
					}
				}
				else
				{
					for (size_t heapType = 0; heapType < HEAP_TYPE_COUNT; ++heapType)
					{
						for (size_t heapSubType = 0; heapSubType < 3; ++heapSubType)
						{
							static const WCHAR* const heapSubTypeName[] = {
								L" + buffer",
								L" + texture",
								L" + texture RT or DS",
							};
							json.BeginString();
							json.ContinueString(HeapTypeNames[heapType]);
							json.ContinueString(heapSubTypeName[heapSubType]);
							json.EndString();
							json.BeginObject();

							json.WriteString(L"Blocks");

							BlockVector* blockVector = m_BlockVectors[heapType * 3 + heapSubType];
							D3D12MA_ASSERT(blockVector);
							blockVector->WriteBlockInfoToJson(json);

							json.EndObject(); // heap name
						}
					}
				}

				json.EndObject(); // DefaultPools

				json.WriteString(L"CommittedAllocations");
				json.BeginObject();

				for (size_t heapType = 0; heapType < HEAP_TYPE_COUNT; ++heapType)
				{
					json.WriteString(HeapTypeNames[heapType]);
					MutexLockRead lock(m_CommittedAllocationsMutex[heapType], m_UseMutex);

					json.BeginArray();
					const AllocationVectorType* const allocationVector = m_pCommittedAllocations[heapType];
					D3D12MA_ASSERT(allocationVector);
					for (size_t i = 0, count = allocationVector->size(); i < count; ++i)
					{
						Allocation* alloc = (*allocationVector)[i];
						D3D12MA_ASSERT(alloc);

						json.BeginObject(true);
						json.AddAllocationToObject(*alloc);
						json.EndObject();
					}
					json.EndArray();
				}

				json.EndObject(); // CommittedAllocations

				json.EndObject(); // DetailedMap
			}
			json.EndObject();
		}

		const size_t length = sb.GetLength();
		WCHAR* result = AllocateArray<WCHAR>(GetAllocs(), length + 1);
		memcpy(result, sb.GetData(), length * sizeof(WCHAR));
		result[length] = L'\0';
		*ppStatsString = result;
	}

	void AllocatorPimpl::FreeStatsString(WCHAR* pStatsString)
	{
		D3D12MA_ASSERT(pStatsString);
		Free(GetAllocs(), pStatsString);
	}

	HRESULT AllocatorPimpl::UpdateD3D12Budget()
	{
#if D3D12MA_DXGI_1_4
		D3D12MA_ASSERT(m_Adapter3);

		DXGI_QUERY_VIDEO_MEMORY_INFO infoLocal = {};
		DXGI_QUERY_VIDEO_MEMORY_INFO infoNonLocal = {};
		HRESULT hrLocal = m_Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &infoLocal);
		HRESULT hrNonLocal = m_Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &infoNonLocal);

		{
			MutexLockWrite lockWrite(m_Budget.m_BudgetMutex, m_UseMutex);

			if (SUCCEEDED(hrLocal))
			{
				m_Budget.m_D3D12UsageLocal = infoLocal.CurrentUsage;
				m_Budget.m_D3D12BudgetLocal = infoLocal.Budget;
			}
			if (SUCCEEDED(hrNonLocal))
			{
				m_Budget.m_D3D12UsageNonLocal = infoNonLocal.CurrentUsage;
				m_Budget.m_D3D12BudgetNonLocal = infoNonLocal.Budget;
			}

			for (UINT i = 0; i < HEAP_TYPE_COUNT; ++i)
			{
				m_Budget.m_BlockBytesAtBudgetFetch[i] = m_Budget.m_BlockBytes[i].load();
			}

			m_Budget.m_OperationsSinceBudgetFetch = 0;
		}

		return FAILED(hrLocal) ? hrLocal : hrNonLocal;
#else
		return S_OK;
#endif
	}

	D3D12_RESOURCE_ALLOCATION_INFO AllocatorPimpl::GetResourceAllocationInfo(D3D12_RESOURCE_DESC& inOutResourceDesc) const
	{
#if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
		if (inOutResourceDesc.Alignment == 0 &&
			inOutResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
			(inOutResourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == 0
#if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT == 1
			&& CanUseSmallAlignment(inOutResourceDesc)
#endif
			)
		{
			/*
			The algorithm here is based on Microsoft sample: "Small Resources Sample"
			https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12SmallResources
			*/
			const UINT64 smallAlignmentToTry = inOutResourceDesc.SampleDesc.Count > 1 ?
				D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT :
				D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
			inOutResourceDesc.Alignment = smallAlignmentToTry;
			const D3D12_RESOURCE_ALLOCATION_INFO smallAllocInfo = m_Device->GetResourceAllocationInfo(0, 1, &inOutResourceDesc);
			// Check if alignment requested has been granted.
			if (smallAllocInfo.Alignment == smallAlignmentToTry)
			{
				return smallAllocInfo;
			}
			inOutResourceDesc.Alignment = 0; // Restore original
		}
#endif // #if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT

		return m_Device->GetResourceAllocationInfo(0, 1, &inOutResourceDesc);
	}

	void AllocatorPimpl::WriteBudgetToJson(JsonWriter& json, const Budget& budget)
	{
		json.BeginObject();
		{
			json.WriteString(L"BlockBytes");
			json.WriteNumber(budget.BlockBytes);
			json.WriteString(L"AllocationBytes");
			json.WriteNumber(budget.AllocationBytes);
			json.WriteString(L"UsageBytes");
			json.WriteNumber(budget.UsageBytes);
			json.WriteString(L"BudgetBytes");
			json.WriteNumber(budget.BudgetBytes);
		}
		json.EndObject();
	}

	////////////////////////////////////////////////////////////////////////////////
	// Public class Allocation implementation

	void Allocation::PackedData::SetType(Type type)
	{
		const UINT u = (UINT)type;
		D3D12MA_ASSERT(u < (1u << 2));
		m_Type = u;
	}

	void Allocation::PackedData::SetResourceDimension(D3D12_RESOURCE_DIMENSION resourceDimension)
	{
		const UINT u = (UINT)resourceDimension;
		D3D12MA_ASSERT(u < (1u << 3));
		m_ResourceDimension = u;
	}

	void Allocation::PackedData::SetResourceFlags(D3D12_RESOURCE_FLAGS resourceFlags)
	{
		const UINT u = (UINT)resourceFlags;
		D3D12MA_ASSERT(u < (1u << 24));
		m_ResourceFlags = u;
	}

	void Allocation::PackedData::SetTextureLayout(D3D12_TEXTURE_LAYOUT textureLayout)
	{
		const UINT u = (UINT)textureLayout;
		D3D12MA_ASSERT(u < (1u << 9));
		m_TextureLayout = u;
	}

	void Allocation::Release()
	{
		if (this == NULL)
		{
			return;
		}

		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

			SAFE_RELEASE(m_Resource);

		switch (m_PackedData.GetType())
		{
		case TYPE_COMMITTED:
			m_Allocator->FreeCommittedMemory(this);
			break;
		case TYPE_PLACED:
			m_Allocator->FreePlacedMemory(this);
			break;
		case TYPE_HEAP:
			m_Allocator->FreeHeapMemory(this);
			break;
		}

		FreeName();

		m_Allocator->GetAllocationObjectAllocator().Free(this);
	}

	UINT64 Allocation::GetOffset() const
	{
		switch (m_PackedData.GetType())
		{
		case TYPE_COMMITTED:
		case TYPE_HEAP:
			return 0;
		case TYPE_PLACED:
			return m_Placed.offset;
		default:
			D3D12MA_ASSERT(0);
			return 0;
		}
	}

	ID3D12Heap* Allocation::GetHeap() const
	{
		switch (m_PackedData.GetType())
		{
		case TYPE_COMMITTED:
			return NULL;
		case TYPE_PLACED:
			return m_Placed.block->GetHeap();
		case TYPE_HEAP:
			return m_Heap.heap;
		default:
			D3D12MA_ASSERT(0);
			return 0;
		}
	}

	void Allocation::SetName(LPCWSTR Name)
	{
		FreeName();

		if (Name)
		{
			const size_t nameCharCount = wcslen(Name) + 1;
			m_Name = D3D12MA_NEW_ARRAY(m_Allocator->GetAllocs(), WCHAR, nameCharCount);
			memcpy(m_Name, Name, nameCharCount * sizeof(WCHAR));
		}
	}

	Allocation::Allocation(AllocatorPimpl* allocator, UINT64 size, BOOL wasZeroInitialized) :
		m_Allocator{ allocator },
		m_Size{ size },
		m_Resource{ NULL },
		m_CreationFrameIndex{ allocator->GetCurrentFrameIndex() },
		m_Name{ NULL }
	{
		D3D12MA_ASSERT(allocator);

		m_PackedData.SetType(TYPE_COUNT);
		m_PackedData.SetResourceDimension(D3D12_RESOURCE_DIMENSION_UNKNOWN);
		m_PackedData.SetResourceFlags(D3D12_RESOURCE_FLAG_NONE);
		m_PackedData.SetTextureLayout(D3D12_TEXTURE_LAYOUT_UNKNOWN);
		m_PackedData.SetWasZeroInitialized(wasZeroInitialized);
	}

	Allocation::~Allocation()
	{
		// Nothing here, everything already done in Release.
	}

	void Allocation::InitCommitted(D3D12_HEAP_TYPE heapType)
	{
		m_PackedData.SetType(TYPE_COMMITTED);
		m_Committed.heapType = heapType;
	}

	void Allocation::InitPlaced(UINT64 offset, UINT64 alignment, NormalBlock* block)
	{
		m_PackedData.SetType(TYPE_PLACED);
		m_Placed.offset = offset;
		m_Placed.block = block;
	}

	void Allocation::InitHeap(D3D12_HEAP_TYPE heapType, ID3D12Heap* heap)
	{
		m_PackedData.SetType(TYPE_HEAP);
		m_Heap.heapType = heapType;
		m_Heap.heap = heap;
	}

	void Allocation::SetResource(ID3D12Resource* resource, const D3D12_RESOURCE_DESC* pResourceDesc)
	{
		D3D12MA_ASSERT(m_Resource == NULL);
		D3D12MA_ASSERT(pResourceDesc);
		m_Resource = resource;
		m_PackedData.SetResourceDimension(pResourceDesc->Dimension);
		m_PackedData.SetResourceFlags(pResourceDesc->Flags);
		m_PackedData.SetTextureLayout(pResourceDesc->Layout);
	}

	void Allocation::FreeName()
	{
		if (m_Name)
		{
			const size_t nameCharCount = wcslen(m_Name) + 1;
			D3D12MA_DELETE_ARRAY(m_Allocator->GetAllocs(), m_Name, nameCharCount);
			m_Name = NULL;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Private class AllocationObjectAllocator implementation

	AllocationObjectAllocator::AllocationObjectAllocator(const ALLOCATION_CALLBACKS& allocationCallbacks) :
		m_Allocator(allocationCallbacks, 1024)
	{
	}

	template<typename... Types> Allocation* AllocationObjectAllocator::Allocate(Types... args)
	{
		MutexLock mutexLock(m_Mutex);
		return m_Allocator.Alloc(std::forward<Types>(args)...);
	}

	void AllocationObjectAllocator::Free(Allocation* alloc)
	{
		MutexLock mutexLock(m_Mutex);
		m_Allocator.Free(alloc);
	}

	////////////////////////////////////////////////////////////////////////////////
	// Public class Allocator implementation

	Allocator::Allocator(const ALLOCATION_CALLBACKS& allocationCallbacks, const ALLOCATOR_DESC& desc) :
		m_Pimpl(D3D12MA_NEW(allocationCallbacks, AllocatorPimpl)(allocationCallbacks, desc))
	{
	}

	Allocator::~Allocator()
	{
		D3D12MA_DELETE(m_Pimpl->GetAllocs(), m_Pimpl);
	}

	void Allocator::Release()
	{
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

			// Copy is needed because otherwise we would call destructor and invalidate the structure with callbacks before using it to free memory.
			const ALLOCATION_CALLBACKS allocationCallbacksCopy = m_Pimpl->GetAllocs();
		D3D12MA_DELETE(allocationCallbacksCopy, this);
	}



	const D3D12_FEATURE_DATA_D3D12_OPTIONS& Allocator::GetD3D12Options() const
	{
		return m_Pimpl->GetD3D12Options();
	}

	HRESULT Allocator::CreateResource(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_DESC* pResourceDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		Allocation** ppAllocation,
		REFIID riidResource,
		void** ppvResource)
	{
		if (!pAllocDesc || !pResourceDesc || !ppAllocation || riidResource == IID_NULL)
		{
			D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreateResource.");
			return E_INVALIDARG;
		}
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			return m_Pimpl->CreateResource(pAllocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue, ppAllocation, riidResource, ppvResource);
	}

	HRESULT Allocator::AllocateMemory(
		const ALLOCATION_DESC* pAllocDesc,
		const D3D12_RESOURCE_ALLOCATION_INFO* pAllocInfo,
		Allocation** ppAllocation)
	{
		if (!pAllocDesc ||
			!pAllocInfo ||
			!ppAllocation ||
			!(pAllocInfo->Alignment == 0 ||
				pAllocInfo->Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
				pAllocInfo->Alignment == D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) ||
			pAllocInfo->SizeInBytes == 0 ||
			pAllocInfo->SizeInBytes % (64ull * 1024) != 0)
		{
			D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::AllocateMemory.");
			return E_INVALIDARG;
		}
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			return m_Pimpl->AllocateMemory(pAllocDesc, pAllocInfo, ppAllocation);
	}

	HRESULT Allocator::CreateAliasingResource(
		Allocation* pAllocation,
		UINT64 AllocationLocalOffset,
		const D3D12_RESOURCE_DESC* pResourceDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE *pOptimizedClearValue,
		REFIID riidResource,
		void** ppvResource)
	{
		if (!pAllocation || !pResourceDesc || riidResource == IID_NULL || !ppvResource)
		{
			D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreateAliasingResource.");
			return E_INVALIDARG;
		}
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			return m_Pimpl->CreateAliasingResource(pAllocation, AllocationLocalOffset, pResourceDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
	}

	HRESULT Allocator::CreatePool(
		const POOL_DESC* pPoolDesc,
		Pool** ppPool)
	{
		if (!pPoolDesc || !ppPool ||
			!IsHeapTypeValid(pPoolDesc->HeapType) ||
			(pPoolDesc->MaxBlockCount > 0 && pPoolDesc->MaxBlockCount < pPoolDesc->MinBlockCount))
		{
			D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreatePool.");
			return E_INVALIDARG;
		}
		if (!m_Pimpl->HeapFlagsFulfillResourceHeapTier(pPoolDesc->HeapFlags))
		{
			D3D12MA_ASSERT(0 && "Invalid pPoolDesc->HeapFlags passed to Allocator::CreatePool. Did you forget to handle ResourceHeapTier=1?");
			return E_INVALIDARG;
		}
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			*ppPool = D3D12MA_NEW(m_Pimpl->GetAllocs(), Pool)(this, *pPoolDesc);
		HRESULT hr = (*ppPool)->m_Pimpl->Init();
		if (SUCCEEDED(hr))
		{
			m_Pimpl->RegisterPool(*ppPool, pPoolDesc->HeapType);
		}
		else
		{
			D3D12MA_DELETE(m_Pimpl->GetAllocs(), *ppPool);
			*ppPool = NULL;
		}
		return hr;
	}

	HRESULT Allocator::SetDefaultHeapMinBytes(
		D3D12_HEAP_TYPE heapType,
		D3D12_HEAP_FLAGS heapFlags,
		UINT64 minBytes)
	{
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			return m_Pimpl->SetDefaultHeapMinBytes(heapType, heapFlags, minBytes);
	}

	void Allocator::SetCurrentFrameIndex(UINT frameIndex)
	{
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->SetCurrentFrameIndex(frameIndex);
	}

	void Allocator::CalculateStats(Stats* pStats)
	{
		D3D12MA_ASSERT(pStats);
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->CalculateStats(*pStats);
	}

	void Allocator::GetBudget(Budget* pGpuBudget, Budget* pCpuBudget)
	{
		if (pGpuBudget == NULL && pCpuBudget == NULL)
		{
			return;
		}
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->GetBudget(pGpuBudget, pCpuBudget);
	}

	void Allocator::BuildStatsString(WCHAR** ppStatsString, BOOL DetailedMap)
	{
		D3D12MA_ASSERT(ppStatsString);
		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
			m_Pimpl->BuildStatsString(ppStatsString, DetailedMap);
	}

	void Allocator::FreeStatsString(WCHAR* pStatsString)
	{
		if (pStatsString != NULL)
		{
			D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
				m_Pimpl->FreeStatsString(pStatsString);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Public global functions

	HRESULT CreateAllocator(const ALLOCATOR_DESC* pDesc, Allocator** ppAllocator)
	{
		if (!pDesc || !ppAllocator || !pDesc->pDevice || !pDesc->pAdapter ||
			!(pDesc->PreferredBlockSize == 0 || (pDesc->PreferredBlockSize >= 16 && pDesc->PreferredBlockSize < 0x10000000000ull)))
		{
			D3D12MA_ASSERT(0 && "Invalid arguments passed to CreateAllocator.");
			return E_INVALIDARG;
		}

		D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

			ALLOCATION_CALLBACKS allocationCallbacks;
		SetupAllocationCallbacks(allocationCallbacks, *pDesc);

		*ppAllocator = D3D12MA_NEW(allocationCallbacks, Allocator)(allocationCallbacks, *pDesc);
		HRESULT hr = (*ppAllocator)->m_Pimpl->Init(*pDesc);
		if (FAILED(hr))
		{
			D3D12MA_DELETE(allocationCallbacks, *ppAllocator);
			*ppAllocator = NULL;
		}
		return hr;
	}

} // namespace D3D12MA
#endif
