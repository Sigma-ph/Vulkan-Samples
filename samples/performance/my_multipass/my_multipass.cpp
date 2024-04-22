#include <vulkan_sample.h>
#include "my_multipass.h"

#include "common/vk_common.h"

#include "gui.h"
#include "rendering/pipeline_state.h"
#include "rendering/render_context.h"
#include "rendering/render_pipeline.h"
#include "rendering/subpasses/geometry_subpass.h"
#include "rendering/subpasses/lighting_subpass.h"
#include "rendering/postprocessing_renderpass.h"
#include "rendering/postprocessing_pass.h"
#include "scene_graph/node.h"

#include <thread>
#include <chrono>

void MyMultipass::prepare_render_context()
{
	get_render_context().prepare(1, [this](vkb::core::Image &&swapchain_image) { return create_render_target(std::move(swapchain_image)); });
}

std::unique_ptr<vkb::RenderTarget> MyMultipass::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto &extent = swapchain_image.get_extent();

	// G-Buffer should fit 128-bit budget for buffer color storage
	// in order to enable subpasses merging by the driver
	// Light (swapchain_image) RGBA8_UNORM   (32-bit)
	// Albedo                  RGBA8_UNORM   (32-bit)
	// Normal                  RGB10A2_UNORM (32-bit)

	vkb::core::Image depth_image{device,
	                             extent,
	                             vkb::get_suitable_depth_format(swapchain_image.get_device().get_gpu().get_handle()),
	                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rt_usage_flags,
	                             VMA_MEMORY_USAGE_GPU_ONLY};

	vkb::core::Image albedo_image{device,
	                              extent,
	                              albedo_format,
	                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	                              VMA_MEMORY_USAGE_GPU_ONLY};

	vkb::core::Image normal_image{device,
	                              extent,
	                              normal_format,
	                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	                              VMA_MEMORY_USAGE_GPU_ONLY};
	vkb::core::Image postprocessing_input{
	    device,
	    extent,
	    albedo_format,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	    VMA_MEMORY_USAGE_GPU_ONLY};

	std::vector<vkb::core::Image> images;

	// Attachment 0
	images.push_back(std::move(swapchain_image));

	// Attachment 1
	images.push_back(std::move(depth_image));

	// Attachment 2
	images.push_back(std::move(albedo_image));

	// Attachment 3
	images.push_back(std::move(normal_image));

	// Attachment 4
    images.push_back(std::move(postprocessing_input));

	return std::make_unique<vkb::RenderTarget>(std::move(images));
}

void my_draw_pipeline(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target, vkb::RenderPipeline &render_pipeline, vkb::Gui *gui = nullptr)
{
	auto &extent = render_target.get_extent();

	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	render_pipeline.draw(command_buffer, render_target);

	if (gui)
	{
		gui->draw(command_buffer);
	}

	command_buffer.end_render_pass();
}

bool MyMultipass::prepare(const vkb::ApplicationOptions &options)
{
	if (!VulkanSample::prepare(options))
	{
		return false;
	}

	std::set<VkImageUsageFlagBits> usage = {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT};
	get_render_context().update_swapchain(usage);

	load_scene("scenes/sponza/Sponza01.gltf");

	get_scene().clear_components<vkb::sg::Light>();

	auto light_pos   = glm::vec3(0.0f, 128.0f, -225.0f);
	auto light_color = glm::vec3(1.0, 1.0, 1.0);

	// Magic numbers used to offset lights in the Sponza scene
	for (int i = -4; i < 4; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			glm::vec3 pos = light_pos;
			pos.x += i * 400;
			pos.z += j * (225 + 140);
			pos.y = 8;

			for (int k = 0; k < 3; ++k)
			{
				pos.y = pos.y + (k * 100);

				light_color.x = static_cast<float>(rand()) / (RAND_MAX);
				light_color.y = static_cast<float>(rand()) / (RAND_MAX);
				light_color.z = static_cast<float>(rand()) / (RAND_MAX);

				vkb::sg::LightProperties props;
				props.color     = light_color;
				props.intensity = 0.2f;

				vkb::add_point_light(get_scene(), pos, props);
			}
		}
	}

	auto &camera_node = vkb::add_free_camera(get_scene(), "main_camera", get_render_context().get_surface_extent());
	camera            = dynamic_cast<vkb::sg::PerspectiveCamera *>(&camera_node.get_component<vkb::sg::Camera>());

    geometry_render_pipeline = create_geometry_renderpass();
	lighting_render_pipeline = create_lighting_renderpass();
	postprocessing_pipeline  = create_postfog_renderpass();

	// Enable stats
	get_stats().request_stats({vkb::StatIndex::frame_times,
	                           vkb::StatIndex::gpu_fragment_jobs,
	                           vkb::StatIndex::gpu_tiles,
	                           vkb::StatIndex::gpu_ext_read_bytes,
	                           vkb::StatIndex::gpu_ext_write_bytes});

	// Enable gui
	create_gui(*window, &get_stats());

	return true;
}


