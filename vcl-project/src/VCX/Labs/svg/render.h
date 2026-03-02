#pragma once

#include "Labs/Common/ImageRGB.h"
#include <tinyxml2.h>

using VCX::Labs::Common::ImageRGB;

namespace VCX::Labs::Project {

    void renderImage(
        ImageRGB &                   image,
        const tinyxml2::XMLElement * svgRoot,
        int &                        outWidth,
        int &                        outHeight,
        bool                         skipDrawing);

} 