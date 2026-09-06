#pragma once
#include <memory>
#include <string>
#include <vulkan/vulkan_raii.hpp>

struct Texture
{
	vk::raii::Image        image  = nullptr;
	vk::raii::DeviceMemory memory = nullptr;
	vk::raii::ImageView    view   = nullptr;
	void load(const std::string &path);
	// Raw, tightly-packed RGBA8 pixels (e.g. a glTF's already-decoded embedded image).
	void loadFromPixels(const unsigned char *pixels, uint32_t width, uint32_t height,
	                    vk::Format format = vk::Format::eR8G8B8A8Srgb);
	// Solid fuchsia 1x1: what load() falls back to when a texture file is
	// missing or fails to parse, so a bad path shows up as an obviously wrong
	// color instead of crashing the whole app.
	void loadFallback();
};

std::shared_ptr<Texture> loadTexture(const std::string &path);
std::shared_ptr<Texture> loadTextureFromPixels(const unsigned char *pixels, uint32_t width,
                                               uint32_t height,
                                               vk::Format format = vk::Format::eR8G8B8A8Srgb);

// Records a layout transition into an already-begun command buffer.
void transitionImageLayout(const vk::raii::CommandBuffer &commandBuffer,
                           vk::Image image, vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout,
                           vk::AccessFlags2 srcAccessMask,
                           vk::AccessFlags2 dstAccessMask,
                           vk::PipelineStageFlags2 srcStageMask,
                           vk::PipelineStageFlags2 dstStageMask,
                           vk::ImageAspectFlags aspectFlags);
// Upload-time transition: takes the queue idle on its own single-time command
// buffer, so it is only for loading, never for the frame loop.
void transitionImageLayout(const vk::raii::Image &image,
                           vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout);
void copyBufferToImage(const vk::raii::Buffer &buffer, vk::raii::Image &image,
                       uint32_t width, uint32_t height);
