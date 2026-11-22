#pragma once

#include "TeleportStore.h"

#include <inc/types.h>

#include <optional>
#include <string>

namespace Teleporter {
std::optional<std::string> PromptTextInput(const std::string& title,
    const std::string& defaultText, int maxLength = 64);
bool ParseFloat(const std::string& raw, float& output);
bool ParseCoordinates(const std::string& raw, Vector3& output);

SavedLocation CaptureCurrentLocation();
Vector3 CurrentPosition();

bool TeleportTo(Vector3 destination, std::optional<float> heading = std::nullopt,
    bool autoGroundZ = false, const std::string& label = {});
bool TeleportToWaypoint();
}
