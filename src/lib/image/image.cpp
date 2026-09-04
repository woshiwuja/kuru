#include "image.hpp"
#include "../common/common.hpp"
#include "../core/core.hpp"
#include <ktx.h>

void transitionImageLayout(const vk::raii::CommandBuffer &commandBuffer,
                           vk::Image image, vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout,
                           vk::AccessFlags2 srcAccessMask,
                           vk::AccessFlags2 dstAccessMask,
                           vk::PipelineStageFlags2 srcStageMask,
                           vk::PipelineStageFlags2 dstStageMask,
                           vk::ImageAspectFlags aspectFlags) {
  vk::ImageMemoryBarrier2 barrier{
      .srcStageMask = srcStageMask,
      .srcAccessMask = srcAccessMask,
      .dstStageMask = dstStageMask,
      .dstAccessMask = dstAccessMask,
      .oldLayout = oldLayout,
      .newLayout = newLayout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {.aspectMask = aspectFlags,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};
  commandBuffer.pipelineBarrier2(vk::DependencyInfo{
      .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionImageLayout(const vk::raii::Image &image,
                           vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout) {
  auto &device = Core::get()->device;
  auto commandBuffer = device->beginSingleTimeCommands();

  vk::AccessFlags2 srcAccessMask;
  vk::AccessFlags2 dstAccessMask;
  vk::PipelineStageFlags2 srcStageMask;
  vk::PipelineStageFlags2 dstStageMask;

  if (oldLayout == vk::ImageLayout::eUndefined &&
      newLayout == vk::ImageLayout::eTransferDstOptimal) {
    srcAccessMask = {};
    dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
    srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
  } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
             newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
    srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  transitionImageLayout(*commandBuffer, *image, oldLayout, newLayout,
                        srcAccessMask, dstAccessMask, srcStageMask,
                        dstStageMask, vk::ImageAspectFlagBits::eColor);
  device->endSingleTimeCommands(*commandBuffer);
}

void copyBufferToImage(const vk::raii::Buffer &buffer, vk::raii::Image &image,
                       uint32_t width, uint32_t height) {
  auto &device = Core::get()->device;
  auto commandBuffer = device->beginSingleTimeCommands();
  vk::BufferImageCopy region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
      .imageOffset = {0, 0, 0},
      .imageExtent = {width, height, 1}};
  commandBuffer->copyBufferToImage(
      *buffer, *image, vk::ImageLayout::eTransferDstOptimal, {region});
  device->endSingleTimeCommands(*commandBuffer);
}

std::shared_ptr<Texture> loadTexture(const std::string &path) {
  auto texture = std::make_shared<Texture>();
  texture->load(path);
  return texture;
}

void Texture::load(const std::string &path)
{
	auto core = Core::get();
	ktxTexture    *kTexture;
	KTX_error_code result = ktxTexture_CreateFromNamedFile(
	    path.c_str(),
	    KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
	    &kTexture);

	if (result != KTX_SUCCESS)
	{
		throw std::runtime_error("failed to load ktx texture " + path);
	}

	uint32_t     texWidth       = kTexture->baseWidth;
	uint32_t     texHeight      = kTexture->baseHeight;
	ktx_size_t   imageSize      = ktxTexture_GetImageSize(kTexture, 0);
	ktx_uint8_t *ktxTextureData = ktxTexture_GetData(kTexture);

	vk::raii::Buffer       stagingBuffer       = nullptr;
	vk::raii::DeviceMemory stagingBufferMemory = nullptr;
	createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
	             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
	             stagingBuffer, stagingBufferMemory);

	void *data = stagingBufferMemory.mapMemory(0, imageSize);
	memcpy(data, ktxTextureData, imageSize);
	stagingBufferMemory.unmapMemory();

	vk::Format textureFormat;
	if (kTexture->classId == ktxTexture2_c)
	{
		// For KTX2 files, we can get the format directly
		auto *ktx2    = reinterpret_cast<ktxTexture2 *>(kTexture);
		textureFormat = static_cast<vk::Format>(ktx2->vkFormat);
		if (textureFormat == vk::Format::eUndefined)
		{
			textureFormat = vk::Format::eR8G8B8A8Unorm;
		}
	}
	else
	{
		// For KTX1 files or if we can't determine the format, use a reasonable default
		textureFormat = vk::Format::eR8G8B8A8Unorm;
	}

	core->graphics->createImage(texWidth, texHeight, textureFormat, vk::ImageTiling::eOptimal,
	            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	            vk::MemoryPropertyFlagBits::eDeviceLocal, image, memory);

	transitionImageLayout(image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	copyBufferToImage(stagingBuffer, image, texWidth, texHeight);
	transitionImageLayout(image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	ktxTexture_Destroy(kTexture);

	view = core->graphics->createImageView(image, textureFormat, vk::ImageAspectFlagBits::eColor);
}
