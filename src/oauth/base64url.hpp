// RFC 4648 section 5 base64url, without the '=' padding that PKCE omits.
#pragma once

#include <string>
#include <string_view>

namespace multistream {

std::string base64url_encode(std::string_view data);

} // namespace multistream
