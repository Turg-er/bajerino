#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <sol/forward.hpp>

namespace chatterino {
class Plugin;
}

namespace chatterino::lua::api::images {

/* @lua-fragment
---@class c2.Image
---@field url string The url of this image.
---@field is_loaded boolean Is this image currently loaded in RAM?
---@field is_empty boolean Is this image empty?
---@field width integer The scaled width of this image in pixels.
---@field height integer The scaled height of this image in pixels.
---@field scale number The scale factor applied to the image.
---@field size QSizeF The scaled size of this image in pixels.
---@field animated boolean Is this image animated? Note that this requires the image to be loaded.
c2.Image = {}
---@param url string The URL to create the image with.
---@param scale? number The scale this image should have (e.g. `0.5`, `0.25`). Defaults to 1.
---@param expected_size? QSize The expected unscaled size of the image. This is only used as a hint when the image is not yet loaded to avoid layout shifts.
---@return c2.Image
function c2.Image.from_url(url, scale, expected_size) end
---@return c2.Image
function c2.Image.empty() end
---@class c2.ImageSet
---@field image1 c2.Image The base image (1x).
---@field image2 c2.Image The first scaled image (often 2x, `scale=0.5`)
---@field image3 c2.Image The second scaled image (often 3x or 4x, `scale=0.25`)
---@field image4 c2.Image The optional third scaled image (`scale=0.125`)
c2.ImageSet = {}
---@param image1? c2.Image|string
---@param image2? c2.Image|string
---@param image3? c2.Image|string
---@param image4? c2.Image|string
---@return c2.ImageSet
function c2.ImageSet.new(image1, image2, image3, image4) end
*/
void createUserTypes(sol::table &c2);

}

#endif
