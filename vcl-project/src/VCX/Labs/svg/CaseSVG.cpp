#include <algorithm>
#include <array>
#include <iostream>
#include <tinyxml2.h>
#include <chrono>

#include "Labs/svg/CaseSVG.h"
#include "Labs/Common/ImGuiHelper.h"
#include "Labs/svg/render.h"

namespace VCX::Labs::Project {

    CaseSVG::CaseSVG():
    _texture({ .MinFilter = Engine::GL::FilterMode::Linear, .MagFilter = Engine::GL::FilterMode::Nearest }){}

    void CaseSVG::OnSetupPropsUI() {
        ImGui::Checkbox("Zoom Tooltip", &_enableZoom);
        ImGui::Spacing();

        _recompute |= ImGui::SliderInt("SSAA Scale", &_scale, 1, 3);
        ImGui::Spacing();

        ImGui::Text("Filter Type");
        ImGui::Spacing();
        {
            bool selected = (_filterMode == FilterMode::Original);
            if (ImGui::Selectable("> Original", selected)) {
                _filterMode = FilterMode::Original;
                _recompute  = true;
            }
            ImGui::Spacing();
        }
        {
            bool selected = (_filterMode == FilterMode::Blur);
            if (ImGui::Selectable("> Blur", selected)) {
                _filterMode = FilterMode::Blur;
                _recompute  = true;
            }
            ImGui::Spacing();
        }
        {
            bool selected = (_filterMode == FilterMode::Edge);
            if (ImGui::Selectable("> Edge", selected)) {
                _filterMode = FilterMode::Edge;
                _recompute  = true;
            }
            ImGui::Spacing();
        }

        if (ImGui::BeginCombo("Pictures", GetPath(_SVGIdx).c_str())) {
            for (std::size_t i = 0; i < Assets::ExampleSVGs.size(); ++i) {
                bool selected = (i == _SVGIdx) ? true : false;
                ImGui::PushID(i);
                if (ImGui::Selectable(GetPath(i).c_str(), selected)) {
                    if (! selected) {
                        _SVGIdx    = i;
                        _recompute = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        std::string path      = GetPath(_SVGIdx);
        std::string save_path = path.substr(0, path.length() - 4) + ".png";
        Common::ImGuiHelper::SaveImage(save_path.c_str(), _texture, std::make_pair(_width, _height));
    }

    Common::CaseRenderResult CaseSVG::OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) {

        int width = ((int)desiredSize.first - 100) * _scale;
        int height = ((int)desiredSize.second - 100) * _scale;

        if (width != _windowWidth || height != _windowHeight) {
            _windowWidth = width;
            _windowHeight = height;
            _recompute = true;
        }

        if (_recompute) {
            _recompute = false;
            _width = width;
            _height = height;

            auto superTex { Common::CreatePureImageRGB(100, 100, { 1., 1., 1. }) };

            //渲染入口
            tinyxml2::XMLDocument doc;
            if (doc.LoadFile(std::filesystem::path(Assets::ExampleSVGs[_SVGIdx]).string().c_str())) {
                std::cerr << "File loading error!" << GetPath(_SVGIdx) << std::endl;
            }
            else {
                auto const * root = doc.FirstChildElement("svg");
                renderImage(superTex, root, _width, _height, false);
            }

            //图片处理
            _width /= _scale, _height /= _scale;
            auto downTex { Common::CreatePureImageRGB(_width, _height, { 1., 1., 1. }) };
            SSAA(_scale, downTex, superTex);
            switch (_filterMode) {
            case FilterMode::Original:
                break;
            case FilterMode::Edge:
                ApplySobel(downTex);
                break;
            case FilterMode::Blur:
                ApplyBlur(downTex);
                break;
            }
            _texture.Update(downTex);
        }

        return Common::CaseRenderResult {
            .Fixed     = true,
            .Image     = _texture,
            .ImageSize = { _width, _height }
        };
    }

    void CaseSVG::OnProcessInput(ImVec2 const & pos) {
        auto         window  = ImGui::GetCurrentWindow();
        bool         hovered = false;
        bool         held    = false;
        ImVec2 const delta   = ImGui::GetIO().MouseDelta;
        ImGui::ButtonBehavior(window->Rect(), window->GetID("##io"), &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
        if (held && delta.x != 0.f)
            ImGui::SetScrollX(window, window->Scroll.x - delta.x);
        if (held && delta.y != 0.f)
            ImGui::SetScrollY(window, window->Scroll.y - delta.y);
        if (_enableZoom && ! held && ImGui::IsItemHovered())
            Common::ImGuiHelper::ZoomTooltip(_texture, { _width, _height}, pos);
    }

    //  add edge filter after loading SVG files
    void CaseSVG::ApplySobel(ImageRGB & img) {
        std::size_t width    = img.GetSizeX();
        std::size_t height   = img.GetSizeY();
        float       gx[3][3] = {
            { -1, 0, 1 },
            { -2, 0, 2 },
            { -1, 0, 1 }
        };
        float gy[3][3] = {
            {  1,  2,  1 },
            {  0,  0,  0 },
            { -1, -2, -1 }
        };

        ImageRGB padded(width + 2, height + 2);
        padded.Fill({ 0.0f, 0.0f, 0.0f });
        for (std::size_t x = 1; x < width + 1; x++) {
            for (std::size_t y = 1; y < height + 1; y++) {
                glm::vec3 tmpc  = img.At(x - 1, y - 1);
                padded.At(x, y) = tmpc;
            }
        }

        ImageRGB output(width, height);
        output.Fill({ 0.0f, 0.0f, 0.0f });
        for (std::size_t x = 1; x < width + 1; x++) {
            for (std::size_t y = 1; y < height + 1; y++) {
                glm::vec3 grad_x(0.0f);
                glm::vec3 grad_y(0.0f);

                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        glm::vec3 color = padded.At(x + dx, y + dy);
                        float     kx    = gx[dx + 1][dy + 1];
                        float     ky    = gy[dx + 1][dy + 1];
                        grad_x += color * kx;
                        grad_y += color * ky;
                    }
                }

                glm::vec3 mag           = glm::sqrt(grad_x * grad_x + grad_y * grad_y);
                output.At(x - 1, y - 1) = mag;
            }
        }
        img = output;
    }

    // add blur filter after loading SVG files
    void CaseSVG::ApplyBlur(ImageRGB & img) {
        ImageRGB    output = img;
        std::size_t width  = img.GetSizeX();
        std::size_t height = img.GetSizeY();

        ImageRGB padded(width + 2, height + 2);
        padded.Fill({ 0.0f, 0.0f, 0.0f });
        for (std::size_t x = 1; x < width + 1; x++) {
            for (std::size_t y = 1; y < height + 1; y++) {
                glm::vec3 tcolor = img.At(x - 1, y - 1);
                padded.At(x, y)  = tcolor;
            }
        }
        for (std::size_t x = 1; x < width + 1; x++) {
            for (std::size_t y = 1; y < height + 1; y++) {
                glm::vec3 sum(0.0f);
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        sum += glm::vec3(padded.At(x + dx, y + dy));
                    }
                }
                output.At(x - 1, y - 1) = sum / 9.0f;
            }
        }
        img = output;
    }

    //  add anti-aliasing
    void CaseSVG::SSAA(int scale, ImageRGB & down, ImageRGB & super) {
        std::size_t width  = down.GetSizeX();
        std::size_t height = down.GetSizeY();

        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                glm::vec3 color(0.0f);

                for (std::size_t dy = 0; dy < scale; ++dy) {
                    for (std::size_t dx = 0; dx < scale; ++dx) {
                        auto && p = super.At(y * scale + dy, x * scale + dx);
                        color += glm::vec3(p);
                    }
                }
                color /= float(scale * scale);
                down.At(x, y) = color;
            }
        }
    }
}
