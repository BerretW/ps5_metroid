#define VK_NO_PROTOTYPES
#include "cube_probe.h"
#ifdef ECHO_RADV_CUBE_INTERACTIVE
#include "cube_input_spirv.h"
#else
#include "cube_spirv.h"
#endif

#include <string.h>

void echo_radv_cube_destroy(EchoRadvCube *cube)
{
    for (uint32_t i = 0; i < cube->count; ++i) {
        if (cube->framebuffers[i]) cube->destroy_framebuffer(cube->device, cube->framebuffers[i], NULL);
        if (cube->views[i]) cube->destroy_view(cube->device, cube->views[i], NULL);
    }
    if (cube->pipeline) cube->destroy_pipeline(cube->device, cube->pipeline, NULL);
    if (cube->layout) cube->destroy_layout(cube->device, cube->layout, NULL);
    if (cube->fragment_shader) cube->destroy_shader(cube->device, cube->fragment_shader, NULL);
    if (cube->vertex_shader) cube->destroy_shader(cube->device, cube->vertex_shader, NULL);
    if (cube->render_pass) cube->destroy_render_pass(cube->device, cube->render_pass, NULL);
    memset(cube, 0, sizeof(*cube));
}

int echo_radv_cube_create(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                          VkFormat format, VkExtent2D extent, const VkImage *images, uint32_t count,
                          EchoRadvEvent event)
{
    memset(cube, 0, sizeof(*cube));
    if (!images || count == 0 || count > 8 || extent.width == 0 || extent.height == 0) {
        event("radv.cube.input.invalid", 0);
        return 1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!images[i]) { event("radv.cube.image.null", (int)i); return 1; }
    }
    cube->device = device;
    cube->extent = extent;
    cube->count = count;
#define LOAD(type, name) type name = (type)get_proc(device, "vk" #name)
    LOAD(PFN_vkCreateRenderPass, CreateRenderPass);
    LOAD(PFN_vkCreateImageView, CreateImageView);
    LOAD(PFN_vkCreateFramebuffer, CreateFramebuffer);
    LOAD(PFN_vkCreateShaderModule, CreateShaderModule);
    LOAD(PFN_vkCreatePipelineLayout, CreatePipelineLayout);
    LOAD(PFN_vkCreateGraphicsPipelines, CreateGraphicsPipelines);
#define SAVE(field, type, name) cube->field = (type)get_proc(device, "vk" #name)
    SAVE(destroy_render_pass, PFN_vkDestroyRenderPass, DestroyRenderPass);
    SAVE(destroy_view, PFN_vkDestroyImageView, DestroyImageView);
    SAVE(destroy_framebuffer, PFN_vkDestroyFramebuffer, DestroyFramebuffer);
    SAVE(destroy_shader, PFN_vkDestroyShaderModule, DestroyShaderModule);
    SAVE(destroy_layout, PFN_vkDestroyPipelineLayout, DestroyPipelineLayout);
    SAVE(destroy_pipeline, PFN_vkDestroyPipeline, DestroyPipeline);
    SAVE(begin_pass, PFN_vkCmdBeginRenderPass, CmdBeginRenderPass);
    SAVE(bind_pipeline, PFN_vkCmdBindPipeline, CmdBindPipeline);
    SAVE(draw, PFN_vkCmdDraw, CmdDraw);
    SAVE(end_pass, PFN_vkCmdEndRenderPass, CmdEndRenderPass);
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    SAVE(push_constants, PFN_vkCmdPushConstants, CmdPushConstants);
#endif
#undef SAVE
#undef LOAD
    if (!CreateRenderPass || !CreateImageView || !CreateFramebuffer || !CreateShaderModule ||
        !CreatePipelineLayout || !CreateGraphicsPipelines || !cube->destroy_render_pass ||
        !cube->destroy_view || !cube->destroy_framebuffer || !cube->destroy_shader ||
        !cube->destroy_layout || !cube->destroy_pipeline || !cube->begin_pass ||
        !cube->bind_pipeline || !cube->draw || !cube->end_pass
#ifdef ECHO_RADV_CUBE_INTERACTIVE
        || !cube->push_constants
#endif
        ) {
        event("radv.cube.function.missing", 0);
        return 1;
    }
    VkResult result;
    VkAttachmentDescription attachment = {
        .format = format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference };
    VkSubpassDependency dependency = { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    VkRenderPassCreateInfo pass_info = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency };
    if (event("radv.cube.renderpass.begin", 0) != 0) goto fail;
    result = CreateRenderPass(device, &pass_info, NULL, &cube->render_pass);
    if (event("radv.cube.renderpass.end", result) != 0 || result != VK_SUCCESS) goto fail;
    if (!cube->render_pass) { event("radv.cube.renderpass.null", 0); goto fail; }
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1} };
        if (event("radv.cube.view.begin", (int)i) != 0) goto fail;
        result = CreateImageView(device, &view_info, NULL, &cube->views[i]);
        if (event("radv.cube.view.end", result) != 0 || result != VK_SUCCESS) goto fail;
        if (!cube->views[i]) { event("radv.cube.view.null", (int)i); goto fail; }
        VkFramebufferCreateInfo fb_info = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = cube->render_pass, .attachmentCount = 1, .pAttachments = &cube->views[i],
            .width = extent.width, .height = extent.height, .layers = 1 };
        if (event("radv.cube.framebuffer.begin", (int)i) != 0) goto fail;
        result = CreateFramebuffer(device, &fb_info, NULL, &cube->framebuffers[i]);
        if (event("radv.cube.framebuffer.end", result) != 0 || result != VK_SUCCESS) goto fail;
        if (!cube->framebuffers[i]) { event("radv.cube.framebuffer.null", (int)i); goto fail; }
    }
    VkShaderModuleCreateInfo shader_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(echo_cube_vert_spv), .pCode = echo_cube_vert_spv };
    if (event("radv.cube.vertex_shader.begin", 0) != 0) goto fail;
    result = CreateShaderModule(device, &shader_info, NULL, &cube->vertex_shader);
    if (event("radv.cube.vertex_shader.end", result) != 0 || result != VK_SUCCESS) goto fail;
    if (!cube->vertex_shader) { event("radv.cube.vertex_shader.null", 0); goto fail; }
    shader_info.codeSize = sizeof(echo_cube_frag_spv);
    shader_info.pCode = echo_cube_frag_spv;
    if (event("radv.cube.fragment_shader.begin", 0) != 0) goto fail;
    result = CreateShaderModule(device, &shader_info, NULL, &cube->fragment_shader);
    if (event("radv.cube.fragment_shader.end", result) != 0 || result != VK_SUCCESS) goto fail;
    if (!cube->fragment_shader) { event("radv.cube.fragment_shader.null", 0); goto fail; }
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
#ifdef ECHO_RADV_CUBE_INTERACTIVE
        , .pushConstantRangeCount = 1
