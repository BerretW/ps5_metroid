#include "echo/framework/gltf.hpp"
#include <cstring>
#include <cmath>

namespace echo::rendering {
namespace {
using framework::JsonValue;
Status bad(std::string_view message) noexcept { return {ErrorCode::InvalidValue,"glTF",message}; }
bool integer(JsonValue object, std::string_view key, std::uint32_t& output, bool optional=false) noexcept {
    auto field=object.find(key);
    if (!field.valid() && optional) { output=0; return true; }
    std::int64_t value{};
    if (!field.integer(value) || value<0 || value>UINT32_MAX) return false;
    output=static_cast<std::uint32_t>(value); return true;
}
struct Access {
    const std::byte* bytes{};
    std::uint32_t count{}, stride{}, components{}, componentType{};
};
Status accessor(JsonValue root, std::span<const std::byte> bytes, std::uint32_t index, Access& output) noexcept {
    auto value=root.find("accessors").at(index);
    if (!value.isObject()) return bad("accessor index or shape");
    if (value.find("sparse").valid()) return {ErrorCode::MissingCapability,"glTF","sparse accessors unsupported"};
    bool normalized{};
    if (value.find("normalized").valid() && (!value.find("normalized").boolean(normalized) || normalized))
        return {ErrorCode::MissingCapability,"glTF","normalized accessors unsupported"};
    std::uint32_t viewIndex{}, offset{}, type{}, count{};
    if (!integer(value,"bufferView",viewIndex) || !integer(value,"byteOffset",offset,true) ||
        !integer(value,"componentType",type) || !integer(value,"count",count) || !count) return bad("accessor fields");
    std::string_view shape{};
    if (!value.find("type").string(shape)) return bad("accessor type");
    unsigned components=shape=="SCALAR"?1:shape=="VEC2"?2:shape=="VEC3"?3:shape=="VEC4"?4:0;
    unsigned componentSize=type==5126 || type==5125 ? 4 : type==5123 ? 2 : type==5121 ? 1 : 0;
    if (!components || !componentSize) return {ErrorCode::MissingCapability,"glTF","accessor format unsupported"};
    auto view=root.find("bufferViews").at(viewIndex);
    std::uint32_t buffer{}, viewOffset{}, length{}, stride{};
    if (!view.isObject() || !integer(view,"buffer",buffer) || buffer ||
        !integer(view,"byteOffset",viewOffset,true) || !integer(view,"byteLength",length) ||
        !integer(view,"byteStride",stride,true)) return bad("buffer view fields");
    if (!stride) stride=componentSize*components;
    if (stride<componentSize*components || stride>252 || stride%componentSize ||
        viewOffset%componentSize || offset%componentSize ||
        std::uint64_t(viewOffset)+length>bytes.size() || offset>length ||
        std::uint64_t(count-1)*stride+componentSize*components>length-offset) return bad("accessor alignment or buffer range");
    output={bytes.data()+viewOffset+offset,count,stride,components,type};
    return Status::success();
}
float numberAt(const Access& accessor, unsigned element, unsigned component) noexcept {
    float output{}; std::memcpy(&output,accessor.bytes+std::size_t(element)*accessor.stride+component*4,4);
    return output;
}
bool numbers(JsonValue object, std::string_view name, float* output, std::size_t count) noexcept {
    auto value=object.find(name);
    if (!value.valid()) return true;
    if (!value.isArray() || value.size()!=count) return false;
    for (std::size_t i=0;i<count;++i) {
        double number{};
        if (!value.at(i).number(number) || !std::isfinite(number) || number<0 || number>1) return false;
        output[i]=static_cast<float>(number);
    }
    return true;
}
Status imageIndex(JsonValue root, JsonValue info, std::uint32_t& output) noexcept {
    output = UINT32_MAX;
    if (!info.valid()) return Status::success();
    std::uint32_t texture{}, texCoord{};
    if (!info.isObject() || !integer(info, "index", texture) ||
        (info.find("texCoord").valid() && (!integer(info, "texCoord", texCoord) || texCoord != 0)) ||
        info.find("extensions").valid()) return bad("texture index or UV set");
    const auto textures = root.find("textures");
    const auto images = root.find("images");
    const auto textureJson = textures.at(texture);
    const auto webp = textureJson.find("extensions").find("EXT_texture_webp");
    if (!textures.isArray() || !images.isArray() || !textureJson.isObject() ||
        !(webp.valid() ? integer(webp, "source", output) : integer(textureJson, "source", output)) ||
        output >= images.size())
        return bad("texture image reference");
    return Status::success();
}
Status generateTangents(std::span<Vertex> vertices, std::span<const std::uint32_t> indices) noexcept {
    std::array<Vec3, GltfMesh::kMaxVertices> tangents{}, bitangents{};
    std::array<int, GltfMesh::kMaxVertices> orientations{};
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
        const auto& v0 = vertices[a]; const auto& v1 = vertices[b]; const auto& v2 = vertices[c];
        const auto e1 = v1.position - v0.position, e2 = v2.position - v0.position;
        const float u1 = v1.uv.x - v0.uv.x, v1y = v1.uv.y - v0.uv.y;
        const float u2 = v2.uv.x - v0.uv.x, v2y = v2.uv.y - v0.uv.y;
        const float determinant = u1 * v2y - u2 * v1y;
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-10F)
            return bad("cannot generate tangents for degenerate UVs");
        const auto tangent = (e1 * v2y - e2 * v1y) * (1.0F / determinant);
        const auto bitangent = (e2 * u1 - e1 * u2) * (1.0F / determinant);
        const int orientation = determinant < 0 ? -1 : 1;
        for (auto index : {a, b, c}) {
            if (orientations[index] && orientations[index] != orientation)
                return bad("mirrored UVs require split vertices or exported tangents");
            orientations[index] = orientation;
            tangents[index] = tangents[index] + tangent;
            bitangents[index] = bitangents[index] + bitangent;
        }
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (!orientations[i]) continue;
        const auto normal = vertices[i].normal;
        const float normalLength = std::sqrt(dot(normal, normal));
        if (!std::isfinite(normalLength) || normalLength < 1e-10F) return bad("invalid normal for tangent generation");
        const auto n = normal * (1.0F / normalLength);
        auto tangent = tangents[i] - n * dot(n, tangents[i]);
        const float length = std::sqrt(dot(tangent, tangent));
        if (!std::isfinite(length) || length < 1e-10F) return bad("invalid generated tangent");
        tangent = tangent * (1.0F / length);
        const float handedness = dot(cross(n, tangent), bitangents[i]) < 0 ? -1.0F : 1.0F;
        vertices[i].tangent = {tangent.x, tangent.y, tangent.z, handedness};
    }
    return Status::success();
}
}
MeshView GltfMesh::mesh() const noexcept {
    if (!loaded_) return {};
    return {{vertices_.data(),vertexCount_},{indices_.data(),indexCount_},{submeshes_.data(),submeshCount_}};
}
Status GltfMesh::parse(std::string_view json, std::span<const std::byte> bytes, framework::JsonError& parseError) noexcept {
    vertexCount_=indexCount_=submeshCount_=materialCount_=0;
    loaded_=false;
    if (json.empty() || json.size()>framework::kMaxAssetJsonBytes) return bad("input size bound");
    framework::JsonDocument document{};
    auto status=framework::JsonDocument::parseAsset(json.data(),json.size(),document,parseError);
    if (!status) return status;
    return parseDocument(document.root(), bytes);
}
Status GltfMesh::parseDocument(framework::JsonValue root, std::span<const std::byte> bytes) noexcept {
    vertexCount_=indexCount_=submeshCount_=materialCount_=0;
    loaded_=false;
    if (!root.isObject() || bytes.empty() || bytes.size()>4*1024*1024) return bad("input size bound");
    Status status{};
    std::string_view version{};
    if (!root.find("asset").find("version").string(version) || version!="2.0")
        return {ErrorCode::UnsupportedVersion,"glTF","asset.version must be 2.0"};
    const auto required = root.find("extensionsRequired");
    if (required.valid() && !required.isArray()) return bad("required extensions shape");
    for (std::size_t i=0;i<required.size();++i) {
        std::string_view extension{};
        if (!required.at(i).string(extension) || extension!="EXT_texture_webp")
            return {ErrorCode::MissingCapability,"glTF","required extension unsupported"};
    }
    if (root.find("animations").size() || root.find("skins").size())
        return {ErrorCode::MissingCapability,"glTF","animations and skins unsupported"};
    const auto nodes = root.find("nodes"), scenes = root.find("scenes");
    if (nodes.valid() || scenes.valid()) {
        std::uint32_t sceneIndex{}, meshIndex{};
        std::int64_t nodeIndex{};
        if (!nodes.isArray() || nodes.size()!=1 || !scenes.isArray() || scenes.size()!=1 ||
            (root.find("scene").valid() && (!integer(root,"scene",sceneIndex) || sceneIndex!=0)) ||
            !scenes.at(0).find("nodes").isArray() || scenes.at(0).find("nodes").size()!=1 ||
            !scenes.at(0).find("nodes").at(0).integer(nodeIndex))
            return {ErrorCode::MissingCapability,"glTF","only one untransformed scene node supported"};
        const auto node=nodes.at(0);
        if (nodeIndex!=0 || !integer(node,"mesh",meshIndex) || meshIndex!=0 ||
            root.find("meshes").size()!=1 || node.find("children").valid() ||
            node.find("matrix").valid() || node.find("translation").valid() ||
            node.find("rotation").valid() || node.find("scale").valid() ||
            node.find("skin").valid() || node.find("weights").valid())
            return {ErrorCode::MissingCapability,"glTF","scene transforms or instancing unsupported"};
    }
    auto buffers=root.find("buffers");
    std::uint32_t byteLength{};
    if (!buffers.isArray() || buffers.size()!=1 || !integer(buffers.at(0),"byteLength",byteLength) || byteLength>bytes.size())
        return bad("exactly one caller-resolved buffer required");
    bytes=bytes.first(byteLength);
    auto materialJson=root.find("materials");
    if (materialJson.valid() && (!materialJson.isArray() || materialJson.size()>materials_.size()))
        return bad("material capacity or shape");
    for (std::size_t i=0;i<materialJson.size();++i) {
        auto value=materialJson.at(i);
        if (!value.isObject()) return bad("material object");
        auto pbr=value.find("pbrMetallicRoughness");
        if (pbr.valid() && !pbr.isObject()) return bad("PBR object");
        if (value.find("occlusionTexture").valid() || value.find("emissiveTexture").valid())
            return {ErrorCode::MissingCapability,"glTF","PBR texture channel unsupported"};
        Material material{}; material.model=MaterialModel::SimplePbr;
        GltfMaterialImages images{};
        status=imageIndex(root,pbr.find("baseColorTexture"),images.baseColor); if (!status) return status;
        status=imageIndex(root,pbr.find("metallicRoughnessTexture"),images.metallicRoughness); if (!status) return status;
        auto normalInfo=value.find("normalTexture");
        status=imageIndex(root,normalInfo,images.normal); if (!status) return status;
        double normalScale=1.0;
        if (normalInfo.find("scale").valid() && !normalInfo.find("scale").number(normalScale)) return bad("normal scale");
        material.normalScale=static_cast<float>(normalScale);
        float color[4]{1,1,1,1}, emission[3]{};
        if (!numbers(pbr,"baseColorFactor",color,4) || !numbers(value,"emissiveFactor",emission,3)) return bad("material colors");
        material.baseColor={color[0],color[1],color[2],color[3]}; material.emission={emission[0],emission[1],emission[2]};
        double metallic=1, roughness=1;
        if ((pbr.find("metallicFactor").valid() && !pbr.find("metallicFactor").number(metallic)) ||
            (pbr.find("roughnessFactor").valid() && !pbr.find("roughnessFactor").number(roughness))) return bad("PBR factors");
        material.metallic=static_cast<float>(metallic); material.roughness=static_cast<float>(roughness);
        std::string_view alpha="OPAQUE";
        if (value.find("alphaMode").valid() && !value.find("alphaMode").string(alpha)) return bad("alpha mode");
        if (alpha=="BLEND") material.alpha=AlphaMode::Blend;
        else if (alpha!="OPAQUE") return {ErrorCode::MissingCapability,"glTF","alpha mask unsupported"};
        status=validateMaterial(material); if (!status) return status;
        images_[materialCount_]=images;
        materials_[materialCount_++]=material;
    }
    auto meshes=root.find("meshes");
    if (!meshes.isArray() || !meshes.size() || meshes.size()>32) return bad("mesh capacity or shape");
    for (std::size_t m=0;m<meshes.size();++m) {
        auto primitives=meshes.at(m).find("primitives");
        if (!primitives.isArray() || !primitives.size()) return bad("primitives array");
        for (std::size_t p=0;p<primitives.size();++p) {
            auto primitive=primitives.at(p); std::uint32_t mode=4;
            if (primitive.find("mode").valid() && !integer(primitive,"mode",mode)) return bad("primitive mode");
            if (mode!=4 || primitive.find("targets").valid() || primitive.find("extensions").valid())
                return {ErrorCode::MissingCapability,"glTF","only static triangle primitives supported"};
            if (submeshCount_==submeshes_.size()) return bad("primitive capacity");
            auto attrs=primitive.find("attributes");
            if (!attrs.isObject() || attrs.find("JOINTS_0").valid() || attrs.find("WEIGHTS_0").valid())
                return {ErrorCode::MissingCapability,"glTF","skinning unsupported"};
            std::uint32_t positions{}, indices{};
            if (!integer(attrs,"POSITION",positions) || !integer(primitive,"indices",indices)) return bad("position/index accessors required");
            Access position{}, index{};
            status=accessor(root,bytes,positions,position); if (!status) return status;
            status=accessor(root,bytes,indices,index); if (!status) return status;
            if (position.componentType!=5126 || position.components!=3 || index.components!=1 || index.componentType==5126 ||
                position.count>kMaxVertices-vertexCount_ || index.count>kMaxIndices-indexCount_ || index.count%3)
                return bad("vertex/index format or capacity");
            auto base=static_cast<std::uint32_t>(vertexCount_);
            for (std::uint32_t v=0;v<position.count;++v)
                vertices_[vertexCount_+v]={{numberAt(position,v,0),numberAt(position,v,1),numberAt(position,v,2)},{0,1,0},{},{1,1,1,1}};
            for (std::string_view name:{"NORMAL","TEXCOORD_0","COLOR_0","TANGENT"}) if (attrs.find(name).valid()) {
                std::uint32_t at{}; Access attribute{};
                if (!integer(attrs,name,at)) return bad("attribute accessor index");
                status=accessor(root,bytes,at,attribute); if (!status) return status;
                unsigned components=name=="NORMAL"?3:name=="TEXCOORD_0"?2:4;
                if (attribute.count!=position.count || attribute.componentType!=5126 ||
                    (attribute.components!=components && !(name=="COLOR_0" && attribute.components==3)))
                    return bad("attribute count or format");
                for (std::uint32_t v=0;v<position.count;++v) {
                    auto& vertex=vertices_[vertexCount_+v];
                    if (name=="NORMAL") vertex.normal={numberAt(attribute,v,0),numberAt(attribute,v,1),numberAt(attribute,v,2)};
                    else if (name=="TEXCOORD_0") vertex.uv={numberAt(attribute,v,0),numberAt(attribute,v,1)};
                    else if (name=="COLOR_0") vertex.color={numberAt(attribute,v,0),numberAt(attribute,v,1),numberAt(attribute,v,2),attribute.components==4?numberAt(attribute,v,3):1};
                    else vertex.tangent={numberAt(attribute,v,0),numberAt(attribute,v,1),numberAt(attribute,v,2),numberAt(attribute,v,3)};
                }
            }
            for (std::uint32_t i=0;i<index.count;++i) {
                std::uint32_t value{}; auto ptr=index.bytes+std::size_t(i)*index.stride;
                unsigned size=index.componentType==5125?4:index.componentType==5123?2:1;
                std::memcpy(&value,ptr,size);
                if (value>=position.count) return bad("primitive index outside positions");
                indices_[indexCount_+i]=base+value;
            }
            std::uint32_t material=UINT32_MAX;
            if (primitive.find("material").valid() && (!integer(primitive,"material",material) || material>=materialCount_)) return bad("material index");
            if (material != UINT32_MAX &&
                (images_[material].baseColor != UINT32_MAX || images_[material].normal != UINT32_MAX ||
                 images_[material].metallicRoughness != UINT32_MAX) && !attrs.find("TEXCOORD_0").valid())
                return {ErrorCode::MissingCapability,"glTF","textured material requires UVs"};
            if (material != UINT32_MAX && images_[material].normal != UINT32_MAX) {
                if (!attrs.find("NORMAL").valid())
                    return {ErrorCode::MissingCapability,"glTF","normal mapping requires normals"};
                if (!attrs.find("TANGENT").valid()) {
                    std::array<std::uint32_t, kMaxIndices> localIndices{};
                    for (std::uint32_t i=0;i<index.count;++i) localIndices[i]=indices_[indexCount_+i]-base;
                    status=generateTangents({vertices_.data()+vertexCount_,position.count},
                                            {localIndices.data(),index.count});
                    if (!status) return status;
                }
            }
            submeshes_[submeshCount_]={static_cast<std::uint32_t>(indexCount_),index.count};
            primitiveMaterials_[submeshCount_++]=material;
            vertexCount_+=position.count; indexCount_+=index.count;
        }
    }
    status=validateMesh({{vertices_.data(),vertexCount_},{indices_.data(),indexCount_},{submeshes_.data(),submeshCount_}});
    if (!status) { vertexCount_=indexCount_=submeshCount_=materialCount_=0; }
    loaded_=static_cast<bool>(status);
    return status;
}
}
