#pragma once

#include "render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

# define M_PI 3.14159265358979323846

namespace VCX::Labs::Project {

    namespace {

        // 渲染上下文
        struct SvgRenderContext {
            ImageRGB * canvas = nullptr;
            int targetWidth  = 0;
            int targetHeight = 0;
            float scale = 1.0f;// SVG空间到像素空间的缩放比例
            glm::vec2 svgOrigin = glm::vec2(0.0f);// SVG 空间中的偏移（用于处理 viewBox）
            glm::vec2 pxOffset = glm::vec2(0.0f);// 像素空间中的偏移（用于居中）
            bool skip = false;
        };

        // 工具函数：字符串与数值解析
        bool isNumberChar(char c) {
            return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
        }
        // 解析一串 "1, 2.3 -4 5" 之类的浮点数
        std::vector<float> parseFloatList(const char *& s, int maxCount = std::numeric_limits<int>::max()) {
            std::vector<float> result;
            int                start    = 0;
            bool               inNumber = false;
            bool               hasDot   = false;

            for (int i = 0;; ++i) {
                char c          = s[i];
                bool numberChar = isNumberChar(c);

                if (numberChar) {
                    if (! inNumber) {
                        inNumber = true;
                        start    = i;
                        hasDot   = (c == '.');
                    } else if (c == '.' && hasDot) {
                        // 遇到第二个 '.'认为前面一个数字结束
                        std::string token(s + start, s + i);
                        result.push_back(std::stof(token));
                        if ((int) result.size() >= maxCount) {
                            s += i;
                            return result;
                        }
                        start  = i;
                        hasDot = true;
                    } else if (c == '.') {
                        hasDot = true;
                    }
                } else if (inNumber) {
                    // 非数值字符，结束当前数字
                    std::string token(s + start, s + i);
                    result.push_back(std::stof(token));
                    inNumber = false;
                    hasDot   = false;
                    if ((int) result.size() >= maxCount || c == '\0') {
                        s += i;
                        break;
                    }
                }
                if (c == '\0') {
                    if (inNumber) {
                        std::string token(s + start, s + i);
                        result.push_back(std::stof(token));
                    }
                    s += i;
                    break;
                }
            }
            return result;
        }

        std::vector<glm::vec2> parsePointList(const char * s) {
            const char *           ptr  = s;
            auto                   nums = parseFloatList(ptr);
            std::vector<glm::vec2> pts;
            pts.reserve(nums.size() / 2);
            for (size_t i = 0; i + 1 < nums.size(); i += 2) {
                pts.emplace_back(nums[i], nums[i + 1]);
            }
            return pts;
        }

        // 颜色解析
        // 十六进制表示
        glm::vec3 parseHexColor(const char * hex) {
            if (! hex) return glm::vec3(0.0f);

            std::string s(hex);
            for (char c : s) {
                if (! std::isxdigit(static_cast<unsigned char>(c))) {
                    std::cerr << "Invalid hex color: " << s << std::endl;
                    return glm::vec3(0.0f);
                }
            }

            if (s.size() != 3 && s.size() != 6) {
                std::cerr << "Unsupported hex color length: " << s << std::endl;
                return glm::vec3(0.0f);
            }

            int  step     = (s.size() == 3) ? 1 : 2;
            auto readComp = [&](int idx) -> int {
                if (step == 1) {
                    std::string t(2, s[idx]);
                    return std::stoi(t, nullptr, 16);
                } else {
                    return std::stoi(s.substr(idx, 2), nullptr, 16);
                }
            };

            float scale = 255.0f;
            int   r     = readComp(0 * step);
            int   g     = readComp(1 * step);
            int   b     = readComp(2 * step);

            return glm::vec3(r / scale, g / scale, b / scale);
        }
        //主颜色解析
        glm::vec3 parseNamedOrRgbColor(const char * text) {
            if (! text) return glm::vec3(0.0f);

            if (text[0] == '#') {
                return parseHexColor(text + 1);
            }

            if (std::strncmp(text, "rgb", 3) == 0) {
                const char * ptr   = text;
                auto         comps = parseFloatList(ptr);
                if (comps.size() >= 3) {
                    return glm::vec3(
                        comps[0] / 255.0f,
                        comps[1] / 255.0f,
                        comps[2] / 255.0f);
                }
            }

            static const std::unordered_map<std::string, glm::vec3> kNamed = {
                {  "black", parseHexColor("000000") },
                {   "gray", parseHexColor("808080") },
                {  "white", parseHexColor("ffffff") },
                {    "red", parseHexColor("ff0000") },
                {  "green", parseHexColor("008000") },
                { "yellow", parseHexColor("ffff00") },
                {   "blue", parseHexColor("0000ff") },
                { "orange", parseHexColor("ffa500") }
            };

            auto it = kNamed.find(text);
            if (it != kNamed.end()) {
                return it->second;
            }

            std::cerr << "Unknown color string: " << text << std::endl;
            return glm::vec3(0.0f);
        }