#endif
    };
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    VkPushConstantRange rotation_range = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = 2 * sizeof(float) };
    layout_info.pPushConstantRanges = &rotation_range;
#endif
    if (event("radv.cube.layout.begin", 0) != 0) goto fail;
    result = CreatePipelineLayout(device, &layout_info, NULL, &cube->layout);
    if (event("radv.cube.layout.end", result) != 0 || result != VK_SUCCESS) goto fail;
    if (!cube->layout) { event("radv.cube.layout.null", 0); goto fail; }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = cube->vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = cube->fragment_shader, .pName = "main"}
    };
    VkPipelineVertexInputStateCreateInfo vertex = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    float side = (float)(extent.width < extent.height ? extent.width : extent.height);
    VkViewport viewport = { ((float)extent.width - side) * 0.5f, ((float)extent.height - side) * 0.5f, side, side, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, extent };
    VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo raster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
#ifdef ECHO_RADV_CUBE_INTERACTIVE
        .cullMode = VK_CULL_MODE_BACK_BIT,
#else
        .cullMode = VK_CULL_MODE_NONE,
#endif
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisample = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend_attachment = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo blend = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment };
    VkGraphicsPipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state, .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &blend, .layout = cube->layout, .renderPass = cube->render_pass, .subpass = 0 };
    if (event("radv.cube.pipeline.begin", 0) != 0) goto fail;
    result = CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &cube->pipeline);
    if (event("radv.cube.pipeline.end", result) != 0 || result != VK_SUCCESS) goto fail;
    if (!cube->pipeline) { event("radv.cube.pipeline.null", 0); goto fail; }
    return 0;
fail:
    echo_radv_cube_destroy(cube);
    return 1;
}

int echo_radv_cube_record(EchoRadvCube *cube, VkCommandBuffer command, uint32_t index, EchoRadvEvent event)
{
    if (index >= cube->count || !cube->framebuffers[index]) {
        event("radv.cube.record.invalid_index", 0);
        return 1;
    }
    VkClearValue clear = { .color = { .float32 = {0.025f, 0.035f, 0.075f, 1.0f} } };
    VkRenderPassBeginInfo pass = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = cube->render_pass, .framebuffer = cube->framebuffers[index],
        .renderArea = {{0, 0}, cube->extent}, .clearValueCount = 1, .pClearValues = &clear };
    if (event("radv.cube.draw.begin", 0) != 0) return 1;
    cube->begin_pass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    cube->bind_pipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, cube->pipeline);
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    cube->push_constants(command, cube->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(cube->angles), cube->angles);
#endif
    cube->draw(command, 36, 1, 0, 0);
    cube->end_pass(command);
    return event("radv.cube.draw.end", 0) != 0;
}
