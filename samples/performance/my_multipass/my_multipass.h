#pragma once

#include "rendering/render_pipeline.h"
#include "rendering/postprocessing_pipeline.h"
#include "scene_graph/components/perspective_camera.h"
#include "vulkan_sample.h"

class MyMultipass : public vkb::VulkanSample<vkb::BindingType::C>
{
 public:
	MyMultipass(){};

	bool prepare(const vkb::ApplicationOptions &options) override;

	void update(float delta_time) override;

	virtual ~MyMultipass() = default;

    void draw_gui() override;

private:
	virtual void prepare_render_context() override;

	std::unique_ptr<vkb::RenderTarget> create_render_target(vkb::core::Image &&swapchain_image);

	std::unique_ptr<vkb::RenderPipeline> create_geometry_renderpass();

    std::unique_ptr<vkb::RenderPipeline> create_lighting_renderpass();

	std::unique_ptr<vkb::PostProcessingPipeline> create_postfog_renderpass();
	
	virtual void draw_renderpass(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target) override;

private:
	std::unique_ptr<vkb::RenderPipeline> geometry_render_pipeline{};

	std::unique_ptr<vkb::RenderPipeline> lighting_render_pipeline{};

	std::unique_ptr<vkb::PostProcessingPipeline> postprocessing_pipeline{};

	void draw_renderpasses(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target);

	vkb::sg::PerspectiveCamera *camera{};
    float target_fps = 20;
    std::vector<float> fps_list = {20, 30, 40, 60};

	VkFormat          albedo_format{VK_FORMAT_R8G8B8A8_UNORM};
	VkFormat          normal_format{VK_FORMAT_A2B10G10R10_UNORM_PACK32};
	VkImageUsageFlags rt_usage_flags{VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT};

    struct Config
    {
        /**
         * @brief Configurations type
         */
        enum Type
        {
            GeometryPassCount,
            LightingPassCount,
            PostProcessingPassCount,
            TargetFPS
        } type;

        /// Used as label by the GUI
        const char *description;

        /// List of options to choose from
        std::vector<const char *> options;

        /// Index of the current selected option
        int value;
    };

    uint16_t last_geometry_count{0};
    uint16_t last_lighting_count{0};
	uint16_t last_postprocessing_count{0};


    std::vector<Config> configs = {
            {/* config      = */ Config::GeometryPassCount,
                    /* description = */ "GeometryPassCount",
                    /* options     = */ {"1x", "4x", "8x", "12x"},
                    /* value       = */ 0},
            {/* config      = */ Config::LightingPassCount,
                    /* description = */ "LightingPassCount",
                    /* options     = */ {"1x", "4x", "8x", "12x"},
                    /* value       = */ 0},
            {/* config      = */ Config::PostProcessingPassCount,
                    /* description = */ "PostProcessingPassCount",
                    /* options     = */ {"1x", "4x", "8x", "12x"},
                    /* value       = */ 0},
	        {/* config      = */ Config::TargetFPS,
	            /* description = */ "Set Target FPS",
	            /* options     = */ {"20", "30", "40", "60"},
	            /* value       = */ 0},
            };
};

std::unique_ptr<vkb::VulkanSample<vkb::BindingType::C>> create_my_multipass();