        glm::vec4 extractPaint(const tinyxml2::XMLElement * ele, const char * attrName) {
            const char * val = ele->Attribute(attrName);

            if (! val) {
                if (std::strcmp(attrName, "stroke") == 0) {
                    return glm::vec4(0.0f); // no stroke by default
                } else if (std::strcmp(attrName, "fill") == 0) {
                    val = "black";
                } else {
                    return glm::vec4(0.0f);
                }
            }

            if (std::strcmp(val, "none") == 0 || std::strcmp(val, "transparent") == 0) {
                return glm::vec4(0.0f);
            }

            glm::vec3   rgb    = parseNamedOrRgbColor(val);
            std::string opName = std::string(attrName) + "-opacity";
            float       alpha  = ele->FloatAttribute(opName.c_str(), 1.0f);

            return glm::vec4(rgb, alpha);
        }

        // 坐标变换 & 最终像素绘制

        glm::vec2 svgToPixel(const SvgRenderContext & ctx, glm::vec2 svgPos) {
            // 先移除 svgOrigin，再乘缩放，最后加像素偏移
            glm::vec2 local = (svgPos - ctx.svgOrigin) / ctx.scale;
            return local + ctx.pxOffset;
        }

        void putPixel(const SvgRenderContext & ctx, glm::ivec2 p, glm::vec4 color) {
            if (! ctx.canvas || ctx.skip) return;

            if (p.x < 0 || p.y < 0 || p.x >= ctx.targetWidth || p.y >= ctx.targetHeight) {
                return;
            }

            auto &&   dst    = ctx.canvas->At(static_cast<size_t>(p.y), static_cast<size_t>(p.x));
            glm::vec3 srcRgb = glm::vec3(color);
            glm::vec3 dstRgb = static_cast<glm::vec3>(dst);
            float     a      = color.a;
            dst              = srcRgb * a + dstRgb * (1.0f - a);
        }

        void drawPixelInSvgSpace(const SvgRenderContext & ctx, glm::vec2 svgPos, glm::vec4 color) {
            glm::vec2 pp = svgToPixel(ctx, svgPos);
            putPixel(ctx, glm::ivec2(std::round(pp.x), std::round(pp.y)), color);
        }

        // 多边形填充（按 y 扫描）
        struct Edge {
            float x0, y0;
            float x1, y1;
            int   winding;
        };

        void buildEdgesFromPolygon(const std::vector<glm::vec2> & poly, std::vector<Edge> & edges) {
            if (poly.size() < 2) return;
            size_t count = poly.size();
            for (size_t i = 0; i + 1 < count; ++i) {
                glm::vec2 a = poly[i];
                glm::vec2 b = poly[i + 1];
                if (a.y == b.y) continue; 

                Edge e;
                if (a.y < b.y) {
                    e.x0      = a.x;
                    e.y0      = a.y;
                    e.x1      = b.x;
                    e.y1      = b.y;
                    e.winding = +1;
                } else {
                    e.x0      = b.x;
                    e.y0      = b.y;
                    e.x1      = a.x;
                    e.y1      = a.y;
                    e.winding = -1;
                }
                edges.push_back(e);
            }
        }

