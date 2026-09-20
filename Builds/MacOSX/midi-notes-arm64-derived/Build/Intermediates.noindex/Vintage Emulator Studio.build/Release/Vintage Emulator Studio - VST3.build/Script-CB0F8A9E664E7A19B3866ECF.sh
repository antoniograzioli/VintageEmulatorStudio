#!/bin/sh
set -euo pipefail
"${SRCROOT}/../../platform/macos/normalize-vst3-moduleinfo.sh" "${CONFIGURATION_BUILD_DIR}/juce_vst3_helper" "${CONFIGURATION_BUILD_DIR}/${WRAPPER_NAME}/Contents/Resources/moduleinfo.json"

