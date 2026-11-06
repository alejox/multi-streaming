// Which icon a source type gets. The ids are libobs's unversioned source type ids.
#pragma once

#include <string_view>

namespace multistream {

enum class SourceIcon { Camera, Display, Window, Game, Image, Media, Text, Browser, Color, Audio, Group, Scene, Fallback };

SourceIcon source_icon_for_type(std::string_view type_id);

// Relative to the plugin's data directory.
const char *source_icon_file(SourceIcon icon);

} // namespace multistream
