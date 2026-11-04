// BLP transcode: re-encode an uncompressed-BGRA (encoding 3) image to DXT5 the Client decodes.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

// The Client's texture loader decodes palettized (1) and DXT (2) BLP, but NOT the uncompressed-BGRA
// encoding (3) introduced after it. A modern source ships smooth textures (skies) as encoding 3, which the
// Client cannot read -> they render white. This re-encodes an encoding-3 image to DXT5 (encoding 2), which
// the Client reads, keeping the full mip chain. Pure bytes -> bytes.
namespace wxl::modern::assets::textures::blp
{
    // Transcode an encoding-3 BLP to DXT5. Returns false (serve raw) for any other encoding or a non-BLP.
    bool TranscodeBlp(std::span<const uint8_t> in, std::vector<uint8_t>& out);

    // Character component textures are CPU-composited by the client. Native components are palettized
    // BLP2 (encoding 1), while modern retail components are commonly DXT. Converts DXT components to that
    // legacy shape while preserving the source mip-0 dimensions; uncompressed BGRA (encoding 3) components
    // are converted the same way. The caller must enforce the client's
    // active decode limit before conversion.
    bool TextureComponentToPaletted(std::span<const uint8_t> in, std::vector<uint8_t>& out);

}