        void fillPolygons(
            const SvgRenderContext &                    ctx,
            glm::vec4                                   color,
            const std::vector<std::vector<glm::vec2>> & polys,
            int                                         rule) {
            std::vector<Edge> edges;
            edges.reserve(polys.size() * 4);

            float ymin = std::numeric_limits<float>::max();
            float ymax = std::numeric_limits<float>::lowest();

            for (const auto & poly : polys) {
                if (poly.size() < 2) continue;
                std::vector<glm::vec2> closed = poly;
                if (closed.front() != closed.back()) {
                    closed.push_back(closed.front());
                }

                buildEdgesFromPolygon(closed, edges);

                for (auto & p : closed) {
                    ymin = std::min(ymin, p.y);
                    ymax = std::max(ymax, p.y);
                }
            }

            if (edges.empty()) return;

            int y0 = std::max(0, static_cast<int>(std::floor(ymin)));
            int y1 = std::min(ctx.targetHeight - 1, static_cast<int>(std::ceil(ymax)));

            for (int iy = y0; iy <= y1; ++iy) {
                float scanY = static_cast<float>(iy) + 0.5f;

                std::vector<std::pair<float, int>> crossings;
                for (auto & e : edges) {
                    if (scanY < e.y0 || scanY >= e.y1) continue;
                    float t = (scanY - e.y0) / (e.y1 - e.y0);
                    float x = e.x0 + (e.x1 - e.x0) * t;
                    crossings.emplace_back(x, e.winding);
                }
                if (crossings.empty()) continue;

                std::sort(crossings.begin(), crossings.end(), [](auto & a, auto & b) { return a.first < b.first; });

                if (rule == 0) { 
                    int   windingCount = 0;
                    float segStart     = 0.0f;
                    bool  inside       = false;

                    for (size_t i = 0; i < crossings.size(); ++i) {
                        windingCount += crossings[i].second;
                        if (! inside && windingCount != 0) {
                            segStart = crossings[i].first;
                            inside   = true;
                        } else if (inside && windingCount == 0) {
                            float segEnd = crossings[i].first;
                            int   x0     = std::max(0, static_cast<int>(std::ceil(segStart)));
                            int   x1     = std::min(ctx.targetWidth - 1, static_cast<int>(std::floor(segEnd)));
                            for (int ix = x0; ix <= x1; ++ix) {
                                putPixel(ctx, { ix, iy }, color);
                            }
                            inside = false;
                        }
                    }
                } else { 
                    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
                        float xStart = crossings[i].first;
                        float xEnd   = crossings[i + 1].first;
                        int   x0     = std::max(0, static_cast<int>(std::ceil(xStart)));
                        int   x1     = std::min(ctx.targetWidth - 1, static_cast<int>(std::floor(xEnd)));
                        for (int ix = x0; ix <= x1; ++ix) {
                            putPixel(ctx, { ix, iy }, color);
                        }
                    }
                }
            }
        }

        // 描边：简单膨胀折线 + 填充
        void strokePolyline(
            const SvgRenderContext &       ctx,
            glm::vec4                      color,
            const std::vector<glm::vec2> & polyline,
            float                          halfWidth) {
            if (polyline.size() < 2 || halfWidth <= 0.0f) return;

            // 构造左右偏移线，然后作为一个闭合多边形去填充
            std::vector<glm::vec2> left, right;
            left.reserve(polyline.size());
            right.reserve(polyline.size());

            for (size_t i = 0; i + 1 < polyline.size(); ++i) {
                glm::vec2 p0  = polyline[i];
                glm::vec2 p1  = polyline[i + 1];
                glm::vec2 dir = p1 - p0;
                float     len = glm::length(dir);
                if (len < 1e-4f) continue;
                glm::vec2 n(-dir.y / len, dir.x / len);
                left.push_back(p0 + n * halfWidth);
                right.push_back(p0 - n * halfWidth);
                if (i + 1 == polyline.size() - 1) {
                    left.push_back(p1 + n * halfWidth);
                    right.push_back(p1 - n * halfWidth);
                }
            }

            if (left.empty() || right.empty()) return;

            std::vector<glm::vec2> strokePoly;
            strokePoly.reserve(left.size() + right.size());
            strokePoly.insert(strokePoly.end(), left.begin(), left.end());
            strokePoly.insert(strokePoly.end(), right.rbegin(), right.rend());
            if (strokePoly.front() != strokePoly.back()) {
                strokePoly.push_back(strokePoly.front());
            }

            fillPolygons(ctx, color, { strokePoly }, 0);
        }

