#pragma once

#include "display_probe.h"

typedef struct EchoRadvCube {
    VkDevice device;
    VkExtent2D extent;
    VkRenderPass render_pass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkImageView views[8];
    VkFramebuffer framebuffers[8];
    uint32_t count;
    float angles[2];
    PFN_vkDestroyRenderPass destroy_render_pass;
    PFN_vkDestroyPipelineLayout destroy_layout;
    PFN_vkDestroyPipeline destroy_pipeline;
    PFN_vkDestroyShaderModule destroy_shader;
    PFN_vkDestroyImageView destroy_view;
    PFN_vkDestroyFramebuffer destroy_framebuffer;
    PFN_vkCmdBeginRenderPass begin_pass;
    PFN_vkCmdBindPipeline bind_pipeline;
    PFN_vkCmdDraw draw;
    PFN_vkCmdEndRenderPass end_pass;
    PFN_vkCmdPushConstants push_constants;
} EchoRadvCube;

int echo_radv_cube_create(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                          VkFormat format, VkExtent2D extent, const VkImage *images, uint32_t count,
                          EchoRadvEvent event);
int echo_radv_cube_record(EchoRadvCube *cube, VkCommandBuffer command, uint32_t index, EchoRadvEvent event);
void echo_radv_cube_destroy(EchoRadvCube *cube);
