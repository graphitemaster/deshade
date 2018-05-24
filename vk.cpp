#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>

#include <vulkan/vk_layer.h>

#include "log.h"
#include "hash.h"

template<typename T>
void* DispatchKey(T instance)
{
	return *(void **)instance;
}

struct ContextVK
{
	std::mutex mutex_;
	std::unordered_map<void*, VkLayerInstanceDispatchTable> instance_dispatch_;
	std::unordered_map<void*, VkLayerDispatchTable> device_dispatch_;
};

static ContextVK& GetContext()
{
	static ContextVK context_;
	return context_;
}

// utilities to figure out shader type from SPIR-V bytecode
enum class ExecutionModel
{
	Unknown,
	Vertex,
	TessellationControl,
	TessellationEvaluation,
	Geometry,
	Fragment,
	Compute,
	Kernel
};

static ExecutionModel GetExecutionModel(const uint32_t* code, const uint32_t *end)
{
	// search for OpEntryPoint=15
	// OpEntryPoint+1 is ExecutionModel
	if (code[0] == 0x07230203)
	{
		code++; // skip magic
		code++; // skip version #
		code++; // skip generators magic #
		code++; // skip bound
		code++; // skip reserved

		while (code < end)
		{
			uint32_t token = *code;
			uint16_t opcode = token & 0x0000FFFF;
			uint16_t length = (uint16_t)((token & 0xFFFF0000)) >> 16;

			if (length == 0)
			{
				length = 1;
			}

			if (opcode == 15)
			{
				// next word is the execution model
				switch (code[1])
				{
				case 0: return ExecutionModel::Vertex;
				case 1: return ExecutionModel::TessellationControl;
				case 2: return ExecutionModel::TessellationEvaluation;
				case 3: return ExecutionModel::Geometry;
				case 4: return ExecutionModel::Fragment;
				case 5: return ExecutionModel::Kernel;
				}
			}

			code += length;
		}
	}

	return ExecutionModel::Unknown;
}

static const char* GetShaderTypeString(ExecutionModel model)
{
	switch (model)
	{
	case ExecutionModel::Vertex:
		return "vertex";
	case ExecutionModel::TessellationControl:
		return "tessellation control";
	case ExecutionModel::TessellationEvaluation:
		return "tessellation evaluation";
	case ExecutionModel::Geometry:
		return "geometry";
	case ExecutionModel::Fragment:
		return "fragment";
	case ExecutionModel::Compute:
		return "compute";
	case ExecutionModel::Kernel:
		return "kernel";
	case ExecutionModel::Unknown:
		break;
	}
	return "unknown";
}