        // 圆 
        void fillRing(const SvgRenderContext & ctx, glm::vec4 color, glm::vec2 center, float outerR, float innerR) {
            if (outerR <= 0.0f) return;
            if (innerR < 0.0f) innerR = 0.0f;
            if (innerR > outerR) std::swap(innerR, outerR);

            int minX = std::max(0, static_cast<int>(std::floor(center.x - outerR)));
            int maxX = std::min(ctx.targetWidth - 1, static_cast<int>(std::ceil(center.x + outerR)));

            for (int ix = minX; ix <= maxX; ++ix) {
                float dx    = ix - center.x;
                float dist2 = dx * dx;
                if (dist2 > outerR * outerR) continue;

                float outerDy = std::sqrt(outerR * outerR - dist2);
                float yTop    = center.y - outerDy;
                float yBottom = center.y + outerDy;

                float innerTop    = yTop;
                float innerBottom = yTop;
                if (dist2 <= innerR * innerR) {
                    float innerDy = std::sqrt(innerR * innerR - dist2);
                    innerTop      = center.y - innerDy;
                    innerBottom   = center.y + innerDy;
                }

                int y0 = std::max(0, static_cast<int>(std::ceil(yTop)));
                int y1 = std::min(ctx.targetHeight - 1, static_cast<int>(std::floor(innerTop)));
                for (int iy = y0; iy <= y1; ++iy) {
                    putPixel(ctx, { ix, iy }, color);
                }

                int y2 = std::max(0, static_cast<int>(std::ceil(innerBottom)));
                int y3 = std::min(ctx.targetHeight - 1, static_cast<int>(std::floor(yBottom)));
                for (int iy = y2; iy <= y3; ++iy) {
                    putPixel(ctx, { ix, iy }, color);
                }
            }
        }

        // 贝塞尔、椭圆
        void sampleCubicBezier(
            std::vector<glm::vec2> & pts,
            glm::vec2                p0,
            glm::vec2                p1,
            glm::vec2                p2,
            glm::vec2                p3,
            float                    step = 0.02f) {
            for (float t = 0.0f; t <= 1.0f + 1e-4f; t += step) {
                float     u = 1.0f - t;
                glm::vec2 p = u * u * u * p0
                    + 3.0f * u * u * t * p1
                    + 3.0f * u * t * t * p2
                    + t * t * t * p3;
                pts.push_back(p);
            }
        }

        void sampleQuadraticBezier(
            std::vector<glm::vec2> & pts,
            glm::vec2                p0,
            glm::vec2                p1,
            glm::vec2                p2,
            float                    step = 0.04f) {
            for (float t = 0.0f; t <= 1.0f + 1e-4f; t += step) {
                float     u = 1.0f - t;
                glm::vec2 p = u * u * p0 + 2.0f * u * t * p1 + t * t * p2;
                pts.push_back(p);
            }
        }

        glm::vec2 rotate2D(glm::vec2 p, float angle) {
            float c = std::cos(angle), s = std::sin(angle);
            return glm::vec2(p.x * c - p.y * s, p.x * s + p.y * c);
        }

        void subdivideArc(
            glm::vec2                center,
            float                    startAngle,
            float                    endAngle,
            float                    threshold,
            std::vector<glm::vec2> & out) {
            float delta = endAngle - startAngle;
            if (std::abs(delta) <= threshold) {
                out.emplace_back(std::cos(endAngle), std::sin(endAngle));
                return;
            }
            float mid = 0.5f * (startAngle + endAngle);
            subdivideArc(center, startAngle, mid, threshold, out);
            subdivideArc(center, mid, endAngle, threshold, out);
        }

        void sampleSvgArc(
            std::vector<glm::vec2> & outPoints,
            glm::vec2                p0,
            glm::vec2                p1,
            float                    rx,
            float                    ry,
            float                    xAxisRotation,
            int                      largeArcFlag,
            int                      sweepFlag,
            float                    approxEps) {
            if (rx <= 0.0f || ry <= 0.0f) {
                outPoints.push_back(p1);
                return;
            }

            float phi = glm::radians(xAxisRotation);

            // 坐标变换到椭圆标准空间
            glm::vec2 dp    = 0.5f * (p0 - p1);
            glm::vec2 dpRot = rotate2D(dp, -phi);
            float     x1p   = dpRot.x / rx;
            float     y1p   = dpRot.y / ry;

            float lambda = x1p * x1p + y1p * y1p;
            if (lambda > 1.0f) {
                float scale = std::sqrt(lambda);
                rx *= scale;
                ry *= scale;
                x1p /= scale;
                y1p /= scale;
            }

            // 求圆弧中心
            float sign   = (largeArcFlag == sweepFlag) ? -1.0f : 1.0f;
            float factor = sign * std::sqrt(std::max(0.0f, (1.0f - lambda) / lambda));
            float cxp    = factor * y1p;
            float cyp    = -factor * x1p;

            glm::vec2 center = rotate2D(glm::vec2(cxp * rx, cyp * ry), phi) + 0.5f * (p0 + p1);

            // 起始角、终止角
            glm::vec2 v1 = glm::vec2((x1p - cxp), (y1p - cyp));
            glm::vec2 v2 = glm::vec2((-x1p - cxp), (-y1p - cyp));

            float theta1 = std::atan2(v1.y, v1.x);
            float theta2 = std::atan2(v2.y, v2.x);

            float dtheta = theta2 - theta1;
            if (! sweepFlag && dtheta > 0) {
                dtheta -= 2.0f * glm::pi<float>();
            } else if (sweepFlag && dtheta < 0) {
                dtheta += 2.0f * glm::pi<float>();
            }

            float endAngle = theta1 + dtheta;

            // 递归
            std::vector<glm::vec2> circlePts;
            subdivideArc(glm::vec2(0.0f), theta1, endAngle, approxEps, circlePts);

            // 回到原始坐标
            for (auto & u : circlePts) {
                glm::vec2 e(rx * u.x, ry * u.y);
                glm::vec2 p = rotate2D(e, phi) + center;
                outPoints.push_back(p);
            }
        }

