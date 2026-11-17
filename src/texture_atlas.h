#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <cassert>
#include <iostream>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mSize{size}, mXOffset{0}, mYOffset{0}, mMaxHeight{0} {}
    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
        
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) { 

        // check if the rectangle fits horizontally
        if (mXOffset + rectSize.x > mSize.x) {
            // move down to bottom of the tallest rectangle on prev row
            mYOffset += mMaxHeight;

            // start new row
            mXOffset = 0;
            mMaxHeight = 0;
        }

        // check whether the rectangle fits inside bin
        if (rectSize.x > mSize.x ||
            mYOffset + rectSize.y > mSize.y) {
            return {false, std::numeric_limits<size_t>::max()};
        }

        // save rectangle bounds
        size_t id = mRects.size();

        mRects.emplace_back(
            sf::Vector2f(mXOffset, mYOffset),
            rectSize
        );

        // move x offset past new rect
        mXOffset += rectSize.x;
        mMaxHeight = std::max(mMaxHeight, rectSize.y);
        return {true, id}; 
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    // Note that in this coordinate system, y points downwards from the origin at the top-left.
    
    // get bounds of rectangle by id
    sf::FloatRect getBounds(size_t rectID) const {
        assert(rectID < mRects.size());

        return mRects[rectID];
    }

private:
    sf::Vector2f mSize;
    float mXOffset;
    float mYOffset;
    float mMaxHeight;
    std::vector<sf::FloatRect> mRects;
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE

// add padding around img
inline sf::Image padImage(const sf::Image& img, uint pixels) { 
    sf::Vector2u originalSize = img.getSize();

    // return empty image if original image is empty
    if (originalSize.x == 0 || originalSize.y == 0) {
        return sf::Image{};
    }

    // calculate new image size
    sf::Vector2u newSize(
        originalSize.x + 2 * pixels,
        originalSize.y + 2 * pixels
    );

    sf::Image padded(newSize);

    // copy pixels and extend edges
    for (unsigned int y = 0; y < newSize.y; ++y) {
        for (unsigned int x = 0; x < newSize.x; ++x) {

            // find closest valid pixel in original image
            int sourceX = std::clamp(
                static_cast<int>(x) - static_cast<int>(pixels),
                0,
                static_cast<int>(originalSize.x) - 1
            );

            int sourceY = std::clamp(
                static_cast<int>(y) - static_cast<int>(pixels),
                0,
                static_cast<int>(originalSize.y) - 1
            );

            // copy pixel colour
            sf::Color color = img.getPixel({
                static_cast<unsigned int>(sourceX),
                static_cast<unsigned int>(sourceY)
            });

            padded.setPixel({x, y}, color);
        }
    }

    return padded;
    //return {}; 
};

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {}

    // load image, add padding, and put it inside atlas
    TextureID registerTexture(const std::string& filepath) {
        sf::Image img;

        // load original image
        if (!img.loadFromFile(filepath)) {
            std::cerr << "failed to load texture: "
                      << filepath << '\n';

            return std::numeric_limits<size_t>::max();
        }

        // save original image size
        sf::Vector2u originalSize = img.getSize();

        // add padding around image
        sf::Image padded = padImage(img, static_cast<uint>(mPadding));

        sf::Vector2u paddedSize = padded.getSize();

        // find space inside atlas
        auto [success, rectID] = mPacker.pack(
            sf::Vector2f(paddedSize)
        );

        // check if image fits
        if (!success) {
            std::cerr << "texture atlas is full: "
                      << filepath << '\n';

            return std::numeric_limits<size_t>::max();
        }

        // get rectangle position
        sf::FloatRect bounds = mPacker.getBounds(rectID);

        // copy padded image into atlas
        mTexture.update(padded,{static_cast<unsigned int>(bounds.position.x), static_cast<unsigned int>(bounds.position.y)});

        // save original image size
        mOriginalSizes.push_back(originalSize);

        return rectID;
    }

    // get texture bounds in opengl uv coordinates
    sf::FloatRect getBoundsUV(TextureID id) const {
        assert(id < mOriginalSizes.size());

        // get original image size and atlas size
        sf::Vector2u originalSize = mOriginalSizes[id];
        sf::Vector2u atlasSize = mTexture.getSize();

        // get rectangle position
        sf::FloatRect bounds = mPacker.getBounds(id);

        // remove padding from position
        float x = bounds.position.x + static_cast<float>(mPadding);
        float y = bounds.position.y + static_cast<float>(mPadding);

        // convert top-left position to bottom-left uv position
        float u = x / static_cast<float>(atlasSize.x);
        float v = 1.0f - (y + originalSize.y) / static_cast<float>(atlasSize.y);

        // calculate original image size in uv coordinates
        float width = static_cast<float>(originalSize.x) /
                      static_cast<float>(atlasSize.x);

        float height = static_cast<float>(originalSize.y) /
                       static_cast<float>(atlasSize.y);

        return sf::FloatRect(
            {u, v},
            {width, height}
        );
    }

    // get original image size in pixels
    sf::Vector2u getSizePixels(TextureID id) const {
        assert(id < mOriginalSizes.size());

        return mOriginalSizes[id];
    }

    const sf::Texture& getTexture() const {
        return mTexture;
    }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
    // store original image sizes
    std::vector<sf::Vector2u> mOriginalSizes;
};

#endif  // TEXTURE_ATLAS_H