void MyMultipass::update(float delta_time)
{
    if (configs[Config::GeometryPassCount].value != last_geometry_count)
    {
        LOGI("Changing GeometryPassCount");
        last_geometry_count = configs[Config::GeometryPassCount].value;
    }
    if (configs[Config::LightingPassCount].value != last_lighting_count)
    {
        LOGI("Changing LightingPassCount");
        last_lighting_count = configs[Config::LightingPassCount].value;
    }
    if (configs[Config::PostProcessingPassCount].value != last_postprocessing_count)
    {
        LOGI("Changing PostProcessingPassCount");
        last_postprocessing_count = configs[Config::PostProcessingPassCount].value;
    }
	if (fps_list[configs[Config::TargetFPS].value] != target_fps)
	{
		LOGI("Changing FPS");
		target_fps = fps_list[configs[Config::TargetFPS].value];
	}
    if (delta_time < 1 / target_fps){
        int time_to_delay = (1 / target_fps - delta_time) * 1000;
        std::chrono::milliseconds duration(time_to_delay);
        std::this_thread::sleep_for(duration);
        delta_time = 1 / target_fps;
    }

	VulkanSample::update(delta_time);
}


std::unique_ptr<vkb::RenderPipeline> MyMultipass::create_geometry_renderpass()
{
	// Geometry subpass
	auto geometry_vs   = vkb::ShaderSource{"deferred/geometry.vert"};
	auto geometry_fs   = vkb::ShaderSource{"deferred/geometry.frag"};
	auto scene_subpass = std::make_unique<vkb::GeometrySubpass>(get_render_context(), std::move(geometry_vs), std::move(geometry_fs), get_scene(), *camera);

	// Outputs are depth, albedo, and normal
	scene_subpass->set_output_attachments({1, 2, 3});

	// Create geometry pipeline
	std::vector<std::unique_ptr<vkb::Subpass>> scene_subpasses{};
	scene_subpasses.push_back(std::move(scene_subpass));

	auto geometry_render_pipeline = std::make_unique<vkb::RenderPipeline>(std::move(scene_subpasses));

	geometry_render_pipeline->set_load_store(vkb::gbuffer::get_clear_store_all());

	geometry_render_pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

	return geometry_render_pipeline;
}


std::unique_ptr<vkb::RenderPipeline> MyMultipass::create_lighting_renderpass()
{
	// Lighting subpass
	auto lighting_vs      = vkb::ShaderSource{"deferred/lighting.vert"};
	auto lighting_fs      = vkb::ShaderSource{"deferred/lighting.frag"};
	auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *camera, get_scene());

	// Inputs are depth, albedo, and normal from the geometry subpass
	lighting_subpass->set_input_attachments({1, 2, 3});
	lighting_subpass->set_output_attachments({4});
	// Create lighting pipeline
	std::vector<std::unique_ptr<vkb::Subpass>> lighting_subpasses{};
	lighting_subpasses.push_back(std::move(lighting_subpass));

	auto lighting_render_pipeline = std::make_unique<vkb::RenderPipeline>(std::move(lighting_subpasses));

	std::vector<vkb::LoadStoreInfo> load_store{5};

	// Swapchain
	load_store[0].load_op  = VK_ATTACHMENT_LOAD_OP_LOAD;
	load_store[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Depth
	load_store[1].load_op  = VK_ATTACHMENT_LOAD_OP_LOAD;
	load_store[1].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Albedo
	load_store[2].load_op  = VK_ATTACHMENT_LOAD_OP_LOAD;
	load_store[2].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Normal
	load_store[3].load_op  = VK_ATTACHMENT_LOAD_OP_CLEAR;
	load_store[3].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Postprocess
	load_store[4].load_op  = VK_ATTACHMENT_LOAD_OP_CLEAR;
	load_store[4].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	lighting_render_pipeline->set_load_store(load_store);

	lighting_render_pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

	return lighting_render_pipeline;
}