        // 各种 SVG 元素绘制
        void renderPolygonElement(const SvgRenderContext & ctx, const tinyxml2::XMLElement * ele) {
            const char * ptsAttr = ele->Attribute("points");
            if (! ptsAttr) return;

            auto svgPts = parsePointList(ptsAttr);
            if (svgPts.size() < 3) return;

            // 转成像素坐标
            std::vector<glm::vec2> pixPts;
            pixPts.reserve(svgPts.size());
            for (auto & p : svgPts) {
                pixPts.push_back(svgToPixel(ctx, p));
            }

            glm::vec4    fillColor   = extractPaint(ele, "fill");
            const char * fillRuleStr = ele->Attribute("fill-rule");
            int          rule        = (fillRuleStr && std::strcmp(fillRuleStr, "evenodd") == 0) ? 1 : 0;

            if (fillColor.a > 0.0f) {
                fillPolygons(ctx, fillColor, { pixPts }, rule);
            }

            glm::vec4 strokeColor = extractPaint(ele, "stroke");
            if (strokeColor.a > 0.0f) {
                float strokeWidth = ele->FloatAttribute("stroke-width", 1.0f);
                strokePolyline(ctx, strokeColor, pixPts, strokeWidth * 0.5f / ctx.scale);
                // 【error】strokeWidth 是 SVG 空间的宽度，换成像素要除以 scale
            }
        }

        void renderCircleElement(const SvgRenderContext & ctx, const tinyxml2::XMLElement * ele) {
            float cx = ele->FloatAttribute("cx", 0.0f);
            float cy = ele->FloatAttribute("cy", 0.0f);
            float r  = ele->FloatAttribute("r", 0.0f);
            if (r <= 0.0f) return;

            glm::vec2 svgCenter(cx, cy);
            glm::vec2 pixCenter = svgToPixel(ctx, svgCenter);
            float     pixR      = r / ctx.scale; 

            glm::vec4 fillColor = extractPaint(ele, "fill");
            if (fillColor.a > 0.0f) {
                fillRing(ctx, fillColor, pixCenter, pixR, 0.0f);
            }

            glm::vec4 strokeColor = extractPaint(ele, "stroke");
            if (strokeColor.a > 0.0f) {
                float strokeWidth = ele->FloatAttribute("stroke-width", 1.0f);
                float half        = (strokeWidth * 0.5f) / ctx.scale;
                fillRing(ctx, strokeColor, pixCenter, pixR + half, pixR - half);
            }
        }

