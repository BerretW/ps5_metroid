#include "echo/framework/gltf.hpp"
#include <cstdio>
#include <cstring>

int main() {
    using namespace echo::rendering;
    const float vertices[]{-1,0,0, 0,0,1, 0,0,
                          0,1,0, 0,0,1, 0.5F,1,
                          1,0,0, 0,0,1, 1,0};
    const std::uint16_t indices[]{0,1,2};
    std::array<std::byte,102> buffer{};
    std::memcpy(buffer.data(),vertices,sizeof(vertices));
    std::memcpy(buffer.data()+96,indices,sizeof(indices));
    constexpr std::string_view json=R"({
      "asset":{"version":"2.0"},
      "buffers":[{"byteLength":102,"uri":"caller-resolved.bin"}],
      "bufferViews":[{"buffer":0,"byteLength":96,"byteStride":32},{"buffer":0,"byteOffset":96,"byteLength":6}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                   {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
                   {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
                   {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"}],
      "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.2,1,1],"metallicFactor":0.25,"roughnessFactor":0.7}}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":2,"TEXCOORD_0":3},"indices":1,"material":0}]}]
    })";
    GltfMesh mesh;
    echo::framework::JsonError parseError{};
    auto status=mesh.parse(json,buffer,parseError);
    if (!status || mesh.mesh().vertices.size()!=3 || mesh.mesh().indices[2]!=2 ||
        mesh.materials().size()!=1 || mesh.materials()[0].metallic!=0.25F ||
        mesh.primitiveMaterials()[0]!=0 || mesh.mesh().vertices[1].normal.z!=1 ||
        mesh.mesh().vertices[1].uv.x!=0.5F) { std::fprintf(stderr,"Valid glTF import failed\n"); return 1; }
    status=mesh.parse(json,std::span<const std::byte>(buffer).first(101),parseError);
    if (status || !mesh.mesh().vertices.empty()) { std::fprintf(stderr,"Truncated glTF buffer exposed geometry\n"); return 1; }
    buffer[100]=std::byte{3};
    status=mesh.parse(json,buffer,parseError);
    if (status || !mesh.materials().empty()) { std::fprintf(stderr,"Invalid glTF index accepted\n"); return 1; }
    status=mesh.parse(R"({"asset":{"version":"1.0"}})",buffer,parseError);
    if (status.code!=ErrorCode::UnsupportedVersion) { std::fprintf(stderr,"Unsupported glTF version accepted\n"); return 1; }
    std::puts("glTF buffer bounds, geometry, material and failure-state tests PASS");
    return 0;
}
