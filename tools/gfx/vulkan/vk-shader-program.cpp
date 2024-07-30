// vk-shader-program.cpp
#include "vk-shader-program.h"

#include "vk-device.h"
#include "vk-util.h"
#include <vulkan/vulkan_core.h>
#include <filesystem>
#include <string>
#include <fstream>
#include <iostream>
#include <atomic>
#include <sstream>
namespace gfx
{

using namespace Slang;

namespace vk
{

ShaderProgramImpl::ShaderProgramImpl(DeviceImpl* device)
    : m_device(device)
{
    for (auto& shaderModule : m_modules)
        shaderModule = VK_NULL_HANDLE;
}

ShaderProgramImpl::~ShaderProgramImpl()
{
    for (auto shaderModule : m_modules)
    {
        if (shaderModule != VK_NULL_HANDLE)
        {
            m_device->m_api.vkDestroyShaderModule(m_device->m_api.m_device, shaderModule, nullptr);
        }
    }
}

void ShaderProgramImpl::comFree() { m_device.breakStrongReference(); }

std::string read_shader_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file \"" << filename << "\" for reading" << std::endl;
        return "";
    }
    std::ostringstream file_stream;
    file_stream << file.rdbuf();
    return file_stream.str();
}

void write_shader_file(const std::string& filename, size_t hash, const void* data, size_t size)
{
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file \"" << filename << "\" for writing" << std::endl;
        return;
    }
    std::string hash_string("// " + std::to_string(hash) + "\n");
    file.write(hash_string.data(), hash_string.size());
    file.write((char*)data, size);
    file.close();
}

VkPipelineShaderStageCreateInfo ShaderProgramImpl::compileEntryPoint(
    const char* entryPointName,
    ISlangBlob* code,
    VkShaderStageFlagBits stage,
    VkShaderModule& outShaderModule)
{
    char const* dataBegin = (char const*)code->getBufferPointer();
    char const* dataEnd = (char const*)code->getBufferPointer() + code->getBufferSize();

    std::string suffix = ".glsl";
    switch (stage) {
        case VK_SHADER_STAGE_COMPUTE_BIT:
            suffix = ".comp";
            break;
        case VK_SHADER_STAGE_VERTEX_BIT:
            suffix = ".vert";
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            suffix = ".frag";
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            suffix = ".geom";
            break;
        case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
            suffix = ".rgen";
            break;
        case VK_SHADER_STAGE_MISS_BIT_KHR:
            suffix = ".rmiss";
            break;
        case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
            suffix = ".rchit";
            break;
        case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
            suffix = ".rahit";
            break;
        case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
            suffix = ".rint";
            break;
    }

    std::filesystem::path shadertmp_dir("shader_tmp/");

    if (!std::filesystem::exists(shadertmp_dir)) std::filesystem::create_directory(shadertmp_dir);

    std::filesystem::path shader_dir("shader_tmp/glsl/");
    if (!std::filesystem::exists(shader_dir)) std::filesystem::create_directory(shader_dir);
    std::filesystem::path shader_bin_dir("shader_tmp/bin/");
    if (!std::filesystem::exists(shader_bin_dir)) std::filesystem::create_directory(shader_bin_dir);

    static std::atomic<int> shader_index = 0;
    size_t hash = std::hash<std::string>{}(std::string((char*)(code->getBufferPointer())));
    std::filesystem::path shader_file(shader_dir / (std::to_string(shader_index) + std::string(entryPointName) + suffix));
    std::filesystem::path shader_bin_file(shader_bin_dir / (std::to_string(hash) + ".spv"));
    shader_index++;
    if (!std::filesystem::exists(shader_bin_file))
    {
        write_shader_file(shader_file.string(), hash, code->getBufferPointer(), code->getBufferSize());
        //system(std::string("glslang --target-env vulkan1.2 -Os -o " + shader_bin_file.string() + " " + shader_file.string()).c_str());
        system(std::string("glslc --target-env=vulkan1.2 -I shader -O -o " + shader_bin_file.string() + " " + shader_file.string()).c_str());
    }
    std::string source = read_shader_file(shader_bin_file.string());
    // We need to make a copy of the code, since the Slang compiler
    // will free the memory after a compile request is closed.

    VkShaderModuleCreateInfo moduleCreateInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleCreateInfo.pCode = (uint32_t*)source.c_str();
    moduleCreateInfo.codeSize = source.size();

    VkShaderModule module;
    SLANG_VK_CHECK(m_device->m_api.vkCreateShaderModule(
        m_device->m_device, &moduleCreateInfo, nullptr, &module));
    outShaderModule = module;

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStageCreateInfo.stage = stage;

    shaderStageCreateInfo.module = module;
    shaderStageCreateInfo.pName = "main";

    return shaderStageCreateInfo;
}

Result ShaderProgramImpl::createShaderModule(
    slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode, const char* name)
{
    m_codeBlobs.add(kernelCode);
    VkShaderModule shaderModule;
    std::string nameString(name);
    nameString = nameString.substr(0, nameString.find_last_of("("));
    std::replace(nameString.begin(), nameString.end(), '/', '-');
    std::replace(nameString.begin(), nameString.end(), ' ', '_');
    const char* realEntryPointName = entryPointInfo->getNameOverride();
    nameString.append(realEntryPointName);
    m_stageCreateInfos.add(compileEntryPoint(
        nameString.c_str(),
        kernelCode,
        (VkShaderStageFlagBits)VulkanUtil::getShaderStage(entryPointInfo->getStage()),
        shaderModule));
    m_entryPointNames.add(realEntryPointName);
    m_modules.add(shaderModule);
    return SLANG_OK;
}

} // namespace vk
} // namespace gfx
