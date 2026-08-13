#pragma once

#include <Arduino.h>
#include <functional>

enum class OtaResult : uint8_t {
  Updated,
  UpToDate,
  NoConfiguration,
  NetworkFailed,
  ManifestFailed,
  SignatureFailed,
  Incompatible,
  DownloadFailed,
  ImageInvalid,
  InstallFailed,
};

using OtaStatusCallback = std::function<void(const char *message, int progress)>;

OtaResult performSignedOta(const char *ssid, const char *password,
                           OtaStatusCallback status);
const char *otaResultMessage(OtaResult result);
