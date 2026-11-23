#include <d3d11.h>
#include <dlfcn.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T>
struct Com {
    T* value = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (value) value->Release(); }
    T* operator->() const { return value; }
};

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char error[32];
        std::snprintf(error, sizeof(error), " HRESULT 0x%08x", static_cast<unsigned>(result));
        throw std::runtime_error(std::string(operation) + error);
    }
}

std::vector<char> ReadDxbc(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "DXBC", 4))
        throw std::runtime_error(std::string("Invalid DXBC file ") + path);
    return bytes;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: %s /path/to/libdxvk_d3d11.so quad.vs.dxbc quad.ps.dxbc\n", argv[0]);
        return 2;
    }
    try {
        const auto vertex = ReadDxbc(argv[2]), pixel = ReadDxbc(argv[3]);
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error(dlerror());
        const auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(dlsym(library, "D3D11CreateDevice"));
        if (!create) throw std::runtime_error("D3D11CreateDevice symbol missing");

        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> context;
        const auto started = std::chrono::steady_clock::now();
        const D3D_FEATURE_LEVEL required = D3D_FEATURE_LEVEL_11_0;
        D3D_FEATURE_LEVEL level{};
        Check(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &required, 1, D3D11_SDK_VERSION,
                     &device.value, &level, &context.value), "D3D11CreateDevice");

        Com<ID3D11VertexShader> vs;
        Com<ID3D11PixelShader> ps;
        Check(device->CreateVertexShader(vertex.data(), vertex.size(), nullptr, &vs.value), "CreateVertexShader");
        Check(device->CreatePixelShader(pixel.data(), pixel.size(), nullptr, &ps.value), "CreatePixelShader");

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Com<ID3D11Texture2D> target, readback, texture;
        Com<ID3D11RenderTargetView> rtv;
        Check(device->CreateTexture2D(&desc, nullptr, &target.value), "CreateTexture2D render target");
        Check(device->CreateRenderTargetView(target.value, nullptr, &rtv.value), "CreateRenderTargetView");

        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&desc, nullptr, &readback.value), "CreateTexture2D staging");

        desc.Width = 2;
        desc.Height = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.CPUAccessFlags = 0;
        constexpr std::array<uint8_t, 8> texels{255, 0, 0, 255, 0, 255, 0, 255};
        const D3D11_SUBRESOURCE_DATA upload{texels.data(), 8, 0};
        Com<ID3D11ShaderResourceView> srv;
        Check(device->CreateTexture2D(&desc, &upload, &texture.value), "CreateTexture2D upload");
        Check(device->CreateShaderResourceView(texture.value, nullptr, &srv.value), "CreateShaderResourceView");

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        Com<ID3D11SamplerState> sampler;
        Check(device->CreateSamplerState(&samplerDesc, &sampler.value), "CreateSamplerState");

        constexpr float clear[4] = {0, 0, 1, 1};
        const D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
        context->ClearRenderTargetView(rtv.value, clear);
        context->OMSetRenderTargets(1, &rtv.value, nullptr);
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vs.value, nullptr, 0);
        context->PSSetShader(ps.value, nullptr, 0);
        context->PSSetShaderResources(0, 1, &srv.value);
        context->PSSetSamplers(0, 1, &sampler.value);
        context->Draw(4, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(readback.value, target.value);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(readback.value, 0, D3D11_MAP_READ, 0, &mapped), "Map readback");
        const auto at = [&](unsigned x, unsigned y) {
            return static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch + x * 4;
        };
        constexpr std::array<uint8_t, 4> blue{0, 0, 255, 255};
        const bool valid = std::memcmp(at(1, 1), blue.data(), 4) == 0 &&
            std::memcmp(at(24, 32), texels.data(), 4) == 0 &&
            std::memcmp(at(40, 32), texels.data() + 4, 4) == 0;
        context->Unmap(readback.value, 0);
        if (!valid) throw std::runtime_error("Readback differs from the clear or quad texture");
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        UINT quality = 0;
        Check(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &quality),
              "CheckMultisampleQualityLevels");
        std::printf("Feature level 0x%x, blue clear and two quad texels verified, 4x D24S8 quality levels %u, startup and readback %.1f ms\n",
                    static_cast<unsigned>(level), quality, milliseconds);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