static std::string GetShaderExtensionString(ExecutionModel model)
{
	std::string result;
	switch (model)
	{
	case ExecutionModel::Vertex:
		result += "_vs.bin";
		break;
	case ExecutionModel::TessellationControl:
		result += "_tcs.bin";
		break;
	case ExecutionModel::TessellationEvaluation:
		result += "_tes.bin";
		break;
	case ExecutionModel::Geometry:
		result += "_gs.bin";
		break;
	case ExecutionModel::Fragment:
		result += "_fs.bin";
		break;
	case ExecutionModel::Compute:
		result += "_cs.bin";
		break;
	case ExecutionModel::Kernel:
		result += "_ks.bin";
		break;
	case ExecutionModel::Unknown:
		break;
	}
	return result;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkCreateInstance(
	const VkInstanceCreateInfo* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkInstance* pInstance)
{
	VkLayerInstanceCreateInfo* pLayerCreateInfo =
		(VkLayerInstanceCreateInfo*)pCreateInfo->pNext;

	// step through the chain
	while (pLayerCreateInfo && (pLayerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
	                            pLayerCreateInfo->function != VK_LAYER_LINK_INFO))
	{
		pLayerCreateInfo =
			(VkLayerInstanceCreateInfo*)pLayerCreateInfo->pNext;
	}

	if (!pLayerCreateInfo)
	{
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	PFN_vkGetInstanceProcAddr pvkGetInstanceProcAddr =
		pLayerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;

	// move chain for the next layer
	pLayerCreateInfo->u.pLayerInfo = pLayerCreateInfo->u.pLayerInfo->pNext;

	PFN_vkCreateInstance pvkCreateInstance =
		(PFN_vkCreateInstance)pvkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");

	VkResult result = pvkCreateInstance(pCreateInfo, pAllocator, pInstance);
	if (result != VK_SUCCESS)
	{
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// fetch our own dispatch table for the functions we need, into the next layer
	VkLayerInstanceDispatchTable dispatch_table;

	dispatch_table.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
		pvkGetInstanceProcAddr(*pInstance, "vkGetInstanceProcAddr");

	dispatch_table.DestroyInstance = (PFN_vkDestroyInstance)
		pvkGetInstanceProcAddr(*pInstance, "vkDestroyInstance");

	dispatch_table.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)
		pvkGetInstanceProcAddr(*pInstance, "vkEnumerateDeviceExtensionProperties");

	{
		ContextVK& context = GetContext();
		std::lock_guard<std::mutex> lock(context.mutex_);
		context.instance_dispatch_.insert({ DispatchKey(*pInstance), dispatch_table });
	}

	return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT void VKAPI_CALL deshade_vkDestroyInstance(
	VkInstance instance,
	const VkAllocationCallbacks*)
{
	ContextVK& context = GetContext();
	std::lock_guard<std::mutex> lock(context.mutex_);
	context.instance_dispatch_.erase(DispatchKey(instance));
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkCreateDevice(
	VkPhysicalDevice physicalDevice,
	const VkDeviceCreateInfo* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDevice* pDevice
)
{
	VkLayerDeviceCreateInfo* pLayerCreateInfo =
		(VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

	// step through the chain
	while (pLayerCreateInfo && (pLayerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
	                            pLayerCreateInfo->function != VK_LAYER_LINK_INFO))
	{
		pLayerCreateInfo =
			(VkLayerDeviceCreateInfo*)pLayerCreateInfo->pNext;
	}

	if (!pLayerCreateInfo)
	{
		// could not find loader instance create info
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	PFN_vkGetInstanceProcAddr pvkGetInstanceProcAddr =
		(PFN_vkGetInstanceProcAddr)pLayerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	PFN_vkGetDeviceProcAddr pvkGetDeviceProcAddr =
		(PFN_vkGetDeviceProcAddr)pLayerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;

	// move chain for next layer
	pLayerCreateInfo->u.pLayerInfo = pLayerCreateInfo->u.pLayerInfo->pNext;

	PFN_vkCreateDevice pvkCreateDevice =
		(PFN_vkCreateDevice)pvkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");

	VkResult result = pvkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
	if (result != VK_SUCCESS)
	{
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// fetch our own dispatch table for the functions we need, into the next layer
	VkLayerDispatchTable dispatch_table;

	dispatch_table.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)
		pvkGetDeviceProcAddr(*pDevice, "vkGetDeviceProcAddr");

	dispatch_table.DestroyDevice = (PFN_vkDestroyDevice)
		pvkGetDeviceProcAddr(*pDevice, "vkDestroyDevice");

	dispatch_table.CreateShaderModule = (PFN_vkCreateShaderModule)
		pvkGetDeviceProcAddr(*pDevice, "vkCreateShaderModule");

	{
		ContextVK& context = GetContext();
		std::lock_guard<std::mutex> lock(context.mutex_);
		context.device_dispatch_.insert({ DispatchKey(*pDevice), dispatch_table });
	}

	return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT void VKAPI_CALL deshade_vkDestroyDevice(
	VkDevice device,
	const VkAllocationCallbacks*)
{
	ContextVK& context = GetContext();
	std::lock_guard<std::mutex> lock(context.mutex_);
	context.device_dispatch_.erase(DispatchKey(device));
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkEnumerateInstanceLayerProperties(
	uint32_t* pPropertyCount,
	VkLayerProperties* pProperties)
{
	if (pPropertyCount)
	{
		*pPropertyCount = 1;
	}

	if (pProperties)
	{
		std::strcpy(pProperties->layerName, "VK_LAYER_deshade");
		std::strcpy(pProperties->description, "deshade - https://github.com/graphitemaster/deshade");
		pProperties->implementationVersion = 1;
		pProperties->specVersion = VK_API_VERSION_1_0;
	}

	return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkEnumerateDeviceLayerProperties(
	VkPhysicalDevice,
	uint32_t* pPropertyCount,
	VkLayerProperties* pProperties)
{
	return deshade_vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkEnumerateInstanceExtensionProperties(
	const char* pLayerName,
	uint32_t* pPropertyCount,
	VkExtensionProperties*)
{
	if (!pLayerName || std::strcmp(pLayerName, "VK_LAYER_deshade"))
	{
		return VK_ERROR_LAYER_NOT_PRESENT;
	}

	// do not expose extensions
	if (pPropertyCount)
	{
		*pPropertyCount = 0;
	}

	return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkEnumerateDeviceExtensionProperties(
	VkPhysicalDevice physicalDevice,
	const char* pLayerName,
	uint32_t* pPropertyCount,
	VkExtensionProperties* pProperties)
{
	// pass through any queries that are not us
	if (!pLayerName || std::strcmp(pLayerName, "VK_LAYER_deshade"))
	{
		if (physicalDevice == VK_NULL_HANDLE)
		{
			return VK_SUCCESS;
		}

		ContextVK& context = GetContext();
		std::lock_guard<std::mutex> lock(context.mutex_);
		auto find = context.instance_dispatch_.find(DispatchKey(physicalDevice));
		if (find == context.instance_dispatch_.end())
		{
			return VK_ERROR_DEVICE_LOST;
		}
		return find->second.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
	}

	// don't expose any extensions
	if (pPropertyCount)
	{
		*pPropertyCount = 0;
	}

	return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL deshade_vkCreateShaderModule(
	VkDevice device,
	const VkShaderModuleCreateInfo* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkShaderModule* pShaderModule
)
{
	ContextVK& context = GetContext();
	std::lock_guard<std::mutex> lock(context.mutex_);
	auto find = context.device_dispatch_.find(DispatchKey(device));
	if (find != context.device_dispatch_.end())
	{
		const uint32_t* pCode = pCreateInfo->pCode;
		const ExecutionModel model = GetExecutionModel(pCode, (const uint32_t *)((const uint8_t *)pCode) + pCreateInfo->codeSize);

		// calculate hash
		std::string hash = Hash128((const uint8_t*)pCode, pCreateInfo->codeSize);

		std::vector<char> contents;
		// check if a shader replacement exists
		std::string file_name = "shaders/" + hash + GetShaderExtensionString(model);
		std::ifstream file_contents(file_name, std::ios::binary);
		if (file_contents.is_open())
		{
			// construct string from replacement contents
			Log("Replaced % shader \"%\"\n", GetShaderTypeString(model), hash);
			contents.assign((std::istreambuf_iterator<char>(file_contents)),
			                 std::istreambuf_iterator<char>());
		}
		else
		{
			// construct from source contents
			contents.assign((const uint8_t*)pCode, (const uint8_t*)pCode + pCreateInfo->codeSize);

			// write the contents to a file
			std::ofstream file(file_name, std::ios::binary);
			if (file.is_open())
			{
				file.write((const char *)contents.data(), contents.size());
				Log("Dumpped % shader \"%\"\n", GetShaderTypeString(model), hash);
			}
		}

		// replace the contents on call
		VkShaderModuleCreateInfo create_info = *pCreateInfo;
		create_info.codeSize = contents.size();
		create_info.pCode = (const uint32_t*)contents.data();
		return find->second.CreateShaderModule(device, &create_info, pAllocator, pShaderModule);
	}

	return VK_ERROR_DEVICE_LOST;
}

extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL deshade_vkGetDeviceProcAddr(
	VkDevice device,
	const char* pName)
{
	if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
	{
		return (PFN_vkVoidFunction)&deshade_vkGetDeviceProcAddr;
	}
	else if (!std::strcmp(pName, "vkEnumerateDeviceLayerProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateDeviceLayerProperties;
	}
	else if (!std::strcmp(pName, "vkEnumerateDeviceExtensionProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateDeviceExtensionProperties;
	}
	else if (!std::strcmp(pName, "vkCreateDevice"))
	{
		return (PFN_vkVoidFunction)&deshade_vkCreateDevice;
	}
	else if (!std::strcmp(pName, "vkDestroyDevice"))
	{
		return (PFN_vkVoidFunction)&deshade_vkDestroyDevice;
	}
	else if (!std::strcmp(pName, "vkCreateShaderModule"))
	{
		return (PFN_vkVoidFunction)&deshade_vkCreateShaderModule;
	}

	{
		ContextVK& context = GetContext();
		std::lock_guard<std::mutex> lock(context.mutex_);
		auto find = context.device_dispatch_.find(DispatchKey(device));
		if (find != context.device_dispatch_.end())
		{
			return find->second.GetDeviceProcAddr(device, pName);
		}
	}

	return VK_NULL_HANDLE;
}

extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL deshade_vkGetInstanceProcAddr(
	VkInstance instance,
	const char* pName)
{
	// instance chain functions that we intercept
	if (!std::strcmp(pName, "vkGetInstanceProcAddr"))
	{
		return (PFN_vkVoidFunction)&deshade_vkGetInstanceProcAddr;
	}
	else if (!std::strcmp(pName, "vkEnumerateInstanceLayerProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateInstanceLayerProperties;
	}
	else if (!std::strcmp(pName, "vkEnumerateInstanceExtensionProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateInstanceExtensionProperties;
	}
	else if (!std::strcmp(pName, "vkCreateInstance"))
	{
		return (PFN_vkVoidFunction)&deshade_vkCreateInstance;
	}
	else if (!std::strcmp(pName, "vkDestroyInstance"))
	{
		return (PFN_vkVoidFunction)&deshade_vkDestroyInstance;
	}

	// device chain functions that we intercept
	if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
	{
		return (PFN_vkVoidFunction)&deshade_vkGetDeviceProcAddr;
	}
	else if (!std::strcmp(pName, "vkEnumerateDeviceLayerProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateDeviceLayerProperties;
	}
	else if (!std::strcmp(pName, "vkEnumerateDeviceExtensionProperties"))
	{
		return (PFN_vkVoidFunction)&deshade_vkEnumerateDeviceExtensionProperties;
	}
	else if (!std::strcmp(pName, "vkCreateDevice"))
	{
		return (PFN_vkVoidFunction)&deshade_vkCreateDevice;
	}
	else if (!std::strcmp(pName, "vkDestroyDevice"))
	{
		return (PFN_vkVoidFunction)&deshade_vkDestroyDevice;
	}
	else if (!std::strcmp(pName, "vkCreateShaderModule"))
	{
		return (PFN_vkVoidFunction)&deshade_vkCreateShaderModule;
	}

	{
		ContextVK& context = GetContext();
		std::lock_guard<std::mutex> lock(context.mutex_);
		auto find = context.instance_dispatch_.find(DispatchKey(instance));
		if (find != context.instance_dispatch_.end())
		{
			return find->second.GetInstanceProcAddr(instance, pName);
		}
	}

	return VK_NULL_HANDLE;
}
