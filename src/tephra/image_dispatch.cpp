
#include "image_impl.hpp"
#include "device/device_container.hpp"
#include "job/local_images.hpp"

namespace tp {

static const char* imageViewTypeName = "ImageView";

// Combines component mappings as result = outer(inner(identity))
ComponentMapping chainComponentMapping(const ComponentMapping& outer, const ComponentMapping& inner) {
    static constexpr int N = 4;
    static constexpr ComponentSwizzle ComponentMapping::*const members[N] = {
        &ComponentMapping::r, &ComponentMapping::g, &ComponentMapping::b, &ComponentMapping::a
    };

    ComponentMapping result;
    for (int i = 0; i < N; i++) {
        ComponentSwizzle outerSwizzle = outer.*members[i];
        if (outerSwizzle == ComponentSwizzle::Identity)
            result.*members[i] = inner.*members[i];
        else if (
            static_cast<uint32_t>(outerSwizzle) >= static_cast<uint32_t>(ComponentSwizzle::R) &&
            static_cast<uint32_t>(outerSwizzle) <= static_cast<uint32_t>(ComponentSwizzle::A)) {
            int swizzleIndex = static_cast<uint32_t>(outerSwizzle) - static_cast<uint32_t>(ComponentSwizzle::R);
            result.*members[i] = inner.*members[swizzleIndex];
        } else {
            // It's a constant that doesn't reference the inner mapping
            result.*members[i] = outerSwizzle;
        }
    }

    return result;
}

ImageViewSetup::ImageViewSetup(
    ImageViewType viewType,
    ImageSubresourceRange subresourceRange,
    Format format,
    ComponentMapping componentMapping)
    : viewType(viewType), subresourceRange(subresourceRange), format(format), componentMapping(componentMapping) {}

ImageView::ImageView() : setup(ImageViewType::View1D, {}) {}

ImageSubresourceRange ImageView::getWholeRange() const {
    return ImageSubresourceRange(
        setup.subresourceRange.aspectMask,
        0,
        setup.subresourceRange.mipLevelCount,
        0,
        setup.subresourceRange.arrayLayerCount);
}

Extent3D ImageView::getExtent(uint32_t mipLevel) const {
    if (viewsJobLocalImage()) {
        return std::get<JobLocalImageImpl*>(image)->getExtent(setup.subresourceRange.baseMipLevel + mipLevel);
    } else if (!isNull()) {
        return std::get<ImageImpl*>(image)->getExtent_(setup.subresourceRange.baseMipLevel + mipLevel);
    } else {
        return {};
    }
}

MultisampleLevel ImageView::getSampleLevel() const {
    if (viewsJobLocalImage()) {
        return std::get<JobLocalImageImpl*>(image)->getSampleLevel();
    } else if (!isNull()) {
        return std::get<ImageImpl*>(image)->getSampleLevel_();
    } else {
        return {};
    }
}

ImageView ImageView::createView(ImageViewSetup subviewSetup) {
    if (subviewSetup.subresourceRange.mipLevelCount == VK_REMAINING_MIP_LEVELS) {
        subviewSetup.subresourceRange.mipLevelCount = setup.subresourceRange.mipLevelCount -
            subviewSetup.subresourceRange.baseMipLevel;
    }
    subviewSetup.subresourceRange.baseMipLevel += setup.subresourceRange.baseMipLevel;

    if (subviewSetup.subresourceRange.arrayLayerCount == VK_REMAINING_ARRAY_LAYERS) {
        subviewSetup.subresourceRange.arrayLayerCount = setup.subresourceRange.arrayLayerCount -
            subviewSetup.subresourceRange.baseArrayLayer;
    }
    subviewSetup.subresourceRange.baseArrayLayer += setup.subresourceRange.baseArrayLayer;
    subviewSetup.subresourceRange.aspectMask &= setup.subresourceRange.aspectMask;

    if (subviewSetup.format == Format::Undefined) {
        subviewSetup.format = setup.format;
    }

    subviewSetup.componentMapping = chainComponentMapping(subviewSetup.componentMapping, setup.componentMapping);

    if (viewsJobLocalImage()) {
        return std::get<JobLocalImageImpl*>(image)->createView(std::move(subviewSetup));
    } else if (!isNull()) {
        return std::get<ImageImpl*>(image)->createView(std::move(subviewSetup));
    } else {
        return {};
    }
}

VkImageViewHandle ImageView::vkGetImageViewHandle() const {
    // Vulkan image views are accessed frequently, so cache them
    if (vkCachedImageViewHandle.isNull()) {
        if (viewsJobLocalImage()) {
            if (std::get<JobLocalImageImpl*>(image)->hasUnderlyingImage()) {
                vkCachedImageViewHandle = JobLocalImageImpl::vkGetImageViewHandle(*this);
            }
        } else if (!isNull()) {
            vkCachedImageViewHandle = ImageImpl::vkGetImageViewHandle(*this);
        }
    }
    return vkCachedImageViewHandle;
}

bool operator==(const ImageView& lhs, const ImageView& rhs) {
    if (lhs.setup.viewType != rhs.setup.viewType || lhs.setup.subresourceRange != rhs.setup.subresourceRange ||
        lhs.setup.format != rhs.setup.format || lhs.setup.componentMapping != rhs.setup.componentMapping) {
        return false;
    }
    if (lhs.viewsJobLocalImage() != rhs.viewsJobLocalImage()) {
        return false;
    }
    return lhs.image == rhs.image;
}

VkImageHandle ImageView::vkResolveImageHandle(uint32_t* baseMipLevel, uint32_t* baseArrayLevel) const {
    if (viewsJobLocalImage()) {
        if (std::get<JobLocalImageImpl*>(image)->hasUnderlyingImage()) {
            ImageView underlyingView = JobLocalImageImpl::getViewToUnderlyingImage(*this);
            TEPHRA_ASSERT(!underlyingView.viewsJobLocalImage());
            return underlyingView.vkResolveImageHandle(baseMipLevel, baseArrayLevel);
        } else {
            return {};
        }
    } else if (!isNull()) {
        *baseMipLevel = setup.subresourceRange.baseMipLevel;
        *baseArrayLevel = setup.subresourceRange.baseArrayLayer;
        return std::get<ImageImpl*>(image)->vkGetImageHandle_();
    } else {
        return {};
    }
}

ImageView::ImageView(ImageImpl* persistentImage, ImageViewSetup setup)
    : image(persistentImage), setup(std::move(setup)) {}

ImageView::ImageView(JobLocalImageImpl* jobLocalImage, ImageViewSetup setup)
    : image(jobLocalImage), setup(std::move(setup)) {}

ImageType Image::getType() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getType_();
}

Format Image::getFormat() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getFormat_();
}

Extent3D Image::getExtent(uint32_t mipLevel) const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getExtent_(mipLevel);
}

ImageSubresourceRange Image::getWholeRange() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getWholeRange_();
}

MultisampleLevel Image::getSampleLevel() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getSampleLevel_();
}

MemoryLocation Image::getMemoryLocation() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getMemoryLocation_();
}

ImageUsage Image::getUsage() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getUsage();
}

const ImageView& Image::getDefaultView() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->getDefaultView_();
}

ImageView Image::createView(ImageViewSetup viewSetup) {
    auto imageImpl = static_cast<ImageImpl*>(this);
    return imageImpl->createView_(std::move(viewSetup));
}

VmaAllocationHandle Image::vmaGetMemoryAllocationHandle() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->vmaGetMemoryAllocationHandle_();
}

VkImageHandle Image::vkGetImageHandle() const {
    auto imageImpl = static_cast<const ImageImpl*>(this);
    return imageImpl->vkGetImageHandle_();
}

ImageImpl::~ImageImpl() {
    TEPHRA_DEBUG_SET_CONTEXT_DESTRUCTOR(getDebugTarget());
    destroyHandles(false);
}

}