        void renderRectElement(const SvgRenderContext & ctx, const tinyxml2::XMLElement * ele) {
            float x = ele->FloatAttribute("x", 0.0f);
            float y = ele->FloatAttribute("y", 0.0f);
            float w = 0.0f, h = 0.0f;
            if (ele->QueryFloatAttribute("width", &w) != tinyxml2::XML_SUCCESS) return;
            if (ele->QueryFloatAttribute("height", &h) != tinyxml2::XML_SUCCESS) return;

            float rx = ele->FloatAttribute("rx", -1.0f);
            float ry = ele->FloatAttribute("ry", -1.0f);
            if (rx < 0 && ry >= 0) rx = ry;
            if (ry < 0 && rx >= 0) ry = rx;
            if (rx < 0 || ry < 0) {
                rx = ry = 0.0f;
            }

            // 矩形多边形
            std::vector<glm::vec2> poly;
            if (rx == 0.0f && ry == 0.0f) {
                poly = {
                    {     x,     y },
                    { x + w,     y },
                    { x + w, y + h },
                    {     x, y + h },
                    {     x,     y }
                };
            } else {
                // 用4个圆角的多段线近似（每个角用几个点采样）
                int   cornerSamples = 6;
                float cx[4]         = { x + rx, x + w - rx, x + w - rx, x + rx };
                float cy[4]         = { y + ry, y + ry, y + h - ry, y + h - ry };
                float start[4]      = { M_PI, -M_PI / 2.0, 0.0, M_PI / 2.0 };
                float end[4]        = { 3 * M_PI / 2.0, 0.0, M_PI / 2.0, M_PI };

                for (int k = 0; k < 4; ++k) {
                    for (int i = 0; i <= cornerSamples; ++i) {
                        float t   = (float) i / (float) cornerSamples;
                        float ang = start[k] + t * (end[k] - start[k]);
                        float px  = cx[k] + rx * std::cos(ang);
                        float py  = cy[k] + ry * std::sin(ang);
                        if (poly.empty() || glm::length(poly.back() - glm::vec2(px, py)) > 1e-4f)
                            poly.emplace_back(px, py);
                    }
                }
                if (! poly.empty() && poly.front() != poly.back()) {
                    poly.push_back(poly.front());
                }
            }
            std::vector<glm::vec2> pixPoly;
            pixPoly.reserve(poly.size());
            for (auto & p : poly) {
                pixPoly.push_back(svgToPixel(ctx, p));
            }

            glm::vec4 fillColor = extractPaint(ele, "fill");
            if (fillColor.a > 0.0f) {
                fillPolygons(ctx, fillColor, { pixPoly }, 0);
            }

            glm::vec4 strokeColor = extractPaint(ele, "stroke");
            if (strokeColor.a > 0.0f) {
                float strokeWidth = ele->FloatAttribute("stroke-width", 1.0f);
                strokePolyline(ctx, strokeColor, pixPoly, strokeWidth * 0.5f / ctx.scale);
            }
        }

