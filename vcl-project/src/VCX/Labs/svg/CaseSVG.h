#pragma once

#include "Engine/GL/Frame.hpp"
#include "Engine/Async.hpp"
#include "Labs/Common/ICase.h"
#include "Labs/Common/ImageRGB.h"
#include "Assets/bundled.h"

using VCX::Labs::Common::ImageRGB;
namespace VCX::Labs::Project {

    class CaseSVG : public Common::ICase {
    public:
        CaseSVG();

        virtual std::string_view const GetName() override { return "SVG Renderer"; }
        
        virtual void OnSetupPropsUI() override;
        virtual Common::CaseRenderResult OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) override;
        virtual void OnProcessInput(ImVec2 const & pos) override;
        
        // 图像处理
        enum class FilterMode {
            Original = 0,
            Edge     = 1,
            Blur     = 2
        };
        FilterMode _filterMode = FilterMode::Original;
        void       SSAA(int scale, ImageRGB & down, ImageRGB & super);
        void       ApplySobel(ImageRGB & img);
        void       ApplyBlur(ImageRGB & img);

    private:

        Engine::GL::UniqueTexture2D _texture;

        bool              _enableZoom   = true;
        bool              _recompute    = true;
        int  _SVGIdx       = 0;// 当前选择的 SVG 文件索引
        int  _scale        = 1;// SSAA 倍率
        int  _windowWidth  = 0;// 渲染窗口大小
        int  _windowHeight = 0;
        int  _width        = 0;// 最终图像大小
        int  _height       = 0;
        //获取 SVG 文件名
        const std::string GetPath(std::size_t const i) const { return std::filesystem::path(Assets::ExampleSVGs[i]).filename().string(); }
    };
}