std::unique_ptr<vkb::PostProcessingPipeline> MyMultipass::create_postfog_renderpass()
{
	auto postprocessing_vs = vkb::ShaderSource{"postprocessing/postprocessing.vert"};
	auto fog_fs = vkb::ShaderSource{"postprocessing/my_post.frag"};

	std::unique_ptr<vkb::PostProcessingPipeline> postprocessing_pipeline2 = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), std::move(postprocessing_vs));
	postprocessing_pipeline2->add_pass().add_subpass(std::move(fog_fs));
	postprocessing_pipeline2->get_pass(0).get_subpass(0).set_input_attachments({1, 4});

	vkb::PostProcessingRenderPass &fog_renderpass = postprocessing_pipeline2->get_pass(0);

	vkb::PostProcessingSubpass    &fog_subpass    = fog_renderpass.get_subpass(0); 
	fog_subpass.bind_sampled_image("DepthTexture", {1});
	fog_subpass.bind_sampled_image("ColorTexture", {4});
	
	return postprocessing_pipeline2;
}

void MyMultipass::draw_gui()
{
    auto lines = configs.size();
    if (camera->get_aspect_ratio() < 1.0f)
    {
        // In portrait, show buttons below heading
        lines = lines * 2;
    }

    get_gui().show_options_window(
            /* body = */ [this, lines]() {
                // Create a line for every config
                for (size_t i = 0; i < configs.size(); ++i)
                {
                    // Avoid conflicts between buttons with identical labels
                    ImGui::PushID(vkb::to_u32(i));

                    auto &config = configs[i];

                    ImGui::Text("%s: ", config.description);

                    if (camera->get_aspect_ratio() > 1.0f)
                    {
                        // In landscape, show all options following the heading
                        ImGui::SameLine();
                    }

                    // Create a radio button for every option
                    for (size_t j = 0; j < config.options.size(); ++j)
                    {
                        ImGui::RadioButton(config.options[j], &config.value, vkb::to_u32(j));

                        // Keep it on the same line til the last one
                        if (j < config.options.size() - 1)
                        {
                            ImGui::SameLine();
                        }
                    }

                    ImGui::PopID();
                }
            },
            /* lines = */ vkb::to_u32(lines));
}

void MyMultipass::draw_renderpass(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	draw_renderpasses(command_buffer, render_target);
}

void MyMultipass::draw_renderpasses(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	// First render pass (no gui)
    int geometry_repeat_time = last_geometry_count * 4 + (last_geometry_count == 0);
    for(int i=0; i<geometry_repeat_time; ++i){
        my_draw_pipeline(command_buffer, render_target, *geometry_render_pipeline);

        if(i == geometry_repeat_time - 1){
            break;
        }
        for (size_t i = 1; i < render_target.get_views().size() - 1; ++i) {
            auto &view = render_target.get_views()[i];

            vkb::ImageMemoryBarrier barrier;

            if (i == 1) {
                barrier.old_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barrier.new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                barrier.src_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                barrier.src_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            } else {
                barrier.old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }

            barrier.dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

            command_buffer.image_memory_barrier(view, barrier);
        }
    }

    int light_repeat_time = last_lighting_count * 4 + (last_lighting_count == 0);
    for(int i=0; i<light_repeat_time; ++i) {
        // Memory barriers needed
        for (size_t i = 1; i < render_target.get_views().size() - 1; ++i) {
            auto &view = render_target.get_views()[i];

            vkb::ImageMemoryBarrier barrier;

            if (i == 1) {
                barrier.old_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barrier.new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                barrier.src_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                barrier.src_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            } else {
                barrier.old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }

            barrier.dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

            command_buffer.image_memory_barrier(view, barrier);
        }
        // Second render pass
        if(i == light_repeat_time - 1){
            my_draw_pipeline(command_buffer, render_target, *lighting_render_pipeline, &get_gui());
        }
        else{
            my_draw_pipeline(command_buffer, render_target, *lighting_render_pipeline);
        }
    }

    int postprocessing_repeat_time = last_postprocessing_count * 4 + (last_postprocessing_count == 0);
    for(int i=0; i<postprocessing_repeat_time; ++i){
        vkb::ImageMemoryBarrier barrier;
        barrier.old_layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.new_layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dst_stage_mask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

        vkb::ImageMemoryBarrier barrier2;
        barrier.old_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.src_stage_mask   = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.src_access_mask  = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier2.dst_stage_mask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier2.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

        command_buffer.image_memory_barrier(render_target.get_views()[4], barrier);
        command_buffer.image_memory_barrier(render_target.get_views()[1], barrier2);

        // Third render pass
        postprocessing_pipeline->draw(command_buffer, render_target);
        command_buffer.end_render_pass();

    }

}

std::unique_ptr<vkb::VulkanSample<vkb::BindingType::C>> create_my_multipass()
{
	return std::make_unique<MyMultipass>();
}