        void renderPathElement(const SvgRenderContext & ctx, const tinyxml2::XMLElement * ele) {
            const char * d = ele->Attribute("d");
            if (! d) return;

            std::vector<std::vector<glm::vec2>> subPaths;
            std::vector<glm::vec2>              currentPath;

            glm::vec2 cur(0.0f), lastControl(0.0f);
            char      prevCmd = 0;

            const char * p = d;
            while (*p) {
                while (std::isspace(static_cast<unsigned char>(*p))) ++p;
                if (*p == '\0') break;

                char cmd = *p;
                if (std::isalpha(static_cast<unsigned char>(cmd))) {
                    ++p;
                } else {
                    cmd = prevCmd;
                }
                prevCmd       = cmd;
                bool relative = (cmd >= 'a' && cmd <= 'z');
                char uc       = static_cast<char>(std::toupper(cmd));

                if (uc == 'M') {
                    auto nums = parseFloatList(p, 2);
                    if (nums.size() < 2) break;
                    glm::vec2 pt(nums[0], nums[1]);
                    if (relative) pt += cur;
                    cur = pt;
                    if (! currentPath.empty()) {
                        subPaths.push_back(currentPath);
                        currentPath.clear();
                    }
                    currentPath.push_back(cur);
                } else if (uc == 'L') {
                    auto nums = parseFloatList(p, 2);
                    if (nums.size() < 2) break;
                    glm::vec2 pt(nums[0], nums[1]);
                    if (relative) pt += cur;
                    cur = pt;
                    currentPath.push_back(cur);
                } else if (uc == 'H') {
                    auto nums = parseFloatList(p, 1);
                    if (nums.empty()) break;
                    float x = nums[0];
                    if (relative) x += cur.x;
                    cur.x = x;
                    currentPath.push_back(cur);
                } else if (uc == 'V') {
                    auto nums = parseFloatList(p, 1);
                    if (nums.empty()) break;
                    float y = nums[0];
                    if (relative) y += cur.y;
                    cur.y = y;
                    currentPath.push_back(cur);
                } else if (uc == 'Z') {
                    if (! currentPath.empty()) {
                        if (currentPath.front() != currentPath.back())
                            currentPath.push_back(currentPath.front());
                    }
                } else if (uc == 'C') {
                    auto nums = parseFloatList(p, 6);
                    if (nums.size() < 6) break;
                    glm::vec2 c1(nums[0], nums[1]);
                    glm::vec2 c2(nums[2], nums[3]);
                    glm::vec2 p2(nums[4], nums[5]);
                    if (relative) {
                        c1 += cur;
                        c2 += cur;
                        p2 += cur;
                    }
                    sampleCubicBezier(currentPath, cur, c1, c2, p2);
                    cur         = p2;
                    lastControl = c2;
                } else if (uc == 'S') {
                    auto nums = parseFloatList(p, 4);
                    if (nums.size() < 4) break;
                    glm::vec2 c2(nums[0], nums[1]);
                    glm::vec2 p2(nums[2], nums[3]);
                    if (relative) {
                        c2 += cur;
                        p2 += cur;
                    }
                    glm::vec2 c1 = cur;
                    if (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's') {
                        c1 = cur + (cur - lastControl);
                    }
                    sampleCubicBezier(currentPath, cur, c1, c2, p2);
                    cur         = p2;
                    lastControl = c2;
                } else if (uc == 'Q') {
                    auto nums = parseFloatList(p, 4);
                    if (nums.size() < 4) break;
                    glm::vec2 c1(nums[0], nums[1]);
                    glm::vec2 p2(nums[2], nums[3]);
                    if (relative) {
                        c1 += cur;
                        p2 += cur;
                    }
                    sampleQuadraticBezier(currentPath, cur, c1, p2);
                    cur         = p2;
                    lastControl = c1;
                } else if (uc == 'T') {
                    auto nums = parseFloatList(p, 2);
                    if (nums.size() < 2) break;
                    glm::vec2 p2(nums[0], nums[1]);
                    if (relative) p2 += cur;
                    glm::vec2 c1 = cur;
                    if (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't') {
                        c1 = cur + (cur - lastControl);
                    }
                    sampleQuadraticBezier(currentPath, cur, c1, p2);
                    cur         = p2;
                    lastControl = c1;
                } else if (uc == 'A') {
                    auto nums = parseFloatList(p, 7);
                    if (nums.size() < 7) break;
                    float     rx       = nums[0];
                    float     ry       = nums[1];
                    float     rot      = nums[2];
                    int       largeArc = static_cast<int>(nums[3]);
                    int       sweep    = static_cast<int>(nums[4]);
                    glm::vec2 p2(nums[5], nums[6]);
                    if (relative) p2 += cur;

                    std::vector<glm::vec2> arcPts;
                    sampleSvgArc(arcPts, cur, p2, rx, ry, rot, largeArc, sweep, /*approxEps=*/0.2f);
                    for (auto & pt : arcPts) {
                        currentPath.push_back(pt);
                    }
                    cur = p2;
                } else {
                    ++p;
                }
            }

            if (! currentPath.empty()) {
                subPaths.push_back(currentPath);
            }

            // 把所有path点从SVG空间转换到像素空间
            std::vector<std::vector<glm::vec2>> pixelPaths;
            pixelPaths.reserve(subPaths.size());
            for (auto & sp : subPaths) {
                std::vector<glm::vec2> pix;
                pix.reserve(sp.size());
                for (auto & pt : sp) {
                    pix.push_back(svgToPixel(ctx, pt));
                }
                pixelPaths.push_back(std::move(pix));
            }

            // 填充
            glm::vec4    fillColor   = extractPaint(ele, "fill");
            const char * fillRuleStr = ele->Attribute("fill-rule");
            int          rule        = (fillRuleStr && std::strcmp(fillRuleStr, "evenodd") == 0) ? 1 : 0;

            if (fillColor.a > 0.0f) {
                fillPolygons(ctx, fillColor, pixelPaths, rule);
            }

            // 描边
            glm::vec4 strokeColor = extractPaint(ele, "stroke");
            if (strokeColor.a > 0.0f) {
                float strokeWidth = ele->FloatAttribute("stroke-width", 1.0f);
                float half        = strokeWidth * 0.5f / ctx.scale;
                for (auto & sp : pixelPaths) {
                    strokePolyline(ctx, strokeColor, sp, half);
                }
            }
        }

        // DOM 遍历与调度
        void renderElementRecursive(const SvgRenderContext & ctx, const tinyxml2::XMLElement * ele) {
            for (auto child = ele->FirstChildElement(); child; child = child->NextSiblingElement()) {
                std::string name = child->Name();
                if (name == "g") {
                    renderElementRecursive(ctx, child);
                } else if (name == "rect") {
                    renderRectElement(ctx, child);
                } else if (name == "polygon") {
                    renderPolygonElement(ctx, child);
                } else if (name == "circle") {
                    renderCircleElement(ctx, child);
                } else if (name == "path") {
                    renderPathElement(ctx, child);
                }
            }
        }

