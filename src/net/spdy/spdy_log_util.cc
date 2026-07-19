// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_log_util.h"

#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/http/http_log_util.h"
#include "net/log/net_log_values.h"

namespace net {

base::Value ElideGoAwayDebugDataForNetLog(NetLogCaptureMode capture_mode,
                                          std::string_view debug_data) {
  if (NetLogCaptureIncludesSensitive(capture_mode))
    return NetLogStringValue(debug_data);

  return NetLogStringValue(base::StrCat(
      {"[", base::NumberToString(debug_data.size()), " bytes were stripped]"}));
}

base::ListValue ElideHttpHeaderBlockForNetLog(
    const quiche::HttpHeaderBlock& headers,
    NetLogCaptureMode capture_mode) {
  base::ListValue headers_list;
  const auto method = headers.find(":method");
  const auto protocol = headers.find(":protocol");
  const bool redact_connect_udp_target =
      method != headers.end() && method->second == "CONNECT" &&
      protocol != headers.end() && protocol->second == "connect-udp";
  for (const auto& [key, value] : headers) {
    // CONNECT-UDP encodes its target in :path. Unlike ordinary request paths,
    // this proxy destination is never safe to persist, even in kEverything
    // captures used for transport debugging.
    if (redact_connect_udp_target && key == ":path") {
      headers_list.Append(":path: [redacted]");
      continue;
    }
    headers_list.Append(NetLogStringValue(base::StrCat(
        {key, ": ", ElideHeaderValueForNetLog(capture_mode, key, value)})));
  }
  return headers_list;
}

base::DictValue HttpHeaderBlockNetLogParams(
    const quiche::HttpHeaderBlock* headers,
    NetLogCaptureMode capture_mode) {
  return base::DictValue().Set(
      "headers", ElideHttpHeaderBlockForNetLog(*headers, capture_mode));
}

}  // namespace net