        void setupContextFromRoot(
            SvgRenderContext &           ctx,
            const tinyxml2::XMLElement * root,
            int &                        outWidth,
            int &                        outHeight) {

            glm::vec2 imageDimensions(0.0f);
            root->QueryFloatAttribute("width", &imageDimensions.x);
            root->QueryFloatAttribute("height", &imageDimensions.y);

            const char *           viewBoxAttr = root->Attribute("viewBox");
            std::vector<glm::vec2> viewBox;
            if (viewBoxAttr) {
                viewBox = parsePointList(viewBoxAttr);
                if (viewBox.size() < 2) {
                    viewBox.clear();
                    viewBoxAttr = nullptr;
                }
            }

            glm::vec2 viewBoxSize(0.0f);
            glm::vec2 viewBoxOrigin(0.0f);
            if (viewBoxAttr) {
                viewBoxOrigin = viewBox[0];
                viewBoxSize   = viewBox[1];

                // 如果 svg 没写 width/height，就用 viewBox 的尺寸
                if (imageDimensions.y == 0.0f) {
                    imageDimensions = viewBoxSize;
                }

                float aspectRatio        = imageDimensions.x / imageDimensions.y;
                float viewBoxAspectRatio = viewBoxSize.x / viewBoxSize.y;

                // 扩展 viewBox 尺寸匹配目标长宽比
                if (viewBoxAspectRatio < aspectRatio) {
                    viewBoxSize.x = viewBoxSize.y * aspectRatio;
                } else {
                    viewBoxSize.y = viewBoxSize.x / aspectRatio;
                }

                imageDimensions = viewBoxSize;
            }

            // 避免除零
            if (imageDimensions.x <= 0.0f || imageDimensions.y <= 0.0f) {
                imageDimensions = glm::vec2(
                    static_cast<float>(outWidth),
                    static_cast<float>(outHeight));
            }

            // 根据期望输出尺寸和 imageDimensions 计算 ratio 和真实输出宽高
            float width  = static_cast<float>(outWidth);
            float height = static_cast<float>(outHeight);
            float ratio  = 1.0f;

            if ((width / imageDimensions.x) < (height / imageDimensions.y)) {
                height = std::ceil(width * imageDimensions.y / imageDimensions.x);
                ratio  = imageDimensions.x / width;
            } else {
                width = std::ceil(height * imageDimensions.x / imageDimensions.y);
                ratio = imageDimensions.y / height;
            }

            outWidth  = static_cast<int>(width);
            outHeight = static_cast<int>(height);

            // 计算世界坐标下的 offset
            glm::vec2 offsetWorld(0.0f);
            if (viewBoxAttr) {
                glm::vec2 vbSize   = viewBox[1]; // 原始 viewBox size
                glm::vec2 vbOrigin = viewBox[0]; // 原始 viewBox origin
                offsetWorld = (vbOrigin + vbSize - imageDimensions) / (2.0f * ratio);
            } else {
                offsetWorld = glm::vec2(0.0f);
            }

            //  写入上下文，配合 svgToPixel 使用
            ctx.targetWidth  = outWidth;
            ctx.targetHeight = outHeight;

            ctx.scale     = ratio;       // 世界 -> 像素的缩放因子
            ctx.svgOrigin = offsetWorld; // 世界空间偏移，对齐 tmp.cpp 的 offset
            ctx.pxOffset = glm::vec2(0.0f);// 【error】居中已经反映在 offsetWorld 里，不用再额外偏移像素
        }

    }

    // 接口
    void renderImage(
        ImageRGB &                   image,
        const tinyxml2::XMLElement * root,
        int &                        outWidth,
        int &                        outHeight,
        bool                         skipDrawing) {
        if (! root) {
            std::cerr << "renderImage: invalid svg root\n";
            return;
        }

        SvgRenderContext ctx;
        ctx.canvas = &image;
        ctx.skip   = skipDrawing;

        // 设置尺寸/viewBox/坐标变换
        setupContextFromRoot(ctx, root, outWidth, outHeight);
        image = VCX::Labs::Common::CreatePureImageRGB(
            ctx.targetHeight,
            ctx.targetWidth,
            glm::vec3(1.0f, 1.0f, 1.0f));

        // DOM绘制
        if (! skipDrawing) {
            renderElementRecursive(ctx, root);
        }
    }

} 